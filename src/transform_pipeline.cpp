#include "transform_pipeline.hpp"

#include <csv.hpp>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <exprtk.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace csvzall::pipeline {

namespace {

enum class NumericParseMode {
  Strict,
  EmptyOrInvalidAsZero,
};

struct ExprContext {
  std::vector<std::string> variable_names;
  std::vector<std::size_t> column_indexes;
  std::vector<double> variable_values;
  exprtk::symbol_table<double> symbol_table;
  exprtk::expression<double> expression;
};

std::string Trim(std::string value) {
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }

  std::size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
    --last;
  }

  return value.substr(first, last - first);
}

bool IsIdentifier(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  if (!(std::isalpha(static_cast<unsigned char>(s[0])) != 0 || s[0] == '_')) {
    return false;
  }
  for (char ch : s) {
    if (!(std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_')) {
      return false;
    }
  }
  return true;
}

// ExprTk uses "and"/"or" keywords.  Translate C-style && / || so users
// can write either style on the command line.
std::string NormalizeLogicalOps(const std::string& text) {
  std::string result;
  result.reserve(text.size() + 16);
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (i + 1 < text.size() && text[i] == '&' && text[i + 1] == '&') {
      result += " and ";
      ++i;
    } else if (i + 1 < text.size() && text[i] == '|' && text[i + 1] == '|') {
      result += " or ";
      ++i;
    } else {
      result += text[i];
    }
  }
  return result;
}

// Scan expression text for tokens that look like identifiers.
std::set<std::string> ExtractIdentifiers(const std::string& text) {
  std::set<std::string> names;
  std::size_t i = 0;
  while (i < text.size()) {
    if (std::isalpha(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_') {
      std::size_t start = i;
      while (i < text.size() &&
             (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
        ++i;
      }
      names.insert(text.substr(start, i - start));
    } else {
      ++i;
    }
  }
  return names;
}

std::string DoubleToString(double value) {
  std::ostringstream oss;
  oss << std::setprecision(15) << value;
  return oss.str();
}

std::vector<std::string> RowToStrings(const csv::CSVRow& row) {
  std::vector<std::string> values;
  values.reserve(row.size());
  for (std::size_t i = 0; i < row.size(); ++i) {
    values.push_back(row[i].get<std::string>());
  }
  return values;
}

void AccumulateRowBytes(const csv::CSVRow& row, RunStats& stats) {
  for (std::size_t i = 0; i < row.size(); ++i) {
    stats.bytes_processed += static_cast<std::uint64_t>(row[i].get<std::string>().size());
  }
}

std::optional<ExprContext> CompileExpression(const std::string& expression_text,
                                            const std::vector<std::string>& headers,
                                            const LoggerCallbacks& logger) {
  const std::string normalized = NormalizeLogicalOps(expression_text);

  // Map valid-identifier column names to their indexes.
  std::unordered_map<std::string, std::size_t> header_to_index;
  for (std::size_t i = 0; i < headers.size(); ++i) {
    if (IsIdentifier(headers[i])) {
      header_to_index[headers[i]] = i;
    }
  }

  // Only register columns whose names actually appear in the expression.
  const auto referenced = ExtractIdentifiers(normalized);

  ExprContext ctx;

  for (const auto& name : referenced) {
    auto it = header_to_index.find(name);
    if (it != header_to_index.end()) {
      ctx.variable_names.push_back(name);
      ctx.column_indexes.push_back(it->second);
    }
  }
  ctx.variable_values.assign(ctx.variable_names.size(), 0.0);

  for (std::size_t i = 0; i < ctx.variable_names.size(); ++i) {
    // add_variable returns false if the name clashes with an ExprTk
    // built-in; silently skip those columns.
    ctx.symbol_table.add_variable(ctx.variable_names[i], ctx.variable_values[i]);
  }
  ctx.symbol_table.add_constants();

  ctx.expression.register_symbol_table(ctx.symbol_table);

  exprtk::parser<double> parser;
  if (!parser.compile(normalized, ctx.expression)) {
    if (logger.error) {
      logger.error("Expression parse error: " + expression_text);
      for (std::size_t i = 0; i < parser.error_count(); ++i) {
        const auto err = parser.get_error(i);
        logger.error("  " + exprtk::parser_error::to_str(err.mode) + ": " + err.diagnostic);
      }
    }
    return std::nullopt;
  }

  return ctx;
}

bool BindRowValues(const csv::CSVRow& row, ExprContext& ctx,
                   std::size_t row_number, const LoggerCallbacks& logger,
                   NumericParseMode parse_mode) {
  for (std::size_t i = 0; i < ctx.variable_names.size(); ++i) {
    const std::size_t col_idx = ctx.column_indexes[i];
    if (col_idx >= row.size()) {
      if (parse_mode == NumericParseMode::EmptyOrInvalidAsZero) {
        ctx.variable_values[i] = 0.0;
        continue;
      }
      if (logger.error) {
        logger.error("Row " + std::to_string(row_number) + " is missing column '" +
                     ctx.variable_names[i] + "'.");
      }
      return false;
    }

    double value = 0.0;
    if (row[col_idx].try_get(value)) {
      ctx.variable_values[i] = value;
    } else if (parse_mode == NumericParseMode::EmptyOrInvalidAsZero) {
      ctx.variable_values[i] = 0.0;
    } else {
      if (logger.error) {
        logger.error("Row " + std::to_string(row_number) + " has non-numeric value in column '" +
                     ctx.variable_names[i] + "'.");
      }
      return false;
    }
  }
  return true;
}

}  // namespace

int RunDerive(const std::string& assignment, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  (void)options;

  const auto equals_pos = assignment.find('=');
  if (equals_pos == std::string::npos) {
    if (logger.error) {
      logger.error("Invalid derive assignment. Expected format: NewCol = expression");
    }
    return 1;
  }

  const std::string new_column = Trim(assignment.substr(0, equals_pos));
  const std::string expression_text = Trim(assignment.substr(equals_pos + 1));

  if (new_column.empty() || expression_text.empty()) {
    if (logger.error) {
      logger.error("Invalid derive assignment. Both column name and expression are required.");
    }
    return 1;
  }

  std::unique_ptr<std::istringstream> buffered_stdin;
  std::istream* parse_input = &input;
  if (options.input_is_stdin) {
    std::ostringstream raw;
    raw << input.rdbuf();
    buffered_stdin = std::make_unique<std::istringstream>(raw.str());
    parse_input = buffered_stdin.get();
  }

  csv::CSVFormat format;
  format.delimiter(',').quote('"').header_row(0);
  csv::CSVReader reader(*parse_input, format);
  const auto headers = reader.get_col_names();
  if (headers.empty()) {
    if (logger.error) {
      logger.error("Input appears to have no header row.");
    }
    return 1;
  }

  if (std::find(headers.begin(), headers.end(), new_column) != headers.end()) {
    if (logger.error) {
      logger.error("Derived column already exists: " + new_column);
    }
    return 1;
  }

  auto ctx = CompileExpression(expression_text, headers, logger);
  if (!ctx.has_value()) {
    return 1;
  }

  auto writer = csv::make_csv_writer_buffered(output);
  std::vector<std::string> output_headers = headers;
  output_headers.push_back(new_column);
  writer << output_headers;

  csv::CSVRow row;
  std::size_t row_number = 1;
  while (reader.read_row(row)) {
    ++row_number;
    if (!BindRowValues(row, *ctx, row_number, logger, NumericParseMode::Strict)) {
      return 1;
    }

    const double result = ctx->expression.value();
    if (!std::isfinite(result)) {
      if (logger.error) {
        logger.error("Expression evaluated to non-finite value at row " + std::to_string(row_number) + ".");
      }
      return 1;
    }

    auto out_row = RowToStrings(row);
    out_row.push_back(DoubleToString(result));
    writer << out_row;

    stats.rows_processed++;
    AccumulateRowBytes(row, stats);
  }

  writer.flush();
  return 0;
}

int RunFilter(const std::string& expression, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  (void)options;

  const std::string expression_text = Trim(expression);
  if (expression_text.empty()) {
    if (logger.error) {
      logger.error("Filter expression cannot be empty.");
    }
    return 1;
  }

  std::unique_ptr<std::istringstream> buffered_stdin;
  std::istream* parse_input = &input;
  if (options.input_is_stdin) {
    std::ostringstream raw;
    raw << input.rdbuf();
    buffered_stdin = std::make_unique<std::istringstream>(raw.str());
    parse_input = buffered_stdin.get();
  }

  csv::CSVFormat format;
  format.delimiter(',').quote('"').header_row(0);
  csv::CSVReader reader(*parse_input, format);
  const auto headers = reader.get_col_names();
  if (headers.empty()) {
    if (logger.error) {
      logger.error("Input appears to have no header row.");
    }
    return 1;
  }

  auto ctx = CompileExpression(expression_text, headers, logger);
  if (!ctx.has_value()) {
    return 1;
  }

  auto writer = csv::make_csv_writer_buffered(output);
  writer << headers;

  csv::CSVRow row;
  std::size_t row_number = 1;
  while (reader.read_row(row)) {
    ++row_number;
    if (!BindRowValues(row, *ctx, row_number, logger, NumericParseMode::EmptyOrInvalidAsZero)) {
      return 1;
    }

    const double result = ctx->expression.value();
    if (result != 0.0) {
      writer << RowToStrings(row);
    }

    stats.rows_processed++;
    AccumulateRowBytes(row, stats);
  }

  writer.flush();
  return 0;
}

int RunSummarize(const std::string& group_by_column, const std::string& max_column,
                 const std::vector<std::string>& show_columns,
                 std::istream& input, std::ostream& output,
                 const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  (void)options;

  std::unique_ptr<std::istringstream> buffered_stdin;
  std::istream* parse_input = &input;
  if (options.input_is_stdin) {
    std::ostringstream raw;
    raw << input.rdbuf();
    buffered_stdin = std::make_unique<std::istringstream>(raw.str());
    parse_input = buffered_stdin.get();
  }

  csv::CSVFormat format;
  format.delimiter(',').quote('"').header_row(0);
  csv::CSVReader reader(*parse_input, format);
  const auto headers = reader.get_col_names();
  if (headers.empty()) {
    if (logger.error) {
      logger.error("Input appears to have no header row.");
    }
    return 1;
  }

  // Resolve column indexes.
  auto find_column = [&](const std::string& name) -> std::optional<std::size_t> {
    for (std::size_t i = 0; i < headers.size(); ++i) {
      if (headers[i] == name) {
        return i;
      }
    }
    return std::nullopt;
  };

  const auto group_idx = find_column(group_by_column);
  if (!group_idx.has_value()) {
    if (logger.error) {
      logger.error("Group-by column not found: " + group_by_column);
    }
    return 1;
  }

  const auto max_idx = find_column(max_column);
  if (!max_idx.has_value()) {
    if (logger.error) {
      logger.error("Max column not found: " + max_column);
    }
    return 1;
  }

  std::vector<std::size_t> show_indexes;
  for (const auto& col : show_columns) {
    const auto idx = find_column(col);
    if (!idx.has_value()) {
      if (logger.error) {
        logger.error("Show column not found: " + col);
      }
      return 1;
    }
    show_indexes.push_back(*idx);
  }

  // Track the winning row (as strings) per group key.
  struct GroupRecord {
    double max_value = -std::numeric_limits<double>::infinity();
    std::vector<std::string> winning_row;
  };

  // Use a vector of pairs to preserve insertion order (first-seen order of groups).
  std::vector<std::pair<std::string, GroupRecord>> groups;
  std::unordered_map<std::string, std::size_t> group_index_map;

  csv::CSVRow row;
  while (reader.read_row(row)) {
    const std::string group_key = row[*group_idx].get<std::string>();

    double value = 0.0;
    if (!row[*max_idx].try_get(value)) {
      continue;  // Skip rows where the max column isn't numeric.
    }

    auto it = group_index_map.find(group_key);
    if (it == group_index_map.end()) {
      group_index_map[group_key] = groups.size();
      GroupRecord rec;
      rec.max_value = value;
      rec.winning_row = RowToStrings(row);
      groups.emplace_back(group_key, rec);
    } else if (value > groups[it->second].second.max_value) {
      groups[it->second].second.max_value = value;
      groups[it->second].second.winning_row = RowToStrings(row);
    }

    stats.rows_processed++;
    AccumulateRowBytes(row, stats);
  }

  // Build output: group-by column, then --show columns, then max column.
  auto writer = csv::make_csv_writer_buffered(output);
  std::vector<std::string> out_headers;
  out_headers.push_back(group_by_column);
  for (const auto& col : show_columns) {
    out_headers.push_back(col);
  }
  out_headers.push_back("max_" + max_column);
  writer << out_headers;

  for (const auto& [key, record] : groups) {
    std::vector<std::string> out_row;
    out_row.push_back(key);
    for (const auto idx : show_indexes) {
      if (idx < record.winning_row.size()) {
        out_row.push_back(record.winning_row[idx]);
      } else {
        out_row.emplace_back();
      }
    }
    out_row.push_back(DoubleToString(record.max_value));
    writer << out_row;
  }

  writer.flush();
  return 0;
}

}  // namespace csvzall::pipeline

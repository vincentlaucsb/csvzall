#include "expr_engine.hpp"

#include "../common/column_lookup.hpp"

#include <cctype>
#include <set>
#include <string>

namespace csvzall::pipeline::expression {

namespace {

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

}  // namespace

std::optional<ExprContext> CompileExpression(const std::string& expression_text,
                                             const std::vector<std::string>& headers,
                                             const RunOptions& options,
                                             const LoggerCallbacks& logger) {
  const std::string normalized = NormalizeLogicalOps(expression_text);

  const auto referenced = ExtractIdentifiers(normalized);

  ExprContext ctx;
  for (const auto& name : referenced) {
    const auto idx = common::FindColumnIndex(headers, name, options.exact_column_matching);
    if (!idx.has_value()) {
      continue;
    }
    ctx.variable_names.push_back(name);
    ctx.column_indexes.push_back(*idx);
  }
  ctx.variable_values.assign(ctx.variable_names.size(), 0.0);

  for (std::size_t i = 0; i < ctx.variable_names.size(); ++i) {
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

}  // namespace csvzall::pipeline::expression

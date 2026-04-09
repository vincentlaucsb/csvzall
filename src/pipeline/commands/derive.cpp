#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../common/row_utils.hpp"
#include "../expression/expr_engine.hpp"

#include <csv.hpp>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace csvzall::pipeline::commands {

int RunDerive(const std::string& assignment, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  const auto equals_pos = assignment.find('=');
  if (equals_pos == std::string::npos) {
    if (logger.error) {
      logger.error("Invalid derive assignment. Expected format: NewCol = expression");
    }
    return 1;
  }

  const std::string new_column = common::Trim(assignment.substr(0, equals_pos));
  const std::string expression_text = common::Trim(assignment.substr(equals_pos + 1));

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

  if (common::FindColumnIndex(headers, new_column, options.exact_column_matching).has_value()) {
    if (logger.error) {
      logger.error("Derived column already exists: " + new_column);
    }
    return 1;
  }

  auto ctx = expression::CompileExpression(expression_text, headers, options, logger);
  if (!ctx.has_value()) {
    return 1;
  }

  auto writer = csv::make_csv_writer_buffered(output);
  std::vector<std::string> output_headers = headers;
  output_headers.push_back(new_column);
  writer << output_headers;

  std::size_t row_number = 1;
  for (auto& row : reader) {
    ++row_number;
    if (!expression::BindRowValues(row, *ctx, row_number, logger,
                                   expression::NumericParseMode::Strict)) {
      return 1;
    }

    const double result = ctx->expression.value();
    if (!std::isfinite(result)) {
      if (logger.error) {
        logger.error("Expression evaluated to non-finite value at row " + std::to_string(row_number) + ".");
      }
      return 1;
    }

    auto out_row = std::vector<std::string>(row);
    out_row.push_back(common::DoubleToString(result));
    writer << out_row;

    stats.rows_processed++;
    common::AccumulateRowBytes(row, stats);
  }

  writer.flush();
  return 0;
}

}  // namespace csvzall::pipeline::commands

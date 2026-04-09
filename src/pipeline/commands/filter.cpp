#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../common/row_utils.hpp"
#include "../expression/expr_engine.hpp"

#include <csv.hpp>

#include <memory>
#include <sstream>
#include <string>

namespace csvzall::pipeline::commands {

int RunFilter(const std::string& expression, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  const std::string expression_text = common::Trim(expression);
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

  auto ctx = expression::CompileExpression(expression_text, headers, options, logger);
  if (!ctx.has_value()) {
    return 1;
  }

  auto writer = csv::make_csv_writer_buffered(output);
  writer << headers;

  std::size_t row_number = 1;
  for (auto& row : reader) {
    ++row_number;
    if (!expression::BindRowValues(row, *ctx, row_number, logger,
                                   expression::NumericParseMode::EmptyOrInvalidAsZero)) {
      return 1;
    }

    const double result = ctx->expression.value();
    if (result != 0.0) {
      writer << std::vector<std::string>(row);
    }

    stats.rows_processed++;
    common::AccumulateRowBytes(row, stats);
  }

  writer.flush();
  return 0;
}

}  // namespace csvzall::pipeline::commands

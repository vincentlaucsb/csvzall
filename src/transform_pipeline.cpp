#include "transform_pipeline.hpp"
#include "pipeline/commands/commands.hpp"

namespace csvzall::pipeline {

int RunDerive(const std::string& assignment, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  return commands::RunDerive(assignment, input, output, options, logger, stats);
}

int RunFilter(const std::string& expression, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  return commands::RunFilter(expression, input, output, options, logger, stats);
}

int RunSummarize(const std::string& group_by_column, const std::string& max_column,
                 const std::vector<std::string>& show_columns,
                 std::istream& input, std::ostream& output,
                 const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  return commands::RunSummarize(group_by_column, max_column, show_columns,
                                input, output, options, logger, stats);
}

int RunTimeseries(const std::string& x_column, const std::string& y_column,
                  const std::string& series_column, const std::string& reduce,
                  const std::string& format,
                  std::istream& input, std::ostream& output,
                  const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  return commands::RunTimeseries(x_column, y_column, series_column, reduce, format,
                                 input, output, options, logger, stats);
}

int RunSqlExport(const std::string& input_path,
                 const std::string& dest_path,
                 const std::string& table_name,
                 std::istream& input,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats) {
  return commands::RunSqlExport(input_path, dest_path, table_name, input, options, logger, stats);
}

}  // namespace csvzall::pipeline

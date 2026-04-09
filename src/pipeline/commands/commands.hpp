#pragma once

#include "../../transform_pipeline.hpp"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace csvzall::pipeline::commands {

int RunDerive(const std::string& assignment, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunFilter(const std::string& expression, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunSummarize(const std::string& group_by_column, const std::string& max_column,
                 const std::vector<std::string>& show_columns,
                 std::istream& input, std::ostream& output,
                 const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunTimeseries(const std::string& x_column, const std::string& y_column,
                  const std::string& series_column, const std::string& reduce,
                  const std::string& format,
                  std::istream& input, std::ostream& output,
                  const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

}  // namespace csvzall::pipeline::commands

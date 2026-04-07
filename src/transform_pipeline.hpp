#pragma once

#include <cstdint>
#include <functional>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace csvzall::pipeline {

struct RunOptions {
  bool single_threaded = false;
  bool input_is_stdin = false;
};

struct RunStats {
  std::uint64_t rows_processed = 0;
  std::uint64_t bytes_processed = 0;
};

struct LoggerCallbacks {
  std::function<void(const std::string&)> error;
  std::function<void(const std::string&)> verbose;
};

int RunDerive(const std::string& assignment, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunFilter(const std::string& expression, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunSummarize(const std::string& group_by_column, const std::string& max_column,
                 const std::vector<std::string>& show_columns,
                 std::istream& input, std::ostream& output,
                 const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

}  // namespace csvzall::pipeline

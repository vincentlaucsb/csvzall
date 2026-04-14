#pragma once

#include <csv.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "../src/pipeline_types.hpp"

namespace csvzall::tests {

// Build a CSV string using csv-parser's own writer so quoting and escaping
// are handled correctly.
inline std::string MakeTestCsv(const std::vector<std::string>& headers,
                               const std::vector<std::vector<std::string>>& rows) {
  std::ostringstream oss;
  auto writer = csv::make_csv_writer(oss);
  writer << headers;
  for (const auto& row : rows) {
    writer << row;
  }
  return oss.str();
}

// Parse a CSV string using csv-parser's own reader.
// Returns all rows including the header row as row 0.
inline std::vector<std::vector<std::string>> ParseCsv(const std::string& csv_text) {
  std::vector<std::vector<std::string>> result;
  std::istringstream stream(csv_text);
  csv::CSVReader reader(stream);

  // Header row first.
  result.push_back(reader.get_col_names());

  for (auto& row : reader) {
    result.push_back(std::vector<std::string>(row));
  }

  return result;
}

// RunOptions suitable for unit tests: treats input as a stream (not a file),
// no exact column matching, auto-detect delimiter.
inline csvzall::pipeline::RunOptions MakeTestOptions(bool exact = false) {
  csvzall::pipeline::RunOptions opts;
  opts.input_is_stdin = true;
  opts.exact_column_matching = exact;
  return opts;
}

// Null-sink LoggerCallbacks for tests that don't inspect log output.
inline csvzall::pipeline::LoggerCallbacks MakeNullLogger() {
  return {nullptr, nullptr};
}

}  // namespace csvzall::tests

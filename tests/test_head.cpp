#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <zlib.h>

#include "../src/head.hpp"
#include "../src/pipeline_types.hpp"

namespace {

struct TestLogger {
  void Error(const std::string& msg) { last_error = msg; }
  void Verbose(const std::string&) const {}
  std::string last_error;
};

// Parse the head table output ("+---+" separators and "| cell |" rows) into a
// 2-D grid of trimmed strings.  Row 0 is the header row.
std::vector<std::vector<std::string>> ParseHeadTable(const std::string& output) {
  std::vector<std::vector<std::string>> result;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] != '|') continue;  // skip +---+ separators
    std::vector<std::string> row;
    // Drop the leading '|' then split on remaining '|' characters.
    std::istringstream cells(line.substr(1));
    std::string cell;
    while (std::getline(cells, cell, '|')) {
      // Trim leading/trailing spaces.
      const auto start = cell.find_first_not_of(' ');
      const auto end   = cell.find_last_not_of(' ');
      row.push_back(start == std::string::npos ? "" : cell.substr(start, end - start + 1));
    }
    result.push_back(std::move(row));
  }
  return result;
}

// Count how many column segments the first separator line has (= number of
// columns). Separator lines look like "+----+-------+---+".
std::size_t CountSeparatorColumns(const std::string& output) {
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line[0] == '+') {
      return static_cast<std::size_t>(std::count(line.begin(), line.end(), '+')) - 1;
    }
  }
  return 0;
}

std::filesystem::path WriteGzipCsv(const std::string& csv_text) {
  auto path = std::filesystem::temp_directory_path() /
              std::filesystem::path("csvzall_head_test.csv.gz");

  gzFile file = gzopen(path.string().c_str(), "wb");
  REQUIRE(file != nullptr);
  const auto written = gzwrite(file, csv_text.data(), static_cast<unsigned int>(csv_text.size()));
  REQUIRE(written == static_cast<int>(csv_text.size()));
  REQUIRE(gzclose(file) == Z_OK);
  return path;
}

}  // namespace

// ---------------------------------------------------------------------------
// CSV (comma-separated) — baseline
// ---------------------------------------------------------------------------
TEST_CASE("Head: CSV comma-separated") {
  const std::string input =
    "name,value,active\n"
    "alice,10,yes\n"
    "bob,20,no\n";

  std::istringstream in(input);
  std::ostringstream out;
  TestLogger logger;
  csvzall::head::Result result;

  const int rc = csvzall::head::Run(10, in, out, csvzall::pipeline::RunOptions{}, logger, result);

  REQUIRE(rc == 0);
  const auto table = ParseHeadTable(out.str());
  REQUIRE(table.size() == 3);         // header + 2 data rows
  REQUIRE(table[0][0] == "name");
  REQUIRE(table[0][1] == "value");
  REQUIRE(table[0][2] == "active");
  REQUIRE(table[1][0] == "alice");
  REQUIRE(table[2][0] == "bob");
}

TEST_CASE("Head: gzip CSV") {
  const std::string input =
    "name,value,active\n"
    "alice,10,yes\n"
    "bob,20,no\n";

  const auto path = WriteGzipCsv(input);

  std::istringstream ignored;
  std::ostringstream out;
  TestLogger logger;
  csvzall::head::Result result;
  csvzall::pipeline::RunOptions options;
  options.input_path = path.string();

  const int rc = csvzall::head::Run(10, ignored, out, options, logger, result);
  std::filesystem::remove(path);

  REQUIRE(rc == 0);
  const auto table = ParseHeadTable(out.str());
  REQUIRE(table.size() == 3);
  REQUIRE(table[0][0] == "name");
  REQUIRE(table[1][0] == "alice");
  REQUIRE(table[2][0] == "bob");
}

// ---------------------------------------------------------------------------
// TSV (tab-separated)
// ---------------------------------------------------------------------------
TEST_CASE("Head: TSV tab-separated") {
  const std::string input =
    "name\tvalue\tactive\n"
    "alice\t10\tyes\n"
    "bob\t20\tno\n";

  std::istringstream in(input);
  std::ostringstream out;
  TestLogger logger;
  csvzall::head::Result result;

  const int rc = csvzall::head::Run(10, in, out, csvzall::pipeline::RunOptions{}, logger, result);

  REQUIRE(rc == 0);
  const auto table = ParseHeadTable(out.str());
  REQUIRE(table.size() == 3);         // header + 2 data rows
  REQUIRE(table[0][0] == "name");
  REQUIRE(table[0][1] == "value");
  REQUIRE(table[0][2] == "active");
  REQUIRE(table[1][0] == "alice");
  REQUIRE(table[2][0] == "bob");
}

// ---------------------------------------------------------------------------
// PSV (pipe-separated)
// ---------------------------------------------------------------------------
TEST_CASE("Head: PSV pipe-separated") {
  const std::string input =
    "name|value|active\n"
    "alice|10|yes\n"
    "bob|20|no\n";

  std::istringstream in(input);
  std::ostringstream out;
  TestLogger logger;
  csvzall::head::Result result;

  const int rc = csvzall::head::Run(10, in, out, csvzall::pipeline::RunOptions{}, logger, result);

  REQUIRE(rc == 0);
  // ParseHeadTable splits on '|', which is also the PSV delimiter, so it
  // accidentally extracts the right-looking values even from a
  // single-column misparse.  Use the separator line instead: correct
  // parsing produces 3 column segments; wrong (comma) parsing produces 1.
  REQUIRE(CountSeparatorColumns(out.str()) == 3);
  const auto table = ParseHeadTable(out.str());
  REQUIRE(table.size() == 3);         // header + 2 data rows
  REQUIRE(table[0][0] == "name");
  REQUIRE(table[0][1] == "value");
  REQUIRE(table[0][2] == "active");
  REQUIRE(table[1][0] == "alice");
  REQUIRE(table[2][0] == "bob");
}

// ---------------------------------------------------------------------------
// Semicolon-separated
// ---------------------------------------------------------------------------
TEST_CASE("Head: semicolon-separated") {
  const std::string input =
    "name;value;active\n"
    "alice;10;yes\n"
    "bob;20;no\n";

  std::istringstream in(input);
  std::ostringstream out;
  TestLogger logger;
  csvzall::head::Result result;

  const int rc = csvzall::head::Run(10, in, out, csvzall::pipeline::RunOptions{}, logger, result);

  REQUIRE(rc == 0);
  const auto table = ParseHeadTable(out.str());
  REQUIRE(table.size() == 3);         // header + 2 data rows
  REQUIRE(table[0][0] == "name");
  REQUIRE(table[0][1] == "value");
  REQUIRE(table[0][2] == "active");
  REQUIRE(table[1][0] == "alice");
  REQUIRE(table[2][0] == "bob");
}

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

void AppendLe16(std::string& out, const std::uint16_t value) {
  out.push_back(static_cast<char>(value & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
}

void AppendLe32(std::string& out, const std::uint32_t value) {
  out.push_back(static_cast<char>(value & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>((value >> 16) & 0xff));
  out.push_back(static_cast<char>((value >> 24) & 0xff));
}

struct ZipTestEntry {
  std::string name;
  std::string content;
  std::uint32_t crc = 0;
  std::uint32_t local_header_offset = 0;
};

std::filesystem::path WriteStoredZip(std::vector<ZipTestEntry> entries,
                                     const std::string& filename) {
  auto path = std::filesystem::temp_directory_path() / filename;
  std::string bytes;

  for (auto& entry : entries) {
    entry.crc = static_cast<std::uint32_t>(
        crc32(0, reinterpret_cast<const Bytef*>(entry.content.data()),
              static_cast<uInt>(entry.content.size())));
    entry.local_header_offset = static_cast<std::uint32_t>(bytes.size());

    AppendLe32(bytes, 0x04034b50);
    AppendLe16(bytes, 20);
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe32(bytes, entry.crc);
    AppendLe32(bytes, static_cast<std::uint32_t>(entry.content.size()));
    AppendLe32(bytes, static_cast<std::uint32_t>(entry.content.size()));
    AppendLe16(bytes, static_cast<std::uint16_t>(entry.name.size()));
    AppendLe16(bytes, 0);
    bytes += entry.name;
    bytes += entry.content;
  }

  const auto central_offset = static_cast<std::uint32_t>(bytes.size());
  for (const auto& entry : entries) {
    AppendLe32(bytes, 0x02014b50);
    AppendLe16(bytes, 20);
    AppendLe16(bytes, 20);
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe32(bytes, entry.crc);
    AppendLe32(bytes, static_cast<std::uint32_t>(entry.content.size()));
    AppendLe32(bytes, static_cast<std::uint32_t>(entry.content.size()));
    AppendLe16(bytes, static_cast<std::uint16_t>(entry.name.size()));
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe16(bytes, 0);
    AppendLe32(bytes, 0);
    AppendLe32(bytes, entry.local_header_offset);
    bytes += entry.name;
  }

  const auto central_size = static_cast<std::uint32_t>(bytes.size() - central_offset);
  AppendLe32(bytes, 0x06054b50);
  AppendLe16(bytes, 0);
  AppendLe16(bytes, 0);
  AppendLe16(bytes, static_cast<std::uint16_t>(entries.size()));
  AppendLe16(bytes, static_cast<std::uint16_t>(entries.size()));
  AppendLe32(bytes, central_size);
  AppendLe32(bytes, central_offset);
  AppendLe16(bytes, 0);

  std::ofstream file(path, std::ios::binary);
  REQUIRE(file);
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  REQUIRE(file);
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

TEST_CASE("Head: single-file ZIP defaults to its only CSV entry") {
  const std::string input =
    "name,value\n"
    "alice,10\n";

  const auto path = WriteStoredZip({{"data.csv", input}}, "csvzall_head_single.zip");

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
  REQUIRE(table.size() == 2);
  REQUIRE(table[0][0] == "name");
  REQUIRE(table[1][0] == "alice");
}

TEST_CASE("Head: multi-file ZIP requires an explicit entry") {
  const auto path = WriteStoredZip(
      {{"first.csv", "name\nalice\n"}, {"second.csv", "name\nbob\n"}},
      "csvzall_head_multi.zip");

  std::istringstream ignored;
  std::ostringstream out;
  TestLogger logger;
  csvzall::head::Result result;
  csvzall::pipeline::RunOptions options;
  options.input_path = path.string();

  const int rc = csvzall::head::Run(10, ignored, out, options, logger, result);
  std::filesystem::remove(path);

  REQUIRE(rc == 1);
  REQUIRE(logger.last_error.find("multiple files") != std::string::npos);
  REQUIRE(logger.last_error.find("--zip-entry") != std::string::npos);
}

TEST_CASE("Head: explicit ZIP entry selects one file from multi-file archive") {
  const auto path = WriteStoredZip(
      {{"first.csv", "name\nalice\n"}, {"folder/second.csv", "name\nbob\n"}},
      "csvzall_head_selected.zip");

  std::istringstream ignored;
  std::ostringstream out;
  TestLogger logger;
  csvzall::head::Result result;
  csvzall::pipeline::RunOptions options;
  options.input_path = path.string();
  options.zip_entry = "folder/second.csv";

  const int rc = csvzall::head::Run(10, ignored, out, options, logger, result);
  std::filesystem::remove(path);

  REQUIRE(rc == 0);
  const auto table = ParseHeadTable(out.str());
  REQUIRE(table.size() == 2);
  REQUIRE(table[1][0] == "bob");
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

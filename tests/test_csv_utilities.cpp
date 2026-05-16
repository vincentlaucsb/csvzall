#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

namespace {

std::filesystem::path TempCsvPath(const std::string& name) {
  return std::filesystem::temp_directory_path() /
         ("csvzall_" + name + ".csv");
}

void WriteCsvFile(const std::filesystem::path& path,
                  const std::vector<std::string>& headers,
                  const std::vector<std::vector<std::string>>& rows) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << tests::MakeTestCsv(headers, rows);
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

}  // namespace

TEST_CASE("max: streams numeric values without SQLite") {
  const auto csv = tests::MakeTestCsv(
      {"name", "weight"},
      {{"bench", "100"}, {"bench", "110"}, {"bench", "95"}});
  std::istringstream input(csv);
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  const int rc = pipeline::RunMax("weight", input, output, options, logger, stats);

  REQUIRE(rc == 0);
  REQUIRE(output.str() == "110\n");
  REQUIRE(stats.rows_processed == 3);
}

TEST_CASE("max: ISO timestamp strings compare deterministically") {
  const auto csv = tests::MakeTestCsv(
      {"when"},
      {{"2026-05-01T00:00:00Z"}, {"2026-05-03T00:00:00Z"}, {"2026-05-02T00:00:00Z"}});
  std::istringstream input(csv);
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  const int rc = pipeline::RunMax("when", input, output, options, logger, stats);

  REQUIRE(rc == 0);
  REQUIRE(output.str() == "2026-05-03T00:00:00Z\n");
}

TEST_CASE("min: streams numeric values without SQLite") {
  const auto csv = tests::MakeTestCsv(
      {"name", "weight"},
      {{"bench", "100"}, {"bench", "110"}, {"bench", "95"}});
  std::istringstream input(csv);
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  const int rc = pipeline::RunMin("weight", input, output, options, logger, stats);

  REQUIRE(rc == 0);
  REQUIRE(output.str() == "95\n");
  REQUIRE(stats.rows_processed == 3);
}

TEST_CASE("min: ISO timestamp strings compare deterministically") {
  const auto csv = tests::MakeTestCsv(
      {"when"},
      {{"2026-05-01T00:00:00Z"}, {"2026-05-03T00:00:00Z"}, {"2026-05-02T00:00:00Z"}});
  std::istringstream input(csv);
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  const int rc = pipeline::RunMin("when", input, output, options, logger, stats);

  REQUIRE(rc == 0);
  REQUIRE(output.str() == "2026-05-01T00:00:00Z\n");
}

TEST_CASE("append: rejects header mismatches before writing output") {
  const auto existing = TempCsvPath("append_existing_mismatch");
  const auto incoming = TempCsvPath("append_incoming_mismatch");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}});
  WriteCsvFile(incoming, {"id", "title"}, {{"2", "new"}});

  std::ostringstream output;
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  const int rc = pipeline::RunAppend(existing.string(), incoming.string(), false,
                                     output, logger, stats);

  REQUIRE(rc == 1);
  REQUIRE(output.str().empty());
}

TEST_CASE("append: succeeds without a key when headers match") {
  const auto existing = TempCsvPath("append_existing_ok");
  const auto incoming = TempCsvPath("append_incoming_ok");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}});
  WriteCsvFile(incoming, {"id", "name"}, {{"2", "new"}});

  std::ostringstream output;
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  const int rc = pipeline::RunAppend(existing.string(), incoming.string(), false,
                                     output, logger, stats);

  REQUIRE(rc == 0);
  REQUIRE(tests::ParseCsv(output.str()) ==
          std::vector<std::vector<std::string>>{{"id", "name"}, {"1", "old"}, {"2", "new"}});
}

TEST_CASE("append: does not inspect keys or deduplicate rows") {
  const auto existing = TempCsvPath("append_existing_no_key_semantics");
  const auto incoming = TempCsvPath("append_incoming_no_key_semantics");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}});
  WriteCsvFile(incoming, {"id", "name"}, {{"1", "duplicate allowed"}});

  std::ostringstream output;
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  REQUIRE(pipeline::RunAppend(existing.string(), incoming.string(), false,
                              output, logger, stats) == 0);
  REQUIRE(tests::ParseCsv(output.str()) ==
          std::vector<std::vector<std::string>>{
              {"id", "name"}, {"1", "old"}, {"1", "duplicate allowed"}});
}

TEST_CASE("merge: requires key") {
  const auto existing = TempCsvPath("merge_existing_no_key");
  const auto incoming = TempCsvPath("merge_incoming_no_key");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}});
  WriteCsvFile(incoming, {"id", "name"}, {{"2", "new"}});

  std::ostringstream output;
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  REQUIRE(pipeline::RunMerge(existing.string(), incoming.string(), "", false,
                             output, logger, stats) == 1);
  REQUIRE(output.str().empty());
}

TEST_CASE("merge: rejects header mismatches") {
  const auto existing = TempCsvPath("merge_existing_mismatch");
  const auto incoming = TempCsvPath("merge_incoming_mismatch");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}});
  WriteCsvFile(incoming, {"id", "title"}, {{"2", "new"}});

  std::ostringstream output;
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  REQUIRE(pipeline::RunMerge(existing.string(), incoming.string(), "id", false,
                             output, logger, stats) == 1);
  REQUIRE(output.str().empty());
}

TEST_CASE("merge: skips existing keys and appends new keys to stdout") {
  const auto existing = TempCsvPath("merge_existing_skip_stdout");
  const auto incoming = TempCsvPath("merge_incoming_skip_stdout");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}, {"2", "kept"}});
  WriteCsvFile(incoming, {"id", "name"}, {{"2", "overlap"}, {"3", "new"}});

  std::ostringstream output;
  std::string info_msg;
  auto logger = tests::MakeNullLogger();
  logger.info = [&](const std::string& msg) { info_msg = msg; };
  pipeline::RunStats stats;

  REQUIRE(pipeline::RunMerge(existing.string(), incoming.string(), "id", false,
                              output, logger, stats) == 0);
  REQUIRE(tests::ParseCsv(output.str()) ==
          std::vector<std::vector<std::string>>{
              {"id", "name"}, {"1", "old"}, {"2", "kept"}, {"3", "new"}});
  REQUIRE(info_msg.find("added 1") != std::string::npos);
  REQUIRE(info_msg.find("skipped 1") != std::string::npos);
}

TEST_CASE("merge: rejects existing and incoming duplicate keys") {
  auto logger = tests::MakeNullLogger();

  {
    const auto existing = TempCsvPath("merge_existing_dup_existing");
    const auto incoming = TempCsvPath("merge_incoming_dup_existing");
    WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}, {"1", "older"}});
    WriteCsvFile(incoming, {"id", "name"}, {{"2", "new"}});
    std::ostringstream output;
    pipeline::RunStats stats;
    REQUIRE(pipeline::RunMerge(existing.string(), incoming.string(), "id", false,
                                output, logger, stats) == 1);
  }

  {
    const auto existing = TempCsvPath("merge_existing_dup_incoming");
    const auto incoming = TempCsvPath("merge_incoming_dup_incoming");
    WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}});
    WriteCsvFile(incoming, {"id", "name"}, {{"2", "new"}, {"2", "newer"}});
    std::ostringstream output;
    pipeline::RunStats stats;
    REQUIRE(pipeline::RunMerge(existing.string(), incoming.string(), "id", false,
                                output, logger, stats) == 1);
  }
}

TEST_CASE("merge --in-place: appends only new keys") {
  const auto existing = TempCsvPath("merge_existing_inplace_ok");
  const auto incoming = TempCsvPath("merge_incoming_inplace_ok");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}, {"2", "kept"}});
  WriteCsvFile(incoming, {"id", "name"}, {{"2", "overlap"}, {"3", "new"}});

  std::ostringstream output;
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  REQUIRE(pipeline::RunMerge(existing.string(), incoming.string(), "id", true,
                             output, logger, stats) == 0);
  REQUIRE(output.str().empty());
  REQUIRE(tests::ParseCsv(ReadText(existing)) ==
          std::vector<std::vector<std::string>>{
              {"id", "name"}, {"1", "old"}, {"2", "kept"}, {"3", "new"}});
}

TEST_CASE("merge --in-place: updates only after validation succeeds") {
  const auto existing = TempCsvPath("merge_existing_inplace_fail");
  const auto incoming = TempCsvPath("merge_incoming_inplace_fail");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}});
  WriteCsvFile(incoming, {"id", "name"}, {{"2", "new"}, {"2", "newer"}});
  const auto original = ReadText(existing);

  std::ostringstream output;
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  REQUIRE(pipeline::RunMerge(existing.string(), incoming.string(), "id", true,
                              output, logger, stats) == 1);
  REQUIRE(ReadText(existing) == original);
}

TEST_CASE("append --in-place: leaves original unchanged when validation fails") {
  const auto existing = TempCsvPath("append_existing_inplace_fail");
  const auto incoming = TempCsvPath("append_incoming_inplace_fail");
  WriteCsvFile(existing, {"id", "name"}, {{"1", "old"}});
  WriteCsvFile(incoming, {"id", "title"}, {{"2", "new"}});
  const auto original = ReadText(existing);

  std::ostringstream output;
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  const int rc = pipeline::RunAppend(existing.string(), incoming.string(), true,
                                     output, logger, stats);

  REQUIRE(rc == 1);
  REQUIRE(ReadText(existing) == original);
}

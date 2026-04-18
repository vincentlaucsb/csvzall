#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <SQLiteCpp/SQLiteCpp.h>
#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

// Returns a path in the system temp directory that does not exist yet.
static std::string TempDbPath(const std::string& name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

TEST_CASE("SqlExport: basic round-trip") {
  const std::string db_path = TempDbPath("csvzall_test_basic.db");
  std::filesystem::remove(db_path);  // ensure clean state

  auto csv = tests::MakeTestCsv(
    {"name", "value"},
    {
      {"alice", "10"},
      {"bob", "20"},
      {"charlie", "5"}
    }
  );

  std::istringstream input(csv);
  pipeline::RunOptions options;
  options.input_is_stdin = true;
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;

  int rc = pipeline::RunSqlExport("-", db_path, "data", input, options, logger, stats);

  REQUIRE(rc == 0);
  REQUIRE(std::filesystem::exists(db_path));
  REQUIRE(stats.rows_processed == 3);

  // Verify contents via direct SQLite query.
  {
    SQLite::Database db(db_path, SQLite::OPEN_READONLY);
    SQLite::Statement q(db, "SELECT name, value FROM \"data\" ORDER BY value");
    REQUIRE(q.executeStep()); REQUIRE(q.getColumn(0).getString() == "charlie");
    REQUIRE(q.executeStep()); REQUIRE(q.getColumn(0).getString() == "alice");
    REQUIRE(q.executeStep()); REQUIRE(q.getColumn(0).getString() == "bob");
  }

  std::filesystem::remove(db_path);
}

TEST_CASE("SqlExport: custom table name") {
  const std::string db_path = TempDbPath("csvzall_test_table.db");
  std::filesystem::remove(db_path);

  auto csv = tests::MakeTestCsv({"x"}, {{"1"}, {"2"}});
  std::istringstream input(csv);

  pipeline::RunOptions options;
  options.input_is_stdin = true;
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;

  int rc = pipeline::RunSqlExport("-", db_path, "mytable", input, options, logger, stats);

  REQUIRE(rc == 0);
  {
    SQLite::Database db(db_path, SQLite::OPEN_READONLY);
    SQLite::Statement q(db, "SELECT COUNT(*) FROM \"mytable\"");
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).getInt() == 2);
  }

  std::filesystem::remove(db_path);
}

TEST_CASE("SqlExport: RunOptions defaults journaling off") {
  pipeline::RunOptions options;
  REQUIRE(options.sqlite_journal_enabled == false);
}

TEST_CASE("SqlExport: explicit journaling opt-in still succeeds") {
  const std::string db_path = TempDbPath("csvzall_test_journal_mode.db");
  std::filesystem::remove(db_path);

  auto csv = tests::MakeTestCsv({"x"}, {{"1"}, {"2"}});
  std::istringstream input(csv);

  pipeline::RunOptions options;
  options.input_is_stdin = true;
  options.sqlite_journal_enabled = true;
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;

  const int rc = pipeline::RunSqlExport("-", db_path, "data", input, options, logger, stats);

  REQUIRE(rc == 0);
  REQUIRE(stats.rows_processed == 2);

  std::filesystem::remove(db_path);
}

TEST_CASE("SqlExport: refuses to overwrite existing file") {
  const std::string db_path = TempDbPath("csvzall_test_overwrite.db");
  // Create the file so it already exists.
  { std::ofstream f(db_path); f << "placeholder"; }

  auto csv = tests::MakeTestCsv({"x"}, {{"1"}});
  std::istringstream input(csv);

  std::string error_msg;
  pipeline::RunOptions options;
  options.input_is_stdin = true;
  pipeline::LoggerCallbacks logger{
    [&](const std::string& msg) { error_msg = msg; }, nullptr};
  pipeline::RunStats stats;

  int rc = pipeline::RunSqlExport("-", db_path, "data", input, options, logger, stats);

  REQUIRE(rc == 1);
  REQUIRE(!error_msg.empty());

  std::filesystem::remove(db_path);
}

TEST_CASE("SqlExport: stdin without dest returns error") {
  auto csv = tests::MakeTestCsv({"x"}, {{"1"}});
  std::istringstream input(csv);

  std::string error_msg;
  pipeline::RunOptions options;
  options.input_is_stdin = true;
  pipeline::LoggerCallbacks logger{
    [&](const std::string& msg) { error_msg = msg; }, nullptr};
  pipeline::RunStats stats;

  int rc = pipeline::RunSqlExport("-", "", "data", input, options, logger, stats);

  REQUIRE(rc == 1);
  REQUIRE(!error_msg.empty());
}

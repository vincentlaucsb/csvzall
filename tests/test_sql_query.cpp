#include <catch2/catch_test_macros.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

TEST_CASE("SqlQueryCsv: runs SELECT and streams result CSV") {
  auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {
          {"alice", "10"},
          {"bob", "20"},
          {"charlie", "5"},
      });

  std::istringstream input(csv);
  std::ostringstream output;

  pipeline::RunOptions options = tests::MakeTestOptions();
  pipeline::LoggerCallbacks logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  const int rc = pipeline::RunSqlQueryCsv(
      "SELECT name, value FROM \"data\" WHERE value > 7 ORDER BY value",
      "data",
      input,
      output,
      options,
      logger,
      stats);

  REQUIRE(rc == 0);
  REQUIRE(stats.rows_processed == 2);

  const auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 3);
  REQUIRE(rows[0] == std::vector<std::string>{"name", "value"});
  REQUIRE(rows[1] == std::vector<std::string>{"alice", "10"});
  REQUIRE(rows[2] == std::vector<std::string>{"bob", "20"});
}

TEST_CASE("SqlQueryCsv: empty SQL returns error") {
  auto csv = tests::MakeTestCsv({"x"}, {{"1"}});

  std::istringstream input(csv);
  std::ostringstream output;
  std::string error_msg;

  pipeline::RunOptions options = tests::MakeTestOptions();
  pipeline::LoggerCallbacks logger{
      [&](const std::string& msg) { error_msg = msg; }, nullptr};
  pipeline::RunStats stats;

  const int rc = pipeline::RunSqlQueryCsv("   ", "data", input, output, options, logger, stats);

  REQUIRE(rc == 1);
  REQUIRE(!error_msg.empty());
}

TEST_CASE("SqlQueryCsv: non-result statement returns error") {
  auto csv = tests::MakeTestCsv({"x"}, {{"1"}});

  std::istringstream input(csv);
  std::ostringstream output;
  std::string error_msg;

  pipeline::RunOptions options = tests::MakeTestOptions();
  pipeline::LoggerCallbacks logger{
      [&](const std::string& msg) { error_msg = msg; }, nullptr};
  pipeline::RunStats stats;

  const int rc = pipeline::RunSqlQueryCsv(
      "UPDATE \"data\" SET x = 2",
      "data",
      input,
      output,
      options,
      logger,
      stats);

  REQUIRE(rc == 1);
  REQUIRE(!error_msg.empty());
}

  TEST_CASE("SqlQuery: auto-detects input type from file extension") {
    REQUIRE(pipeline::DetectSqlQueryInputKind("report.csv") ==
      pipeline::SqlQueryInputKind::kCsv);
    REQUIRE(pipeline::DetectSqlQueryInputKind("report.TXT") ==
      pipeline::SqlQueryInputKind::kCsv);

    REQUIRE(pipeline::DetectSqlQueryInputKind("warehouse.db") ==
      pipeline::SqlQueryInputKind::kSqlite);
    REQUIRE(pipeline::DetectSqlQueryInputKind("warehouse.sqlite") ==
      pipeline::SqlQueryInputKind::kSqlite);
    REQUIRE(pipeline::DetectSqlQueryInputKind("warehouse.SQLITE3") ==
      pipeline::SqlQueryInputKind::kSqlite);

    REQUIRE(pipeline::DetectSqlQueryInputKind("-") ==
      pipeline::SqlQueryInputKind::kUnknown);
    REQUIRE(pipeline::DetectSqlQueryInputKind("no_extension") ==
      pipeline::SqlQueryInputKind::kUnknown);
  }

  TEST_CASE("SqlQueryDb: runs SELECT against existing SQLite file") {
    const std::string db_path =
        (std::filesystem::temp_directory_path() / "csvzall_sql_query_db_test.sqlite").string();
    std::filesystem::remove(db_path);

    {
      SQLite::Database db(db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
      db.exec("CREATE TABLE data (name TEXT, value NUMERIC)");
      db.exec("INSERT INTO data (name, value) VALUES ('alice', 10)");
      db.exec("INSERT INTO data (name, value) VALUES ('bob', 20)");
    }

    std::ostringstream output;
    pipeline::RunStats stats;

    const int rc = pipeline::RunSqlQueryDb(
        "SELECT name FROM data WHERE value > 10 ORDER BY value",
        db_path,
        output,
        tests::MakeNullLogger(),
        stats);

    REQUIRE(rc == 0);
    REQUIRE(stats.rows_processed == 1);

    const auto rows = tests::ParseCsv(output.str());
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0] == std::vector<std::string>{"name"});
    REQUIRE(rows[1] == std::vector<std::string>{"bob"});

    std::filesystem::remove(db_path);
  }

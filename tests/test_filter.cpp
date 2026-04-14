#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

TEST_CASE("Filter: basic numeric comparison") {
  auto csv = tests::MakeTestCsv(
    {"name", "value"},
    {
      {"alice", "10"},
      {"bob", "20"},
      {"charlie", "5"}
    }
  );

  std::istringstream input(csv);
  std::ostringstream output;

  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  int rc = pipeline::RunFilter("value > 10", input, output, options, logger, stats);

  REQUIRE(rc == 0);

  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);  // header + 1 data row
  REQUIRE(rows[1][0] == "bob");
  REQUIRE(rows[1][1] == "20");
}

TEST_CASE("Filter: case-insensitive column matching") {
  // SQLite resolves identifiers case-insensitively, so 'value' matches 'Value'.
  auto csv = tests::MakeTestCsv(
    {"Name", "Value"},
    {
      {"alice", "10"},
      {"bob", "20"}
    }
  );

  std::istringstream input(csv);
  std::ostringstream output;

  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  int rc = pipeline::RunFilter("value > 10", input, output, options, logger, stats);

  REQUIRE(rc == 0);
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);  // header + 1 data row
}

TEST_CASE("Filter: wrong-case column reference succeeds (SQLite is case-insensitive)") {
  // SQLite identifier resolution is inherently case-insensitive; --exact does
  // not change this for raw WHERE clauses.
  auto csv = tests::MakeTestCsv(
    {"Name", "Value"},
    {
      {"alice", "10"},
      {"bob", "20"}
    }
  );

  std::istringstream input(csv);
  std::ostringstream output;

  auto options = tests::MakeTestOptions(true);  // exact flag set but SQLite ignores case
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  int rc = pipeline::RunFilter("value > 10", input, output, options, logger, stats);

  REQUIRE(rc == 0);
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);  // header + 1 data row
}

TEST_CASE("Filter: SQL AND operator") {
  auto csv = tests::MakeTestCsv(
    {"value", "status"},
    {
      {"10", "active"},
      {"20", "active"},
      {"5", "inactive"},
      {"30", "inactive"}
    }
  );

  std::istringstream input(csv);
  std::ostringstream output;

  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  int rc = pipeline::RunFilter("value > 10 AND status = 'active'", input, output, options, logger, stats);

  REQUIRE(rc == 0);
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);  // header + 1 data row (bob: value=20, status=active)
  REQUIRE(rows[1][1] == "active");
}

TEST_CASE("Filter: invalid SQL returns error") {
  auto csv = tests::MakeTestCsv(
    {"name", "value"},
    {{"alice", "10"}}
  );

  std::istringstream input(csv);
  std::ostringstream output;

  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  int rc = pipeline::RunFilter("value >>>> @@@ INVALID", input, output, options, logger, stats);

  REQUIRE(rc == 1);
}

TEST_CASE("Filter: empty expression returns error") {
  auto csv = tests::MakeTestCsv(
    {"name", "value"},
    {{"alice", "10"}}
  );

  std::istringstream input(csv);
  std::ostringstream output;

  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  int rc = pipeline::RunFilter("", input, output, options, logger, stats);

  REQUIRE(rc == 1);
}

TEST_CASE("Filter: string LIKE pattern") {
  auto csv = tests::MakeTestCsv(
    {"name", "value"},
    {
      {"alice", "10"},
      {"alex", "20"},
      {"bob", "30"}
    }
  );

  std::istringstream input(csv);
  std::ostringstream output;

  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  int rc = pipeline::RunFilter("name LIKE 'al%'", input, output, options, logger, stats);

  REQUIRE(rc == 0);
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 3);  // header + alice + alex
}

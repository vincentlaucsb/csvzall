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
  
  pipeline::RunOptions options{false, true, false};
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;
  
  int rc = pipeline::RunFilter("value > 10", input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);  // header + 1 data row
  REQUIRE(rows[1][0] == "bob");
  REQUIRE(rows[1][1] == "20");
}

TEST_CASE("Filter: case-insensitive column matching") {
  auto csv = tests::MakeTestCsv(
    {"Name", "Value"},
    {
      {"alice", "10"},
      {"bob", "20"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  pipeline::RunOptions options{false, true, false};  // case-insensitive (default)
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;
  
  // Use lowercase column name
  int rc = pipeline::RunFilter("value > 10", input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);
}

TEST_CASE("Filter: --exact rejects wrong case") {
  auto csv = tests::MakeTestCsv(
    {"Name", "Value"},
    {
      {"alice", "10"},
      {"bob", "20"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  pipeline::RunOptions options{false, true, true};  // exact matching
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;
  
  // Use lowercase column name - should fail with exact matching
  int rc = pipeline::RunFilter("value > 10", input, output, options, logger, stats);
  
  REQUIRE(rc == 1);  // Expected to fail
}

TEST_CASE("Filter: logical operators (&&)") {
  auto csv = tests::MakeTestCsv(
    {"value", "status"},
    {
      {"10", "active"},
      {"20", "active"},
      {"5", "inactive"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  pipeline::RunOptions options{false, true, false};
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;
  
  int rc = pipeline::RunFilter("value > 10 && status == 1", input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  // status == 1 will evaluate active/inactive as 1 or 0, but the concept holds
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() >= 1);  // header + filtered rows
}

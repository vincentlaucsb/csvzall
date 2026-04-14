#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

TEST_CASE("Summarize: group by and max") {
  auto csv = tests::MakeTestCsv(
    {"exercise", "weight"},
    {
      {"bench", "100"},
      {"bench", "110"},
      {"squat", "150"},
      {"squat", "140"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  int rc = pipeline::RunSummarize("exercise", "weight", {}, input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 3);  // header + 2 groups
  
  // Find bench and squat rows
  bool found_bench = false;
  bool found_squat = false;
  for (std::size_t i = 1; i < rows.size(); ++i) {
    if (rows[i][0] == "bench") {
      found_bench = true;
      REQUIRE(rows[i][1] == "110");  // max weight for bench
    }
    if (rows[i][0] == "squat") {
      found_squat = true;
      REQUIRE(rows[i][1] == "150");  // max weight for squat
    }
  }
  REQUIRE(found_bench);
  REQUIRE(found_squat);
}

TEST_CASE("Summarize: with --show columns") {
  auto csv = tests::MakeTestCsv(
    {"exercise", "date", "weight"},
    {
      {"bench", "2026-01-01", "100"},
      {"bench", "2026-02-01", "110"},
      {"squat", "2026-01-01", "150"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  int rc = pipeline::RunSummarize("exercise", "weight", {"date"}, input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 3);  // header + 2 groups
  REQUIRE(rows[0].size() == 3);  // exercise, date, max_weight
  
  // For bench, should show date from row with max weight (2026-02-01)
  for (std::size_t i = 1; i < rows.size(); ++i) {
    if (rows[i][0] == "bench") {
      REQUIRE(rows[i][1] == "2026-02-01");
      REQUIRE(rows[i][2] == "110");
    }
  }
}

TEST_CASE("Summarize: insertion order preserved for groups") {
  auto csv = tests::MakeTestCsv(
    {"exercise", "weight"},
    {
      {"squat", "100"},
      {"bench", "50"},
      {"squat", "110"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  int rc = pipeline::RunSummarize("exercise", "weight", {}, input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 3);
  
  // squat should appear first (inserted first)
  REQUIRE(rows[1][0] == "squat");
  REQUIRE(rows[2][0] == "bench");
}

TEST_CASE("Summarize: case-insensitive column matching") {
  auto csv = tests::MakeTestCsv(
    {"Exercise", "Weight"},
    {
      {"bench", "100"},
      {"bench", "110"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  // Use lowercase column names
  int rc = pipeline::RunSummarize("exercise", "weight", {}, input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);  // header + 1 group
}

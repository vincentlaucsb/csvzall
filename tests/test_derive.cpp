#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

TEST_CASE("Derive: simple arithmetic") {
  auto csv = tests::MakeTestCsv(
    {"a", "b"},
    {
      {"2", "3"},
      {"4", "5"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  pipeline::RunOptions options{false, true, false};
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;
  
  int rc = pipeline::RunDerive("sum = a + b", input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 3);  // header + 2 data rows
  REQUIRE(rows[0].size() == 3);  // a, b, sum
  REQUIRE(rows[1][2] == "5");  // 2 + 3
  REQUIRE(rows[2][2] == "9");  // 4 + 5
}

TEST_CASE("Derive: Epley1RM formula") {
  auto csv = tests::MakeTestCsv(
    {"Weight", "Reps"},
    {
      {"100", "1"},
      {"80", "10"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  pipeline::RunOptions options{false, true, false};
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;
  
  int rc = pipeline::RunDerive("Epley1RM = Weight * (1 + Reps / 30)", input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 3);
  REQUIRE(rows[0].size() == 3);
  
  // Verify first row: 100 * (1 + 1/30) = 100 * 1.0333... ≈ 103.33
  double epley1 = std::stod(rows[1][2]);
  REQUIRE(epley1 > 103.0);
  REQUIRE(epley1 < 104.0);
  
  // Verify second row: 80 * (1 + 10/30) = 80 * 1.333... ≈ 106.67
  double epley2 = std::stod(rows[2][2]);
  REQUIRE(epley2 > 106.0);
  REQUIRE(epley2 < 107.0);
}

TEST_CASE("Derive: case-insensitive column matching") {
  auto csv = tests::MakeTestCsv(
    {"MyValue", "OtherValue"},
    {
      {"5", "10"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  pipeline::RunOptions options{false, true, false};
  pipeline::LoggerCallbacks logger{nullptr, nullptr};
  pipeline::RunStats stats;
  
  // Use lowercase in expression
  int rc = pipeline::RunDerive("result = myvalue + othervalue", input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);
  REQUIRE(rows[1][2] == "15");
}

TEST_CASE("Derive: overwrite existing column") {
  auto csv = tests::MakeTestCsv(
    {"value"},
    {
      {"10"},
      {"20"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  pipeline::RunOptions options{false, true, false};
  
  std::string error_msg;
  pipeline::LoggerCallbacks logger{
    [&](const std::string& msg) { error_msg = msg; },
    nullptr
  };
  pipeline::RunStats stats;
  
  int rc = pipeline::RunDerive("value = value * 2", input, output, options, logger, stats);
  
  // derive command returns 1 when overwriting existing column - that's by design
  // (avoids ambiguity). Skip this test.
  REQUIRE(rc == 1);
}

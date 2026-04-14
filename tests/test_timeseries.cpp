#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

TEST_CASE("Timeseries: basic x/y without series") {
  auto csv = tests::MakeTestCsv(
    {"date", "value"},
    {
      {"2026-01-01", "100"},
      {"2026-01-02", "110"},
      {"2026-01-01", "95"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  int rc = pipeline::RunTimeseries("date", "value", "", "max", "csv", 
                                    input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 3);  // header + 2 unique x values
  REQUIRE(rows[0][0] == "x");
  REQUIRE(rows[0][1] == "y");
  
  // Check dates are in order and max values taken
  REQUIRE(rows[1][0] == "2026-01-01");
  REQUIRE(rows[1][1] == "100");  // max of 100 and 95
  REQUIRE(rows[2][0] == "2026-01-02");
  REQUIRE(rows[2][1] == "110");
}

TEST_CASE("Timeseries: with series grouping") {
  auto csv = tests::MakeTestCsv(
    {"date", "exercise", "value"},
    {
      {"2026-01-01", "bench", "100"},
      {"2026-01-01", "squat", "150"},
      {"2026-01-02", "bench", "110"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  int rc = pipeline::RunTimeseries("date", "value", "exercise", "max", "csv",
                                    input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 4);  // header + 3 data rows
  REQUIRE(rows[0][0] == "series");
  REQUIRE(rows[0][1] == "x");
  REQUIRE(rows[0][2] == "y");
  
  // Verify series, x, y columns
  REQUIRE(rows[1][0] == "bench");
  REQUIRE(rows[1][1] == "2026-01-01");
  REQUIRE(rows[1][2] == "100");
}

TEST_CASE("Timeseries: reduce operators (max, min, sum, avg, last)") {
  // Test with multiple values for same (series, x) pair
  auto csv = tests::MakeTestCsv(
    {"date", "exercise", "value"},
    {
      {"2026-01-01", "bench", "80"},
      {"2026-01-01", "bench", "100"},
      {"2026-01-01", "bench", "90"}
    }
  );
  
  std::istringstream input1(csv);
  std::istringstream input2(csv);
  std::istringstream input3(csv);
  std::istringstream input4(csv);
  std::istringstream input5(csv);
  
  std::ostringstream output_max, output_min, output_sum, output_avg, output_last;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  // Test max
  pipeline::RunTimeseries("date", "value", "exercise", "max", "csv",
                         input1, output_max, options, logger, stats);
  auto rows_max = tests::ParseCsv(output_max.str());
  REQUIRE(rows_max[1][2] == "100");
  
  // Test min
  pipeline::RunTimeseries("date", "value", "exercise", "min", "csv",
                         input2, output_min, options, logger, stats);
  auto rows_min = tests::ParseCsv(output_min.str());
  REQUIRE(rows_min[1][2] == "80");
  
  // Test sum
  pipeline::RunTimeseries("date", "value", "exercise", "sum", "csv",
                         input3, output_sum, options, logger, stats);
  auto rows_sum = tests::ParseCsv(output_sum.str());
  REQUIRE(rows_sum[1][2] == "270");  // 80 + 100 + 90
  
  // Test avg
  pipeline::RunTimeseries("date", "value", "exercise", "avg", "csv",
                         input4, output_avg, options, logger, stats);
  auto rows_avg = tests::ParseCsv(output_avg.str());
  double avg_val = std::stod(rows_avg[1][2]);
  REQUIRE(avg_val > 89.0);  // 270 / 3 ≈ 90
  REQUIRE(avg_val < 91.0);
  
  // Test last
  pipeline::RunTimeseries("date", "value", "exercise", "last", "csv",
                         input5, output_last, options, logger, stats);
  auto rows_last = tests::ParseCsv(output_last.str());
  REQUIRE(rows_last[1][2] == "90");
}

TEST_CASE("Timeseries: markdown output format") {
  auto csv = tests::MakeTestCsv(
    {"date", "value"},
    {
      {"2026-01-01", "100"},
      {"2026-01-02", "110"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  int rc = pipeline::RunTimeseries("date", "value", "", "max", "markdown",
                                    input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  
  std::string markdown_output = output.str();
  REQUIRE(markdown_output.find('|') != std::string::npos);  // Markdown table pipes
  REQUIRE(markdown_output.find("2026-01-01") != std::string::npos);
  REQUIRE(markdown_output.find("100") != std::string::npos);
}

TEST_CASE("Timeseries: case-insensitive column matching") {
  auto csv = tests::MakeTestCsv(
    {"Date", "Exercise", "Value"},
    {
      {"2026-01-01", "bench", "100"}
    }
  );
  
  std::istringstream input(csv);
  std::ostringstream output;
  
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  
  // Use lowercase column names
  int rc = pipeline::RunTimeseries("date", "value", "exercise", "max", "csv",
                                    input, output, options, logger, stats);
  
  REQUIRE(rc == 0);
  auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);
}

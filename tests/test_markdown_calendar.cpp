#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "../src/pipeline/common/markdown_calendar.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

TEST_CASE("Markdown calendar: renders a single month from fixed date content CSV") {
  const auto csv = tests::MakeTestCsv(
      {"date", "content"},
      {{"2026-05-01", "Done"}, {"2026-05-03", "Skipped"}});
  std::istringstream input(csv);
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  const int rc = pipeline::common::RenderMarkdownCalendarCsv(
      input, output, options, logger, stats,
      {.start_date = "2026-05-01", .end_date = "2026-05-03"});

  REQUIRE(rc == 0);
  REQUIRE(output.str() ==
          "### May 2026\n\n"
          "| Sun | Mon | Tue | Wed | Thu | Fri | Sat |\n"
          "| --- | --- | --- | --- | --- | --- | --- |\n"
          "|  |  |  |  |  | 1<br>Done | 2 |\n"
          "| 3<br>Skipped |  |  |  |  |  |  |\n"
          "|  |  |  |  |  |  |  |\n"
          "|  |  |  |  |  |  |  |\n"
          "|  |  |  |  |  |  |  |\n"
          "|  |  |  |  |  |  |  |\n");
}

TEST_CASE("Markdown calendar: renders multiple months with custom headers") {
  const auto csv = tests::MakeTestCsv(
      {"date", "content"},
      {{"2026-05-31", "May"}, {"2026-06-01", "June"}});
  std::istringstream input(csv);
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  const int rc = pipeline::common::RenderMarkdownCalendarCsv(
      input, output, options, logger, stats,
      {.start_date = "2026-05-31",
       .end_date = "2026-06-01",
       .month_header = [](int year, unsigned month) {
         return std::to_string(year) + "/" + std::to_string(month);
       }});

  REQUIRE(rc == 0);
  REQUIRE(output.str().find("### 2026/5") != std::string::npos);
  REQUIRE(output.str().find("### 2026/6") != std::string::npos);
  REQUIRE(output.str().find("31<br>May") != std::string::npos);
  REQUIRE(output.str().find("1<br>June") != std::string::npos);
}

TEST_CASE("Markdown calendar: rejects duplicate dates") {
  const auto csv = tests::MakeTestCsv(
      {"date", "content"},
      {{"2026-05-01", "Done"}, {"2026-05-01", "Again"}});
  std::istringstream input(csv);
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;

  const int rc = pipeline::common::RenderMarkdownCalendarCsv(
      input, output, options, logger, stats,
      {.start_date = "2026-05-01", .end_date = "2026-05-31"});

  REQUIRE(rc == 1);
}

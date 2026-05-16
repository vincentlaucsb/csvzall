#include <catch2/catch_test_macros.hpp>

#include "../src/transform_pipeline.hpp"

#include <sstream>
#include <string>

namespace {

csvzall::pipeline::LoggerCallbacks SilentLogger() {
  return {};
}

}  // namespace

TEST_CASE("heatmap renders CSV date rows as an SVG calendar heatmap") {
  std::istringstream input(
      "date,count,content\n"
      "2026-01-01,1,Gym\n"
      "2026-01-01,2,Lift\n"
      "2026-01-08,1,Workout\n");
  std::ostringstream output;

  csvzall::pipeline::RunOptions options;
  options.input_is_stdin = true;
  csvzall::pipeline::RunStats stats;

  const auto rc = csvzall::pipeline::RunHeatmap(
      "date", "count", "content", "2026-01-01", "2026-01-14", "Gym Attendance",
      input, output, options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(stats.rows_processed == 3);

  const auto svg = output.str();
  CHECK(svg.find("<svg") != std::string::npos);
  CHECK(svg.find("Gym Attendance") != std::string::npos);
  CHECK(svg.find("class=\"heatmap-cell\"") != std::string::npos);
  CHECK(svg.find("<title>2026-01-01: 3 - Gym; Lift</title>") != std::string::npos);
}

TEST_CASE("heatmap counts rows when no value column is provided") {
  std::istringstream input(
      "date,content\n"
      "2026-01-01,Gym\n"
      "2026-01-01,Lift\n");
  std::ostringstream output;

  csvzall::pipeline::RunOptions options;
  options.input_is_stdin = true;
  csvzall::pipeline::RunStats stats;

  const auto rc = csvzall::pipeline::RunHeatmap(
      "date", "", "content", "2026-01-01", "2026-01-07", "",
      input, output, options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(output.str().find("<title>2026-01-01: 2 - Gym; Lift</title>") != std::string::npos);
}

TEST_CASE("heatmap reports missing date columns") {
  std::istringstream input("day,count\n2026-01-01,1\n");
  std::ostringstream output;
  std::string error;

  csvzall::pipeline::RunOptions options;
  options.input_is_stdin = true;
  csvzall::pipeline::RunStats stats;
  csvzall::pipeline::LoggerCallbacks logger;
  logger.error = [&error](const std::string& message) { error = message; };

  const auto rc = csvzall::pipeline::RunHeatmap(
      "date", "count", "", "2026-01-01", "2026-01-07", "",
      input, output, options, logger, stats);

  CHECK(rc == 1);
  CHECK(error.find("date column not found") != std::string::npos);
}

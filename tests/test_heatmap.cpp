#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "../src/transform_pipeline.hpp"
#include "../src/pipeline/common/chart_spec.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

csvzall::pipeline::LoggerCallbacks SilentLogger() {
  return {};
}

std::filesystem::path TempDir(const std::string& name) {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              (name + "_" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  REQUIRE(output.is_open());
  output << text;
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input.is_open());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

struct CurrentPathGuard {
  std::filesystem::path previous = std::filesystem::current_path();
  explicit CurrentPathGuard(const std::filesystem::path& next) {
    std::filesystem::current_path(next);
  }
  ~CurrentPathGuard() {
    std::filesystem::current_path(previous);
  }
};

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

TEST_CASE("heatmap accepts rolling lookback ranges with an explicit end date") {
  std::istringstream input(
      "date,content\n"
      "2025-01-01,Old\n"
      "2026-01-01,Gym\n");
  std::ostringstream output;

  csvzall::pipeline::common::HeatmapSpec spec;
  spec.date_column = "date";
  spec.label_column = "content";
  spec.lookback = "1y";
  spec.end_date = "2026-01-01";
  spec.title = "Rolling Gym";

  csvzall::pipeline::RunOptions options;
  options.input_is_stdin = true;
  csvzall::pipeline::RunStats stats;

  const auto rc = csvzall::pipeline::RunHeatmap(
      spec, input, output, options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(output.str().find("Rolling Gym") != std::string::npos);
  CHECK(output.str().find("<title>2026-01-01: 1 - Gym</title>") != std::string::npos);
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

TEST_CASE("charts config accepts heatmap lookback instead of fixed start and end") {
  const auto root = TempDir("csvzall_chart_lookback");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"gym","type":"heatmap","input":"gym.csv","output":"gym.svg","options":{"date":"date","lookback":"365d","title":"Gym"}}]})");

  const auto config = csvzall::pipeline::common::LoadChartConfig(root / "config.json");
  REQUIRE(config.charts.size() == 1);
  CHECK(config.charts.front().heatmap.lookback == "365d");
  CHECK(config.charts.front().heatmap.start_date.empty());
  CHECK(config.charts.front().heatmap.end_date.empty());
}

TEST_CASE("heatmap chart specs from direct args and JSON config match") {
  const auto root = TempDir("csvzall_chart_spec_match");
  WriteText(root / ".csvzall" / "charts.json",
            R"({
  "charts": [
    {
      "id": "gym-attendance-heatmap",
      "type": "heatmap",
      "input": "Exercise/output/gym_attendance.csv",
      "output": "Exercise/output/gym_attendance_heatmap.svg",
      "options": {
        "date": "attendance_date",
        "start": "2025-05-17",
        "end": "2026-05-17",
        "title": "Gym Attendance"
      },
      "runOnSave": true
    }
  ]
})");

  csvzall::pipeline::common::HeatmapSpec heatmap;
  heatmap.date_column = "attendance_date";
  heatmap.start_date = "2025-05-17";
  heatmap.end_date = "2026-05-17";
  heatmap.title = "Gym Attendance";
  const auto direct = csvzall::pipeline::common::MakeHeatmapChartSpec(
      "gym-attendance-heatmap",
      root / "Exercise" / "output" / "gym_attendance.csv",
      root / "Exercise" / "output" / "gym_attendance_heatmap.svg",
      true,
      heatmap);

  const auto config = csvzall::pipeline::common::LoadChartConfig(root / ".csvzall" / "charts.json");
  REQUIRE(config.charts.size() == 1);
  const auto& parsed = config.charts.front();

  CHECK(parsed.id == direct.id);
  CHECK(parsed.type == direct.type);
  CHECK(parsed.input == direct.input);
  REQUIRE(parsed.output);
  REQUIRE(direct.output);
  CHECK(*parsed.output == *direct.output);
  CHECK(parsed.run_on_save == direct.run_on_save);
  CHECK(parsed.heatmap.date_column == direct.heatmap.date_column);
  CHECK(parsed.heatmap.value_column == direct.heatmap.value_column);
  CHECK(parsed.heatmap.label_column == direct.heatmap.label_column);
  CHECK(parsed.heatmap.start_date == direct.heatmap.start_date);
  CHECK(parsed.heatmap.end_date == direct.heatmap.end_date);
  CHECK(parsed.heatmap.lookback == direct.heatmap.lookback);
  CHECK(parsed.heatmap.title == direct.heatmap.title);
}

TEST_CASE("charts config root resolution handles default and explicit paths") {
  const auto root = TempDir("csvzall_chart_root");
  CHECK(csvzall::pipeline::common::ResolveChartConfigRoot(root / ".csvzall" / "charts.json") ==
        root);
  CHECK(csvzall::pipeline::common::ResolveChartConfigRoot(root / "charts.json") == root);
}

TEST_CASE("charts run loads default config and writes configured heatmap output") {
  const auto root = TempDir("csvzall_charts_default_run");
  CurrentPathGuard cwd(root);
  WriteText(root / "data" / "gym.csv",
            "date,count,content\n"
            "2026-01-01,1,Gym\n"
            "2026-01-01,2,Lift\n");
  WriteText(root / ".csvzall" / "charts.json",
            R"({"charts":[{"id":"gym","type":"heatmap","input":"data/gym.csv","output":"charts/gym.svg","options":{"date":"date","value":"count","label":"content","start":"2026-01-01","end":"2026-01-07","title":"Gym"}}]})");

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto rc = csvzall::pipeline::RunCharts("", "", options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(stats.rows_processed == 2);
  const auto svg = ReadText(root / "charts" / "gym.svg");
  CHECK(svg.find("<svg") != std::string::npos);
  CHECK(svg.find("Gym") != std::string::npos);
  CHECK(svg.find("2026-01-01: 3") != std::string::npos);
}

TEST_CASE("charts run explicit config overwrites output and can select one chart") {
  const auto root = TempDir("csvzall_charts_explicit_run");
  WriteText(root / "data.csv",
            "date,count\n"
            "2026-01-01,1\n");
  WriteText(root / "stale.svg", "stale");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"first","type":"heatmap","input":"data.csv","output":"stale.svg","options":{"date":"date","value":"count","start":"2026-01-01","end":"2026-01-07"}},{"id":"second","type":"heatmap","input":"data.csv","output":"second.svg","options":{"date":"date","value":"count","start":"2026-01-01","end":"2026-01-07"}}]})");

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto rc = csvzall::pipeline::RunCharts((root / "config.json").string(),
                                               "first", options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(ReadText(root / "stale.svg").find("<svg") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(root / "second.svg"));
}

TEST_CASE("charts run renders multiple configured outputs") {
  const auto root = TempDir("csvzall_charts_multiple");
  WriteText(root / "data.csv",
            "date,count\n"
            "2026-01-01,1\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"first","type":"heatmap","input":"data.csv","output":"first.svg","options":{"date":"date","value":"count","start":"2026-01-01","end":"2026-01-07"}},{"id":"second","type":"heatmap","input":"data.csv","output":"second.svg","options":{"date":"date","value":"count","start":"2026-01-01","end":"2026-01-07"}}]})");

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto rc = csvzall::pipeline::RunCharts((root / "config.json").string(),
                                               "", options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(ReadText(root / "first.svg").find("<svg") != std::string::npos);
  CHECK(ReadText(root / "second.svg").find("<svg") != std::string::npos);
}

TEST_CASE("charts run missing default config fails clearly") {
  const auto root = TempDir("csvzall_charts_missing_default");
  CurrentPathGuard cwd(root);
  std::string error;
  csvzall::pipeline::LoggerCallbacks logger;
  logger.error = [&error](const std::string& message) { error = message; };

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  CHECK(csvzall::pipeline::RunCharts("", "", options, logger, stats) == 1);
  CHECK(error.find("config file not found") != std::string::npos);
  CHECK(error.find(".csvzall") != std::string::npos);
}

TEST_CASE("charts run reports missing input and missing date columns clearly") {
  const auto root = TempDir("csvzall_charts_errors");
  WriteText(root / "bad.csv", "day,count\n2026-01-01,1\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"missing-input","type":"heatmap","input":"missing.csv","output":"missing.svg","options":{"date":"date","start":"2026-01-01","end":"2026-01-07"}},{"id":"missing-column","type":"heatmap","input":"bad.csv","output":"bad.svg","options":{"date":"date","start":"2026-01-01","end":"2026-01-07"}}]})");

  std::string error;
  csvzall::pipeline::LoggerCallbacks logger;
  logger.error = [&error](const std::string& message) { error = message; };

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  CHECK(csvzall::pipeline::RunCharts((root / "config.json").string(),
                                     "missing-input", options, logger, stats) == 1);
  CHECK(error.find("missing input file") != std::string::npos);

  error.clear();
  CHECK(csvzall::pipeline::RunCharts((root / "config.json").string(),
                                     "missing-column", options, logger, stats) == 1);
  CHECK(error.find("date column not found") != std::string::npos);
}

TEST_CASE("charts config rejects unknown chart types and options") {
  const auto root = TempDir("csvzall_charts_unknowns");
  WriteText(root / "unknown-type.json",
            R"({"charts":[{"id":"x","type":"pie","input":"data.csv","output":"x.svg","options":{}}]})");
  WriteText(root / "unknown-option.json",
            R"({"charts":[{"id":"x","type":"heatmap","input":"data.csv","output":"x.svg","options":{"date":"date","start":"2026-01-01","end":"2026-01-07","bogus":true}}]})");

  CHECK_THROWS_WITH(csvzall::pipeline::common::LoadChartConfig(root / "unknown-type.json"),
                    Catch::Matchers::ContainsSubstring("unknown chart type"));
  CHECK_THROWS_WITH(csvzall::pipeline::common::LoadChartConfig(root / "unknown-option.json"),
                    Catch::Matchers::ContainsSubstring("unknown key 'bogus'"));
}

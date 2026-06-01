#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "../src/transform_pipeline.hpp"
#include "../src/charts/csv_chart.hpp"
#include "../src/pipeline/common/chart_spec.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

struct ChartReader {
  std::istringstream input;
  csv::CSVReader reader;
  std::vector<std::string> headers;

  explicit ChartReader(std::string text)
      : input(std::move(text)),
        reader(input, csv::CSVFormat().delimiter({',', '|', '\t', ';', '^'}).quote('"').header_row(0)),
        headers(reader.get_col_names()) {}
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
  CHECK(svg.find("class=\"heatmap-cell") != std::string::npos);
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

TEST_CASE("heatmap renders multiple value columns as categorical days") {
  std::istringstream input(
      "date,gym,bike,content\n"
      "2026-01-01,1,0,Squat\n"
      "2026-01-02,0,1,Ride\n"
      "2026-01-03,1,1,Brick\n");
  std::ostringstream output;

  csvzall::pipeline::common::HeatmapSpec spec;
  spec.date_column = "date";
  spec.values = {
      {"gym", "Gym", ""},
      {"bike", "Bike", ""},
  };
  spec.label_column = "content";
  spec.start_date = "2026-01-01";
  spec.end_date = "2026-01-07";
  spec.title = "Training";
  spec.orientation = "months-vertical";

  csvzall::pipeline::RunOptions options;
  options.input_is_stdin = true;
  csvzall::pipeline::RunStats stats;

  const auto rc = csvzall::pipeline::RunHeatmap(
      spec, input, output, options, SilentLogger(), stats);

  CHECK(rc == 0);
  const auto svg = output.str();
  CHECK(svg.find("text-anchor=\"end\" x=\"133.0\" y=\"49.0\">Thu</text>") !=
        std::string::npos);
  CHECK(svg.find(">Gym</text>") != std::string::npos);
  CHECK(svg.find(">Bike</text>") != std::string::npos);
  CHECK(svg.find("class=\"heatmap-cell-multi-value") != std::string::npos);
  CHECK(svg.find("<title>2026-01-03: Gym + Bike - Brick</title>") != std::string::npos);
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

TEST_CASE("charts config accepts multi-value chart options") {
  const auto root = TempDir("csvzall_chart_values_config");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"training","type":"heatmap","input":"training.csv","output":"training.svg","options":{"date":"date","values":[{"column":"gym","label":"Gym"},{"column":"bike","label":"Bike"}],"lookback":"365d","orientation":"months-vertical","title":"Training"}}]})");

  const auto config = csvzall::pipeline::common::LoadChartConfig(root / "config.json");
  REQUIRE(config.charts.size() == 1);
  CHECK(config.charts.front().heatmap.values.size() == 2);
  CHECK(config.charts.front().heatmap.values[0].column == "gym");
  CHECK(config.charts.front().heatmap.values[0].label == "Gym");
  CHECK(config.charts.front().heatmap.values[1].column == "bike");
  CHECK(config.charts.front().heatmap.values[1].label == "Bike");
  CHECK(config.charts.front().heatmap.orientation == "months-vertical");
}

TEST_CASE("charts config accepts grouped bar presentation") {
  const auto root = TempDir("csvzall_chart_bar_presentation_config");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"training","type":"bar","input":"training.csv","output":"training.svg","options":{"label":"date","values":[{"column":"gym","label":"Gym"},{"column":"bike","label":"Bike"}],"presentation":"grouped","title":"Training"}}]})");

  const auto config = csvzall::pipeline::common::LoadChartConfig(root / "config.json");
  REQUIRE(config.charts.size() == 1);
  CHECK(config.charts.front().bar.values.size() == 2);
  CHECK(config.charts.front().bar.presentation == "grouped");
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
  CHECK(parsed.heatmap.orientation == direct.heatmap.orientation);
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
  const auto rc = csvzall::pipeline::RunCharts("", "", false, options, SilentLogger(), stats);

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
                                               "first", false, options, SilentLogger(), stats);

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
                                               "", false, options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(ReadText(root / "first.svg").find("<svg") != std::string::npos);
  CHECK(ReadText(root / "second.svg").find("<svg") != std::string::npos);
}

TEST_CASE("charts run renders configured bar and line outputs") {
  const auto root = TempDir("csvzall_charts_bar_line");
  WriteText(root / "data.csv",
            "week,volume,series,session\n"
            "1,100,A,1\n"
            "2,125,A,2\n"
            "1,90,B,1\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"bar","type":"bar","input":"data.csv","output":"bar.svg","options":{"label":"week","value":"volume","title":"Volume"}},{"id":"line","type":"line","input":"data.csv","output":"line.svg","options":{"x":"session","y":"volume","series":"series","title":"Progress"}}]})");

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto rc = csvzall::pipeline::RunCharts((root / "config.json").string(),
                                               "", false, options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(ReadText(root / "bar.svg").find("class=\"bar") != std::string::npos);
  CHECK(ReadText(root / "line.svg").find("class=\"line-series") != std::string::npos);
}

TEST_CASE("charts run renders configured multi-value heatmap bar and line outputs") {
  const auto root = TempDir("csvzall_charts_multi_value");
  WriteText(root / "training.csv",
            "date,day,gym,bike\n"
            "2026-01-01,1,1,0\n"
            "2026-01-02,2,0,1\n"
            "2026-01-03,3,1,1\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"heatmap","type":"heatmap","input":"training.csv","output":"heatmap.svg","options":{"date":"date","values":[{"column":"gym","label":"Gym"},{"column":"bike","label":"Bike"}],"start":"2026-01-01","end":"2026-01-07"}},{"id":"bar","type":"bar","input":"training.csv","output":"bar.svg","options":{"label":"date","values":[{"column":"gym","label":"Gym"},{"column":"bike","label":"Bike"}]}},{"id":"grouped-bar","type":"bar","input":"training.csv","output":"grouped-bar.svg","options":{"label":"date","values":[{"column":"gym","label":"Gym"},{"column":"bike","label":"Bike"}],"presentation":"grouped"}},{"id":"line","type":"line","input":"training.csv","output":"line.svg","options":{"x":"day","values":[{"column":"gym","label":"Gym"},{"column":"bike","label":"Bike"}]}}]})");

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto rc = csvzall::pipeline::RunCharts((root / "config.json").string(),
                                               "", false, options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(ReadText(root / "heatmap.svg").find("Gym + Bike") != std::string::npos);
  CHECK(ReadText(root / "bar.svg").find("class=\"bar bar-segment") != std::string::npos);
  CHECK(ReadText(root / "grouped-bar.svg").find("class=\"bar bar-grouped") != std::string::npos);
  CHECK(ReadText(root / "line.svg").find(">Gym</text>") != std::string::npos);
  CHECK(ReadText(root / "line.svg").find(">Bike</text>") != std::string::npos);
}

TEST_CASE("charts run renders configured Markdown table output") {
  const auto root = TempDir("csvzall_charts_markdown_table");
  WriteText(root / "activity.csv",
            "completed_at,content,due_date\n"
            "2026-01-02T10:00:00Z,Gym|Lift,2026-01-02\n"
            "2026-01-15T10:00:00Z,Bike,2026-01-15\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"monthly","type":"markdown-table","input":"activity.csv","output":"Reports/generated/monthly.md","runOnSave":true,"options":{"sql":"SELECT substr(completed_at, 1, 7) AS month, COUNT(*) AS days FROM data GROUP BY month ORDER BY month"}}]})");

  const auto config = csvzall::pipeline::common::LoadChartConfig(root / "config.json");
  REQUIRE(config.charts.size() == 1);
  CHECK(config.charts.front().type == "markdown-table");
  CHECK(config.charts.front().markdown_table.sql.find("GROUP BY month") != std::string::npos);
  CHECK(config.charts.front().run_on_save);

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto rc = csvzall::pipeline::RunCharts((root / "config.json").string(),
                                               "monthly", false, options, SilentLogger(), stats);

  CHECK(rc == 0);
  CHECK(stats.rows_processed == 1);
  const auto markdown = ReadText(root / "Reports" / "generated" / "monthly.md");
  CHECK(markdown.find("| month   | days |") != std::string::npos);
  CHECK(markdown.find("| 2026-01 | 2    |") != std::string::npos);
}

TEST_CASE("charts run renders Markdown table column projection") {
  const auto root = TempDir("csvzall_charts_markdown_columns");
  WriteText(root / "activity.csv",
            "completed_at,content,due_date\n"
            "2026-01-02T10:00:00Z,Gym|Lift,2026-01-02\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"events","type":"markdown-table","input":"activity.csv","output":"events.md","options":{"columns":["content","due_date"]}}]})");

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto rc = csvzall::pipeline::RunCharts((root / "config.json").string(),
                                               "events", false, options, SilentLogger(), stats);

  CHECK(rc == 0);
  const auto markdown = ReadText(root / "events.md");
  CHECK(markdown.find("| content   | due_date   |") != std::string::npos);
  CHECK(markdown.find("Gym\\|Lift") != std::string::npos);
  CHECK(markdown.find("completed_at") == std::string::npos);
}

TEST_CASE("charts run missing default config fails clearly") {
  const auto root = TempDir("csvzall_charts_missing_default");
  CurrentPathGuard cwd(root);
  std::string error;
  csvzall::pipeline::LoggerCallbacks logger;
  logger.error = [&error](const std::string& message) { error = message; };

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  CHECK(csvzall::pipeline::RunCharts("", "", false, options, logger, stats) == 1);
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
                                     "missing-input", false, options, logger, stats) == 1);
  CHECK(error.find("missing input file") != std::string::npos);

  error.clear();
  CHECK(csvzall::pipeline::RunCharts((root / "config.json").string(),
                                     "missing-column", false, options, logger, stats) == 1);
  CHECK(error.find("date column not found") != std::string::npos);
}

TEST_CASE("charts validate checks config without writing outputs") {
  const auto root = TempDir("csvzall_charts_validate");
  WriteText(root / "data.csv",
            "date,count\n"
            "2026-01-01,1\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"valid","type":"heatmap","input":"data.csv","output":"valid.svg","options":{"date":"date","value":"count","start":"2026-01-01","end":"2026-01-07"}}]})");

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  CHECK(csvzall::pipeline::RunCharts((root / "config.json").string(),
                                     "valid", true, options, SilentLogger(), stats) == 0);
  CHECK_FALSE(std::filesystem::exists(root / "valid.svg"));
}

TEST_CASE("charts validate catches non-numeric line values") {
  const auto root = TempDir("csvzall_charts_validate_line");
  WriteText(root / "data.csv",
            "x,y\n"
            "1,nope\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"bad-line","type":"line","input":"data.csv","output":"line.svg","options":{"x":"x","y":"y"}}]})");
  std::string error;
  csvzall::pipeline::LoggerCallbacks logger;
  logger.error = [&error](const std::string& message) { error = message; };

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  CHECK(csvzall::pipeline::RunCharts((root / "config.json").string(),
                                     "bad-line", true, options, logger, stats) == 1);
  CHECK(error.find("non-numeric line y value") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(root / "line.svg"));
}

TEST_CASE("charts validate reports missing chart fields clearly") {
  const auto root = TempDir("csvzall_charts_validate_required_fields");
  WriteText(root / "data.csv",
            "label,value\n"
            "A,1\n");
  WriteText(root / "config.json",
            R"({"charts":[{"id":"bad-bar","type":"bar","input":"data.csv","output":"bar.svg","options":{"label":"","value":"value"}}]})");
  std::string error;
  csvzall::pipeline::LoggerCallbacks logger;
  logger.error = [&error](const std::string& message) { error = message; };

  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  CHECK(csvzall::pipeline::RunCharts((root / "config.json").string(),
                                     "bad-bar", true, options, logger, stats) == 1);
  CHECK(error.find("bar: label column is required") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(root / "bar.svg"));
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

TEST_CASE("CSV chart helpers reject invalid heatmap date range and orientation options") {
  using namespace csvzall::charts;

  HeatmapSpec spec;
  CHECK_THROWS_WITH(ResolveHeatmapDateRange(spec),
                    Catch::Matchers::ContainsSubstring("start and end dates are required"));

  spec.lookback = "  \t ";
  CHECK_THROWS_WITH(ResolveHeatmapDateRange(spec),
                    Catch::Matchers::ContainsSubstring("lookback is empty"));

  spec.lookback = "days";
  CHECK_THROWS_WITH(ResolveHeatmapDateRange(spec),
                    Catch::Matchers::ContainsSubstring("lookback must start"));

  spec.lookback = "0d";
  CHECK_THROWS_WITH(ResolveHeatmapDateRange(spec),
                    Catch::Matchers::ContainsSubstring("lookback must be positive"));

  spec.lookback = "1w";
  CHECK_THROWS_WITH(ResolveHeatmapDateRange(spec),
                    Catch::Matchers::ContainsSubstring("lookback unit"));

  spec.lookback = "1y";
  spec.end_date = "2026-02-30";
  CHECK_THROWS_WITH(ResolveHeatmapDateRange(spec),
                    Catch::Matchers::ContainsSubstring("invalid heatmap date"));

  spec.end_date = "2026-01-01";
  spec.start_date = "2025-01-01";
  CHECK_THROWS_WITH(ResolveHeatmapDateRange(spec),
                    Catch::Matchers::ContainsSubstring("lookback cannot be combined"));

  CHECK_THROWS_WITH(ParseHeatmapOrientation("diagonal"),
                    Catch::Matchers::ContainsSubstring("orientation must be"));
}

TEST_CASE("CSV heatmap helper reports all CSV conversion error paths") {
  using namespace csvzall::charts;
  CsvChartOptions options;

  {
    ChartReader data("date,count\n2026-01-01,1\n");
    HeatmapSpec spec;
    spec.date_column = "date";
    spec.value_column = "missing";
    spec.start_date = "2026-01-01";
    spec.end_date = "2026-01-07";
    CHECK_THROWS_WITH(RenderHeatmapCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("value column not found"));
  }
  {
    ChartReader data("date,count\n2026-01-01,1\n");
    HeatmapSpec spec;
    spec.date_column = "date";
    spec.label_column = "missing";
    spec.start_date = "2026-01-01";
    spec.end_date = "2026-01-07";
    CHECK_THROWS_WITH(RenderHeatmapCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("label column not found"));
  }
  {
    ChartReader data("date,count\n,1\n");
    HeatmapSpec spec;
    spec.date_column = "date";
    spec.start_date = "2026-01-01";
    spec.end_date = "2026-01-07";
    CHECK_THROWS_WITH(RenderHeatmapCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("empty date value"));
  }
  {
    ChartReader data("date,count\n2026-01-01,nope\n");
    HeatmapSpec spec;
    spec.date_column = "date";
    spec.value_column = "count";
    spec.start_date = "2026-01-01";
    spec.end_date = "2026-01-07";
    CHECK_THROWS_WITH(RenderHeatmapCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("non-numeric heatmap value"));
  }
  {
    ChartReader data("date,gym\n2026-01-01,nope\n");
    HeatmapSpec spec;
    spec.date_column = "date";
    spec.values = {{"gym", "Gym", ""}};
    spec.start_date = "2026-01-01";
    spec.end_date = "2026-01-07";
    CHECK_THROWS_WITH(RenderHeatmapCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("non-numeric heatmap value"));
  }
}

TEST_CASE("CSV bar and line helpers report all CSV conversion error paths") {
  using namespace csvzall::charts;
  CsvChartOptions options;

  CHECK_THROWS_WITH(IsGroupedBarPresentation("sideways"),
                    Catch::Matchers::ContainsSubstring("presentation must be"));

  {
    ChartReader data("label,value\nA,1\n");
    BarSpec spec;
    spec.label_column = "missing";
    spec.value_column = "value";
    CHECK_THROWS_WITH(RenderBarCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("label column not found"));
  }
  {
    ChartReader data("label,value\nA,1\n");
    BarSpec spec;
    spec.label_column = "label";
    CHECK_THROWS_WITH(RenderBarCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("value column is required"));
  }
  {
    ChartReader data("label,value\nA,1\n");
    BarSpec spec;
    spec.label_column = "label";
    spec.value_column = "missing";
    CHECK_THROWS_WITH(RenderBarCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("value column not found"));
  }
  {
    ChartReader data("label,a\nA,1\n");
    BarSpec spec;
    spec.label_column = "label";
    spec.values = {{"a", "A", ""}, {"missing", "Missing", ""}};
    CHECK_THROWS_WITH(RenderBarCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("value column not found"));
  }
  {
    ChartReader data("label,value\nA,nope\n");
    BarSpec spec;
    spec.label_column = "label";
    spec.value_column = "value";
    CHECK_THROWS_WITH(RenderBarCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("non-numeric bar value"));
  }
  {
    ChartReader data("x,y,series\n1,2,A\n");
    LineSpec spec;
    spec.x_column = "missing";
    spec.y_column = "y";
    CHECK_THROWS_WITH(RenderLineCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("x column not found"));
  }
  {
    ChartReader data("x,y,series\n1,2,A\n");
    LineSpec spec;
    spec.x_column = "x";
    CHECK_THROWS_WITH(RenderLineCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("y column is required"));
  }
  {
    ChartReader data("x,y,series\n1,2,A\n");
    LineSpec spec;
    spec.x_column = "x";
    spec.y_column = "missing";
    CHECK_THROWS_WITH(RenderLineCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("y column not found"));
  }
  {
    ChartReader data("x,y,series\n1,2,A\n");
    LineSpec spec;
    spec.x_column = "x";
    spec.y_column = "y";
    spec.series_column = "missing";
    CHECK_THROWS_WITH(RenderLineCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("series column not found"));
  }
  {
    ChartReader data("x,a\n1,2\n");
    LineSpec spec;
    spec.x_column = "x";
    spec.values = {{"a", "A", ""}, {"missing", "Missing", ""}};
    CHECK_THROWS_WITH(RenderLineCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("y column not found"));
  }
  {
    ChartReader data("x,y\nnope,2\n");
    LineSpec spec;
    spec.x_column = "x";
    spec.y_column = "y";
    CHECK_THROWS_WITH(RenderLineCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("non-numeric line x value"));
  }
  {
    ChartReader data("x,y\n1,nope\n");
    LineSpec spec;
    spec.x_column = "x";
    spec.y_column = "y";
    CHECK_THROWS_WITH(RenderLineCsv(data.reader, data.headers, spec, options),
                      Catch::Matchers::ContainsSubstring("non-numeric line y value"));
  }
}

TEST_CASE("charts config parser reports malformed config shapes") {
  const auto root = TempDir("csvzall_chart_config_errors");
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"top-level-unknown.json", R"({"charts":[],"extra":true})"},
      {"missing-charts.json", R"({})"},
      {"charts-not-array.json", R"({"charts":{}})"},
      {"chart-not-object.json", R"({"charts":[1]})"},
      {"missing-id.json", R"({"charts":[{"type":"heatmap","input":"data.csv","output":"x.svg","options":{"date":"date","start":"2026-01-01","end":"2026-01-07"}}]})"},
      {"type-not-string.json", R"({"charts":[{"id":"x","type":1,"input":"data.csv","output":"x.svg","options":{}}]})"},
      {"options-not-object.json", R"({"charts":[{"id":"x","type":"bar","input":"data.csv","output":"x.svg","options":1}]})"},
      {"values-not-array.json", R"({"charts":[{"id":"x","type":"bar","input":"data.csv","output":"x.svg","options":{"label":"label","values":1}}]})"},
      {"values-missing-column.json", R"({"charts":[{"id":"x","type":"bar","input":"data.csv","output":"x.svg","options":{"label":"label","values":[{"label":"A"}]}}]})"},
      {"columns-not-array.json", R"({"charts":[{"id":"x","type":"markdown-table","input":"data.csv","output":"x.md","options":{"columns":1}}]})"},
      {"columns-empty-value.json", R"({"charts":[{"id":"x","type":"markdown-table","input":"data.csv","output":"x.md","options":{"columns":[""]}}]})"},
      {"run-on-save-not-bool.json", R"({"charts":[{"id":"x","type":"bar","input":"data.csv","output":"x.svg","runOnSave":"yes","options":{"label":"label","value":"value"}}]})"},
      {"root-not-object.json", R"([])"},
      {"invalid-json.json", R"({"charts":[)"},
  };

  for (const auto& [name, json] : cases) {
    WriteText(root / name, json);
    CHECK_THROWS(csvzall::pipeline::common::LoadChartConfig(root / name));
  }
}

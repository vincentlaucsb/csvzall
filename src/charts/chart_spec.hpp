#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace csvzall::charts {

struct ChartValueSpec {
  std::string column;
  std::string label;
  std::string color;
};

// `sql`, when set, is a SELECT against SQLite table `data` loaded from the
// chart input; the chart's column options then name result columns.
struct HeatmapSpec {
  std::string sql;
  std::string date_column = "date";
  std::string value_column;
  std::vector<ChartValueSpec> values;
  std::string label_column;
  std::string start_date;
  std::string end_date;
  std::string lookback;
  std::string title;
  std::string orientation = "months-horizontal";
};

struct BarSpec {
  std::string sql;
  std::string label_column;
  std::string value_column;
  std::vector<ChartValueSpec> values;
  std::string color_scheme = "sequential";
  std::string title;
  std::string x_label;
  std::string y_label;
  std::string presentation = "stacked";
};

struct LineSpec {
  std::string sql;
  std::string x_column;
  std::string y_column;
  std::vector<ChartValueSpec> values;
  std::string color_scheme = "sequential";
  std::string series_column;
  std::string title;
  std::string x_label;
  std::string y_label;
};

struct MarkdownTableSpec {
  std::string sql;
  std::vector<std::string> columns;
};

struct ChartSpec {
  std::string id;
  std::string type;
  std::filesystem::path input;
  std::optional<std::filesystem::path> output;
  bool run_on_save = false;
  HeatmapSpec heatmap;
  BarSpec bar;
  LineSpec line;
  MarkdownTableSpec markdown_table;
};

// A chart entry that failed to parse. `id` is empty when the entry has no
// readable string id.
struct ChartConfigError {
  std::size_t index = 0;
  std::string id;
  std::string message;
};

struct ChartConfig {
  std::filesystem::path path;
  std::filesystem::path root;
  std::vector<ChartSpec> charts;
  // Only populated by LoadChartConfigAllowingInvalidCharts.
  std::vector<ChartConfigError> invalid_charts;
};

ChartSpec MakeHeatmapChartSpec(std::string id,
                               std::filesystem::path input,
                               std::optional<std::filesystem::path> output,
                               bool run_on_save,
                               HeatmapSpec heatmap);
ChartSpec MakeBarChartSpec(std::string id,
                           std::filesystem::path input,
                           std::optional<std::filesystem::path> output,
                           bool run_on_save,
                           BarSpec bar);
ChartSpec MakeLineChartSpec(std::string id,
                            std::filesystem::path input,
                            std::optional<std::filesystem::path> output,
                            bool run_on_save,
                            LineSpec line);
ChartSpec MakeMarkdownTableChartSpec(std::string id,
                                     std::filesystem::path input,
                                     std::optional<std::filesystem::path> output,
                                     bool run_on_save,
                                     MarkdownTableSpec markdown_table);

std::filesystem::path DefaultChartConfigPath();
std::filesystem::path ResolveChartConfigRoot(const std::filesystem::path& config_path);
// Throws on the first invalid chart entry.
ChartConfig LoadChartConfig(const std::filesystem::path& config_path);
// Throws only for config-level errors (unreadable JSON, bad top-level shape);
// invalid chart entries are skipped and recorded in `invalid_charts`.
ChartConfig LoadChartConfigAllowingInvalidCharts(const std::filesystem::path& config_path);

}  // namespace csvzall::charts

namespace csvzall::pipeline::common {
using ::csvzall::charts::BarSpec;
using ::csvzall::charts::ChartConfig;
using ::csvzall::charts::ChartConfigError;
using ::csvzall::charts::ChartSpec;
using ::csvzall::charts::ChartValueSpec;
using ::csvzall::charts::DefaultChartConfigPath;
using ::csvzall::charts::HeatmapSpec;
using ::csvzall::charts::LineSpec;
using ::csvzall::charts::LoadChartConfig;
using ::csvzall::charts::LoadChartConfigAllowingInvalidCharts;
using ::csvzall::charts::MakeBarChartSpec;
using ::csvzall::charts::MakeHeatmapChartSpec;
using ::csvzall::charts::MakeLineChartSpec;
using ::csvzall::charts::MakeMarkdownTableChartSpec;
using ::csvzall::charts::MarkdownTableSpec;
using ::csvzall::charts::ResolveChartConfigRoot;
}  // namespace csvzall::pipeline::common

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

struct HeatmapSpec {
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

struct ChartConfig {
  std::filesystem::path path;
  std::filesystem::path root;
  std::vector<ChartSpec> charts;
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
ChartConfig LoadChartConfig(const std::filesystem::path& config_path);

}  // namespace csvzall::charts

namespace csvzall::pipeline::common {
using ::csvzall::charts::BarSpec;
using ::csvzall::charts::ChartConfig;
using ::csvzall::charts::ChartSpec;
using ::csvzall::charts::ChartValueSpec;
using ::csvzall::charts::DefaultChartConfigPath;
using ::csvzall::charts::HeatmapSpec;
using ::csvzall::charts::LineSpec;
using ::csvzall::charts::LoadChartConfig;
using ::csvzall::charts::MakeBarChartSpec;
using ::csvzall::charts::MakeHeatmapChartSpec;
using ::csvzall::charts::MakeLineChartSpec;
using ::csvzall::charts::MakeMarkdownTableChartSpec;
using ::csvzall::charts::MarkdownTableSpec;
using ::csvzall::charts::ResolveChartConfigRoot;
}  // namespace csvzall::pipeline::common

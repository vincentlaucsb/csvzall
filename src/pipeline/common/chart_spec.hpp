#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace csvzall::pipeline::common {

struct HeatmapSpec {
  std::string date_column = "date";
  std::string value_column;
  std::string label_column;
  std::string start_date;
  std::string end_date;
  std::string lookback;
  std::string title;
};

struct ChartSpec {
  std::string id;
  std::string type;
  std::filesystem::path input;
  std::optional<std::filesystem::path> output;
  bool run_on_save = false;
  HeatmapSpec heatmap;
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

std::filesystem::path DefaultChartConfigPath();
std::filesystem::path ResolveChartConfigRoot(const std::filesystem::path& config_path);
ChartConfig LoadChartConfig(const std::filesystem::path& config_path);

}  // namespace csvzall::pipeline::common

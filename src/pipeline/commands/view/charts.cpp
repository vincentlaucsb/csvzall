#include "charts.hpp"

#include "json.hpp"

#include "../../common/chart_spec.hpp"
#include "../commands.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace csvzall::pipeline::commands::view_internal {
namespace {

using Json = nlohmann::ordered_json;

std::filesystem::path FindChartConfigPath(const std::filesystem::path& input_path) {
  auto current = std::filesystem::absolute(input_path).parent_path();
  while (!current.empty()) {
    const auto candidate = current / ".csvzall" / "charts.json";
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    const auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return std::filesystem::absolute(input_path).parent_path() / ".csvzall" / "charts.json";
}

std::filesystem::path ChartConfigRoot(const std::filesystem::path& config_path) {
  const auto parent = std::filesystem::absolute(config_path).parent_path();
  if (parent.filename() == ".csvzall") {
    return parent.parent_path();
  }
  return parent;
}

std::string RelativePathForConfig(const std::filesystem::path& root,
                                  const std::filesystem::path& path) {
  const auto absolute_path = std::filesystem::absolute(path).lexically_normal();
  const auto absolute_root = std::filesystem::absolute(root).lexically_normal();
  auto relative = absolute_path.lexically_relative(absolute_root);
  const auto relative_text = relative.generic_string();
  if (relative.empty() || relative_text.starts_with("..")) {
    relative = absolute_path;
  }
  return relative.generic_string();
}

std::filesystem::path NormalizeAbsolutePath(const std::filesystem::path& path) {
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
  return ec ? std::filesystem::absolute(path).lexically_normal() : canonical;
}

bool SamePath(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
  return NormalizeAbsolutePath(lhs) == NormalizeAbsolutePath(rhs);
}

std::string SanitizeChartId(std::string value) {
  std::string result;
  bool last_dash = false;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) != 0) {
      result.push_back(static_cast<char>(std::tolower(ch)));
      last_dash = false;
      continue;
    }
    if (!last_dash && !result.empty()) {
      result.push_back('-');
      last_dash = true;
    }
  }
  while (!result.empty() && result.back() == '-') {
    result.pop_back();
  }
  return result;
}

std::vector<common::ChartValueSpec> ChartValuesFromColumns(
    const std::vector<std::string>& columns) {
  std::vector<common::ChartValueSpec> values;
  values.reserve(columns.size());
  for (const auto& column : columns) {
    if (!column.empty()) {
      values.push_back({column, column, ""});
    }
  }
  return values;
}

Json ChartValueForConfig(const common::ChartValueSpec& value) {
  Json json;
  json["column"] = value.column;
  json["label"] = value.label.empty() ? value.column : value.label;
  if (!value.color.empty()) {
    json["color"] = value.color;
  }
  return json;
}

Json ChartValueForApi(const common::ChartValueSpec& value) {
  Json json;
  json["column"] = value.column;
  json["label"] = value.label.empty() ? value.column : value.label;
  json["color"] = value.color;
  return json;
}

Json ChartValuesForConfig(const std::vector<common::ChartValueSpec>& values) {
  Json json = Json::array();
  for (const auto& value : values) {
    json.push_back(ChartValueForConfig(value));
  }
  return json;
}

Json ChartValuesForApi(const std::vector<common::ChartValueSpec>& values) {
  Json json = Json::array();
  for (const auto& value : values) {
    json.push_back(ChartValueForApi(value));
  }
  return json;
}

Json ChartOptionsForConfig(const common::ChartSpec& spec) {
  Json options;
  if (spec.type == "heatmap") {
    options["date"] = spec.heatmap.date_column;
    if (!spec.heatmap.values.empty()) {
      options["values"] = ChartValuesForConfig(spec.heatmap.values);
    } else if (!spec.heatmap.value_column.empty()) {
      options["value"] = spec.heatmap.value_column;
    }
    if (!spec.heatmap.label_column.empty()) {
      options["label"] = spec.heatmap.label_column;
    }
    if (!spec.heatmap.lookback.empty()) {
      options["lookback"] = spec.heatmap.lookback;
      if (!spec.heatmap.end_date.empty()) {
        options["end"] = spec.heatmap.end_date;
      }
    } else {
      options["start"] = spec.heatmap.start_date;
      options["end"] = spec.heatmap.end_date;
    }
    if (!spec.heatmap.title.empty()) {
      options["title"] = spec.heatmap.title;
    }
    if (!spec.heatmap.orientation.empty() &&
        spec.heatmap.orientation != "months-horizontal") {
      options["orientation"] = spec.heatmap.orientation;
    }
  } else if (spec.type == "bar") {
    options["label"] = spec.bar.label_column;
    if (!spec.bar.values.empty()) {
      options["values"] = ChartValuesForConfig(spec.bar.values);
    } else {
      options["value"] = spec.bar.value_column;
    }
    if (!spec.bar.title.empty()) options["title"] = spec.bar.title;
    if (!spec.bar.x_label.empty()) options["xLabel"] = spec.bar.x_label;
    if (!spec.bar.y_label.empty()) options["yLabel"] = spec.bar.y_label;
    if (!spec.bar.color_scheme.empty() && spec.bar.color_scheme != "sequential") {
      options["colorScheme"] = spec.bar.color_scheme;
    }
    if (!spec.bar.presentation.empty() && spec.bar.presentation != "stacked") {
      options["presentation"] = spec.bar.presentation;
    }
  } else if (spec.type == "line") {
    options["x"] = spec.line.x_column;
    if (!spec.line.values.empty()) {
      options["values"] = ChartValuesForConfig(spec.line.values);
    } else {
      options["y"] = spec.line.y_column;
    }
    if (!spec.line.series_column.empty()) options["series"] = spec.line.series_column;
    if (!spec.line.title.empty()) options["title"] = spec.line.title;
    if (!spec.line.x_label.empty()) options["xLabel"] = spec.line.x_label;
    if (!spec.line.y_label.empty()) options["yLabel"] = spec.line.y_label;
    if (!spec.line.color_scheme.empty() && spec.line.color_scheme != "sequential") {
      options["colorScheme"] = spec.line.color_scheme;
    }
  } else if (spec.type == "markdown-table") {
    if (!spec.markdown_table.sql.empty()) {
      options["sql"] = spec.markdown_table.sql;
    }
    if (!spec.markdown_table.columns.empty()) {
      options["columns"] = spec.markdown_table.columns;
    }
  }
  return options;
}

Json ChartOptionsForApi(const common::ChartSpec& spec) {
  Json options;
  if (spec.type == "heatmap") {
    options["date"] = spec.heatmap.date_column;
    options["value"] = spec.heatmap.value_column;
    options["values"] = ChartValuesForApi(spec.heatmap.values);
    options["label"] = spec.heatmap.label_column;
    options["start"] = spec.heatmap.start_date;
    options["end"] = spec.heatmap.end_date;
    options["lookback"] = spec.heatmap.lookback;
    options["title"] = spec.heatmap.title;
    options["orientation"] = spec.heatmap.orientation;
  } else if (spec.type == "bar") {
    options["label"] = spec.bar.label_column;
    options["value"] = spec.bar.value_column;
    options["values"] = ChartValuesForApi(spec.bar.values);
    options["title"] = spec.bar.title;
    options["xLabel"] = spec.bar.x_label;
    options["yLabel"] = spec.bar.y_label;
    options["colorScheme"] = spec.bar.color_scheme;
    options["presentation"] = spec.bar.presentation;
  } else if (spec.type == "line") {
    options["x"] = spec.line.x_column;
    options["y"] = spec.line.y_column;
    options["values"] = ChartValuesForApi(spec.line.values);
    options["series"] = spec.line.series_column;
    options["title"] = spec.line.title;
    options["xLabel"] = spec.line.x_label;
    options["yLabel"] = spec.line.y_label;
    options["colorScheme"] = spec.line.color_scheme;
  } else if (spec.type == "markdown-table") {
    options["sql"] = spec.markdown_table.sql;
    options["columns"] = spec.markdown_table.columns;
  }
  return options;
}

Json ChartSpecForConfig(const common::ChartSpec& spec,
                        const std::filesystem::path& root) {
  if (!spec.output) {
    throw std::runtime_error("chart output path is required: " + spec.id);
  }

  Json json;
  json["id"] = spec.id;
  json["type"] = spec.type;
  json["input"] = RelativePathForConfig(root, spec.input);
  json["output"] = RelativePathForConfig(root, *spec.output);
  json["options"] = ChartOptionsForConfig(spec);
  json["runOnSave"] = spec.run_on_save;
  return json;
}

Json ChartSpecForApi(const common::ChartSpec& spec,
                     const std::filesystem::path& root) {
  Json json;
  json["id"] = spec.id;
  json["type"] = spec.type;
  json["input"] = RelativePathForConfig(root, spec.input);
  json["output"] = spec.output ? RelativePathForConfig(root, *spec.output) : "";
  json["options"] = ChartOptionsForApi(spec);
  json["runOnSave"] = spec.run_on_save;
  return json;
}

std::string SerializeChartConfig(const std::vector<common::ChartSpec>& charts,
                                 const std::filesystem::path& root) {
  Json chart_values = Json::array();
  for (const auto& chart : charts) {
    chart_values.push_back(ChartSpecForConfig(chart, root));
  }

  Json json;
  json["charts"] = std::move(chart_values);
  return json.dump(2) + "\n";
}

std::vector<common::ChartSpec> LoadChartConfigIfExists(const std::filesystem::path& config_path) {
  if (!std::filesystem::exists(config_path)) {
    return {};
  }
  return common::LoadChartConfig(config_path).charts;
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("unable to open chart config for writing: " + path.string());
  }
  output << text;
  output.flush();
  if (!output.good()) {
    throw std::runtime_error("failed to write chart config: " + path.string());
  }
}

std::filesystem::path ResolveChartOutputPath(const std::filesystem::path& root,
                                             const std::string& output) {
  const std::filesystem::path output_path(output);
  return output_path.is_absolute() ? output_path : root / output_path;
}

void RenderSavedChart(common::ChartSpec spec,
                      const LoggerCallbacks& logger) {
#ifdef CSVZALL_HAVE_SVGPLOT
  RunOptions options;
  options.input_path = spec.input.string();
  RunStats stats;
  std::string render_error;
  LoggerCallbacks chart_logger = logger;
  chart_logger.error = [&logger, &render_error](const std::string& message) {
    render_error = message;
    if (logger.error) {
      logger.error(message);
    }
  };
  const auto rc = RunChart(spec, options, chart_logger, stats);
  if (rc != 0) {
    auto message = "chart config saved, but chart rendering failed: " + spec.id;
    if (!render_error.empty()) {
      message += ": " + render_error;
    }
    throw std::runtime_error(message);
  }
#else
  (void)spec;
  (void)logger;
  throw std::runtime_error("chart config saved, but SVG chart rendering is disabled in this build");
#endif
}

void ValidateChartBeforeSave(const common::ChartSpec& spec,
                             const LoggerCallbacks& logger) {
#ifdef CSVZALL_HAVE_SVGPLOT
  RunOptions options;
  options.input_path = spec.input.string();
  RunStats stats;
  std::string validation_error;
  LoggerCallbacks chart_logger = logger;
  chart_logger.error = [&logger, &validation_error](const std::string& message) {
    validation_error = message;
    if (logger.error) {
      logger.error(message);
    }
  };
  const auto rc = ValidateChart(spec, options, chart_logger, stats);
  if (rc != 0) {
    throw std::runtime_error(validation_error.empty() ? "chart validation failed"
                                                     : validation_error);
  }
#else
  (void)spec;
  (void)logger;
#endif
}

}  // namespace

std::string BuildChartConfigListJson(const CsvViewData& data) {
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto root = ChartConfigRoot(config_path);
  const auto charts = LoadChartConfigIfExists(config_path);

  Json chart_values = Json::array();
  std::set<std::string> seen_ids;
  for (const auto& chart : charts) {
    if (!SamePath(chart.input, data.input_path())) {
      continue;
    }
    if (!seen_ids.insert(chart.id).second) {
      continue;
    }
    chart_values.push_back(ChartSpecForApi(chart, root));
  }

  Json result;
  result["ok"] = true;
  result["configPath"] = config_path.string();
  result["charts"] = std::move(chart_values);
  return result.dump();
}

common::ChartSpec FindCurrentCsvChart(const CsvViewData& data, const std::string& id) {
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto charts = LoadChartConfigIfExists(config_path);
  for (const auto& chart : charts) {
    if (chart.id == id && SamePath(chart.input, data.input_path())) {
      return chart;
    }
  }
  throw std::runtime_error("chart not found for current CSV: " + id);
}

std::string GenerateCurrentCsvChart(const CsvViewData& data,
                                    std::string_view body,
                                    const LoggerCallbacks& logger) {
  const auto id = JsonStringField(body, "id");
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto root = ChartConfigRoot(config_path);
  const auto chart = FindCurrentCsvChart(data, id);
  if (!chart.output) {
    throw std::runtime_error("chart output path is required: " + chart.id);
  }
  (void)data;
  RenderSavedChart(chart, logger);

  Json result;
  result["ok"] = true;
  result["id"] = chart.id;
  result["output"] = RelativePathForConfig(root, *chart.output);
  result["generated"] = true;
  return result.dump();
}

std::size_t RenderRunOnSaveChartsForCurrentCsv(const CsvViewData& data,
                                               const LoggerCallbacks& logger) {
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto charts = LoadChartConfigIfExists(config_path);
  std::size_t generated = 0;
  for (const auto& chart : charts) {
    if (!chart.run_on_save || !SamePath(chart.input, data.input_path())) {
      continue;
    }
    if (chart.output && SamePath(*chart.output, data.input_path())) {
      continue;
    }
    RenderSavedChart(chart, logger);
    ++generated;
  }
  return generated;
}

std::string AppendHeatmapChartConfig(const CsvViewData& data,
                                     std::string_view body,
                                     const LoggerCallbacks& logger) {
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto root = ChartConfigRoot(config_path);
  const auto input_path = RelativePathForConfig(root, data.input_path());
  const auto id = SanitizeChartId(JsonStringFieldOr(body, "id", ""));
  const auto type = JsonStringFieldOr(body, "type", "heatmap");
  const auto output = JsonStringFieldOr(body, "output", "");
  const auto title = JsonStringFieldOr(body, "title", "");
  const auto run_on_save = JsonBoolFieldOr(body, "runOnSave", true);

  auto charts = LoadChartConfigIfExists(config_path);
  common::ChartSpec spec;
  std::optional<std::filesystem::path> output_path;
  if (!output.empty()) {
    output_path = ResolveChartOutputPath(root, output);
  }
  if (type == "heatmap") {
    const auto date = JsonStringFieldOr(body, "date", "");
    const auto value = JsonStringFieldOr(body, "value", "");
    const auto values = ChartValuesFromColumns(
        JsonStringArrayFieldOr(body, "values", {}));
    const auto label = JsonStringFieldOr(body, "label", "");
    const auto start = JsonStringFieldOr(body, "start", "");
    const auto end = JsonStringFieldOr(body, "end", "");
    const auto lookback = JsonStringFieldOr(body, "lookback", "");
    const auto orientation = JsonStringFieldOr(body, "orientation", "months-horizontal");
    common::HeatmapSpec heatmap;
    heatmap.date_column = date;
    heatmap.value_column = value;
    heatmap.values = values;
    heatmap.label_column = label;
    heatmap.start_date = start;
    heatmap.end_date = end;
    heatmap.lookback = lookback;
    heatmap.title = title;
    heatmap.orientation = orientation;
    spec = common::MakeHeatmapChartSpec(
        id,
        ResolveChartOutputPath(root, input_path),
        output_path,
        run_on_save,
        heatmap);
  } else if (type == "bar") {
    const auto label = JsonStringFieldOr(body, "label", "");
    const auto value = JsonStringFieldOr(body, "value", "");
    const auto values = ChartValuesFromColumns(
        JsonStringArrayFieldOr(body, "values", {}));
    common::BarSpec bar;
    bar.label_column = label;
    bar.value_column = value;
    bar.values = values;
    bar.title = title;
    bar.x_label = JsonStringFieldOr(body, "xLabel", "");
    bar.y_label = JsonStringFieldOr(body, "yLabel", "");
    bar.color_scheme = JsonStringFieldOr(body, "colorScheme", "sequential");
    bar.presentation = JsonStringFieldOr(body, "presentation", "stacked");
    spec = common::MakeBarChartSpec(
        id,
        ResolveChartOutputPath(root, input_path),
        output_path,
        run_on_save,
        bar);
  } else if (type == "line") {
    const auto x = JsonStringFieldOr(body, "x", "");
    const auto y = JsonStringFieldOr(body, "y", "");
    const auto values = ChartValuesFromColumns(
        JsonStringArrayFieldOr(body, "values", {}));
    const auto series = JsonStringFieldOr(body, "series", "");
    common::LineSpec line;
    line.x_column = x;
    line.y_column = y;
    line.values = values;
    line.series_column = series;
    line.title = title;
    line.x_label = JsonStringFieldOr(body, "xLabel", "");
    line.y_label = JsonStringFieldOr(body, "yLabel", "");
    line.color_scheme = JsonStringFieldOr(body, "colorScheme", "sequential");
    spec = common::MakeLineChartSpec(
        id,
        ResolveChartOutputPath(root, input_path),
        output_path,
        run_on_save,
        line);
  } else if (type == "markdown-table") {
    common::MarkdownTableSpec markdown_table;
    markdown_table.sql = JsonStringFieldOr(body, "sql", "");
    markdown_table.columns = JsonStringArrayFieldOr(body, "columns", {});
    spec = common::MakeMarkdownTableChartSpec(
        id,
        ResolveChartOutputPath(root, input_path),
        output_path,
        run_on_save,
        markdown_table);
  } else {
    throw std::runtime_error("unknown chart type: " + type);
  }
  ValidateChartBeforeSave(spec, logger);

  bool updated_existing = false;
  std::vector<common::ChartSpec> updated_charts;
  updated_charts.reserve(charts.size() + 1);
  for (auto& chart : charts) {
    if (chart.id == id) {
      if (!updated_existing) {
        updated_charts.push_back(spec);
        updated_existing = true;
      }
      continue;
    }
    updated_charts.push_back(std::move(chart));
  }
  if (!updated_existing) {
    updated_charts.push_back(spec);
  }
  WriteTextFile(config_path, SerializeChartConfig(updated_charts, root));
  RenderSavedChart(spec, logger);

  Json result;
  result["ok"] = true;
  result["configPath"] = config_path.string();
  result["id"] = id;
  result["output"] = output;
  result["action"] = updated_existing ? "updated" : "created";
  result["generated"] = true;
  return result.dump();
}
}  // namespace csvzall::pipeline::commands::view_internal

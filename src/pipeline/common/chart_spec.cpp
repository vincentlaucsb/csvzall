#include "chart_spec.hpp"

#include <simdjson.h>

#include <array>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace csvzall::pipeline::common {
namespace {

bool Contains(const std::set<std::string_view>& values, std::string_view value) {
  return values.find(value) != values.end();
}

void RejectUnknownKeys(simdjson::dom::object object,
                       const std::set<std::string_view>& allowed,
                       const std::string& context) {
  for (auto field : object) {
    const std::string_view key = field.key;
    if (!Contains(allowed, key)) {
      throw std::runtime_error(context + ": unknown key '" + std::string(key) + "'");
    }
  }
}

simdjson::dom::element RequireField(simdjson::dom::object object,
                                    std::string_view key,
                                    const std::string& context) {
  simdjson::dom::element value;
  const auto error = object.at_key(key).get(value);
  if (error == simdjson::NO_SUCH_FIELD) {
    throw std::runtime_error(context + ": missing required key '" + std::string(key) + "'");
  }
  if (error) {
    throw std::runtime_error(context + ": unable to read key '" + std::string(key) +
                             "': " + simdjson::error_message(error));
  }
  return value;
}

std::optional<simdjson::dom::element> OptionalField(simdjson::dom::object object,
                                                    std::string_view key,
                                                    const std::string& context) {
  simdjson::dom::element value;
  const auto error = object.at_key(key).get(value);
  if (error == simdjson::NO_SUCH_FIELD) {
    return std::nullopt;
  }
  if (error) {
    throw std::runtime_error(context + ": unable to read key '" + std::string(key) +
                             "': " + simdjson::error_message(error));
  }
  return value;
}

std::string AsString(simdjson::dom::element value,
                     std::string_view key,
                     const std::string& context) {
  std::string_view result;
  const auto error = value.get_string().get(result);
  if (error) {
    throw std::runtime_error(context + ": key '" + std::string(key) +
                             "' must be a string");
  }
  return std::string(result);
}

std::string RequireString(simdjson::dom::object object,
                          std::string_view key,
                          const std::string& context) {
  return AsString(RequireField(object, key, context), key, context);
}

std::string OptionalString(simdjson::dom::object object,
                           std::string_view key,
                           const std::string& fallback,
                           const std::string& context) {
  const auto value = OptionalField(object, key, context);
  if (!value) {
    return fallback;
  }
  return AsString(*value, key, context);
}

bool OptionalBool(simdjson::dom::object object,
                  std::string_view key,
                  bool fallback,
                  const std::string& context) {
  const auto value = OptionalField(object, key, context);
  if (!value) {
    return fallback;
  }
  bool result = false;
  const auto error = value->get_bool().get(result);
  if (error) {
    throw std::runtime_error(context + ": key '" + std::string(key) +
                             "' must be a boolean");
  }
  return result;
}

simdjson::dom::object RequireObject(simdjson::dom::element value,
                                    std::string_view key,
                                    const std::string& context);

ChartValueSpec ParseChartValue(simdjson::dom::element value,
                               std::string_view key,
                               const std::string& context) {
  std::string_view column;
  if (!value.get_string().get(column)) {
    return ChartValueSpec{std::string(column), std::string(column), ""};
  }

  const auto object = RequireObject(value, key, context);
  static const std::set<std::string_view> kAllowed{"column", "label", "color"};
  RejectUnknownKeys(object, kAllowed, context);

  ChartValueSpec spec;
  spec.column = RequireString(object, "column", context);
  spec.label = OptionalString(object, "label", spec.column, context);
  spec.color = OptionalString(object, "color", "", context);
  return spec;
}

std::vector<ChartValueSpec> OptionalValues(simdjson::dom::object object,
                                           std::string_view key,
                                           const std::string& context) {
  const auto value = OptionalField(object, key, context);
  if (!value) {
    return {};
  }

  simdjson::dom::array array;
  const auto error = value->get_array().get(array);
  if (error) {
    throw std::runtime_error(context + ": key '" + std::string(key) +
                             "' must be an array");
  }

  std::vector<ChartValueSpec> values;
  std::size_t index = 0;
  for (simdjson::dom::element entry : array) {
    auto spec = ParseChartValue(entry, key,
                                context + "." + std::string(key) +
                                    "[" + std::to_string(index) + "]");
    if (spec.column.empty()) {
      throw std::runtime_error(context + "." + std::string(key) +
                               "[" + std::to_string(index) + "]: column is required");
    }
    values.push_back(std::move(spec));
    ++index;
  }
  return values;
}

std::vector<std::string> OptionalStringArray(simdjson::dom::object object,
                                             std::string_view key,
                                             const std::string& context) {
  const auto value = OptionalField(object, key, context);
  if (!value) {
    return {};
  }

  simdjson::dom::array array;
  const auto error = value->get_array().get(array);
  if (error) {
    throw std::runtime_error(context + ": key '" + std::string(key) +
                             "' must be an array");
  }

  std::vector<std::string> result;
  std::size_t index = 0;
  for (simdjson::dom::element entry : array) {
    auto item = AsString(entry,
                         std::string(key) + "[" + std::to_string(index) + "]",
                         context);
    if (item.empty()) {
      throw std::runtime_error(context + "." + std::string(key) +
                               "[" + std::to_string(index) + "]: value is required");
    }
    result.push_back(std::move(item));
    ++index;
  }
  return result;
}

simdjson::dom::object RequireObject(simdjson::dom::element value,
                                    std::string_view key,
                                    const std::string& context) {
  simdjson::dom::object result;
  const auto error = value.get_object().get(result);
  if (error) {
    throw std::runtime_error(context + ": key '" + std::string(key) +
                             "' must be an object");
  }
  return result;
}

HeatmapSpec ParseHeatmapOptions(simdjson::dom::object options,
                                const std::string& context) {
  static const std::set<std::string_view> kAllowed{
      "date", "value", "values", "label", "start", "end", "lookback", "title",
      "orientation"};
  RejectUnknownKeys(options, kAllowed, context + ".options");

  HeatmapSpec spec;
  spec.date_column = OptionalString(options, "date", "date", context + ".options");
  spec.value_column = OptionalString(options, "value", "", context + ".options");
  spec.values = OptionalValues(options, "values", context + ".options");
  spec.label_column = OptionalString(options, "label", "", context + ".options");
  spec.lookback = OptionalString(options, "lookback", "", context + ".options");
  spec.start_date = OptionalString(options, "start", "", context + ".options");
  spec.end_date = OptionalString(options, "end", "", context + ".options");
  spec.orientation = OptionalString(
      options, "orientation", "months-horizontal", context + ".options");
  if (spec.lookback.empty() && (spec.start_date.empty() || spec.end_date.empty())) {
    throw std::runtime_error(context + ".options: provide start/end or lookback");
  }
  if (!spec.lookback.empty() && !spec.start_date.empty()) {
    throw std::runtime_error(context + ".options: lookback cannot be combined with start");
  }
  spec.title = OptionalString(options, "title", "", context + ".options");
  return spec;
}

BarSpec ParseBarOptions(simdjson::dom::object options,
                        const std::string& context) {
  static const std::set<std::string_view> kAllowed{
      "label", "value", "values", "title", "xLabel", "yLabel", "presentation"};
  RejectUnknownKeys(options, kAllowed, context + ".options");

  BarSpec spec;
  spec.label_column = RequireString(options, "label", context + ".options");
  spec.value_column = OptionalString(options, "value", "", context + ".options");
  spec.values = OptionalValues(options, "values", context + ".options");
  spec.title = OptionalString(options, "title", "", context + ".options");
  spec.x_label = OptionalString(options, "xLabel", "", context + ".options");
  spec.y_label = OptionalString(options, "yLabel", "", context + ".options");
  spec.presentation = OptionalString(options, "presentation", "stacked", context + ".options");
  return spec;
}

LineSpec ParseLineOptions(simdjson::dom::object options,
                          const std::string& context) {
  static const std::set<std::string_view> kAllowed{
      "x", "y", "values", "series", "title", "xLabel", "yLabel"};
  RejectUnknownKeys(options, kAllowed, context + ".options");

  LineSpec spec;
  spec.x_column = RequireString(options, "x", context + ".options");
  spec.y_column = OptionalString(options, "y", "", context + ".options");
  spec.values = OptionalValues(options, "values", context + ".options");
  spec.series_column = OptionalString(options, "series", "", context + ".options");
  spec.title = OptionalString(options, "title", "", context + ".options");
  spec.x_label = OptionalString(options, "xLabel", "", context + ".options");
  spec.y_label = OptionalString(options, "yLabel", "", context + ".options");
  return spec;
}

MarkdownTableSpec ParseMarkdownTableOptions(simdjson::dom::object options,
                                            const std::string& context) {
  static const std::set<std::string_view> kAllowed{"sql", "columns"};
  RejectUnknownKeys(options, kAllowed, context + ".options");

  MarkdownTableSpec spec;
  spec.sql = OptionalString(options, "sql", "", context + ".options");
  spec.columns = OptionalStringArray(options, "columns", context + ".options");
  return spec;
}

std::filesystem::path ResolveConfigPath(const std::filesystem::path& config_path) {
  return std::filesystem::absolute(config_path).lexically_normal();
}

std::filesystem::path ResolveAgainstRoot(const std::filesystem::path& root,
                                         const std::filesystem::path& path) {
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return (root / path).lexically_normal();
}

}  // namespace

ChartSpec MakeHeatmapChartSpec(std::string id,
                               std::filesystem::path input,
                               std::optional<std::filesystem::path> output,
                               bool run_on_save,
                               HeatmapSpec heatmap) {
  ChartSpec spec;
  spec.id = std::move(id);
  spec.type = "heatmap";
  spec.input = std::move(input);
  spec.output = std::move(output);
  spec.run_on_save = run_on_save;
  spec.heatmap = std::move(heatmap);
  return spec;
}

ChartSpec MakeBarChartSpec(std::string id,
                           std::filesystem::path input,
                           std::optional<std::filesystem::path> output,
                           bool run_on_save,
                           BarSpec bar) {
  ChartSpec spec;
  spec.id = std::move(id);
  spec.type = "bar";
  spec.input = std::move(input);
  spec.output = std::move(output);
  spec.run_on_save = run_on_save;
  spec.bar = std::move(bar);
  return spec;
}

ChartSpec MakeLineChartSpec(std::string id,
                            std::filesystem::path input,
                            std::optional<std::filesystem::path> output,
                            bool run_on_save,
                            LineSpec line) {
  ChartSpec spec;
  spec.id = std::move(id);
  spec.type = "line";
  spec.input = std::move(input);
  spec.output = std::move(output);
  spec.run_on_save = run_on_save;
  spec.line = std::move(line);
  return spec;
}

ChartSpec MakeMarkdownTableChartSpec(std::string id,
                                     std::filesystem::path input,
                                     std::optional<std::filesystem::path> output,
                                     bool run_on_save,
                                     MarkdownTableSpec markdown_table) {
  ChartSpec spec;
  spec.id = std::move(id);
  spec.type = "markdown-table";
  spec.input = std::move(input);
  spec.output = std::move(output);
  spec.run_on_save = run_on_save;
  spec.markdown_table = std::move(markdown_table);
  return spec;
}

std::filesystem::path DefaultChartConfigPath() {
  return std::filesystem::current_path() / ".csvzall" / "charts.json";
}

std::filesystem::path ResolveChartConfigRoot(const std::filesystem::path& config_path) {
  const auto absolute = ResolveConfigPath(config_path);
  const auto parent = absolute.parent_path();
  if (parent.filename() == ".csvzall") {
    return parent.parent_path();
  }
  return parent;
}

ChartConfig LoadChartConfig(const std::filesystem::path& config_path) {
  const auto absolute_config_path = ResolveConfigPath(config_path);
  if (!std::filesystem::exists(absolute_config_path)) {
    throw std::runtime_error("config file not found: " + absolute_config_path.string());
  }

  simdjson::dom::parser parser;
  simdjson::dom::element document;
  const auto load_error = parser.load(absolute_config_path.string()).get(document);
  if (load_error) {
    throw std::runtime_error("unable to parse config JSON: " +
                             std::string(simdjson::error_message(load_error)));
  }

  simdjson::dom::object root_object;
  const auto root_error = document.get_object().get(root_object);
  if (root_error) {
    throw std::runtime_error("chart config root must be a JSON object");
  }

  static const std::set<std::string_view> kTopLevelAllowed{"charts"};
  RejectUnknownKeys(root_object, kTopLevelAllowed, "chart config");

  simdjson::dom::array charts_array;
  const auto charts_error =
      RequireField(root_object, "charts", "chart config").get_array().get(charts_array);
  if (charts_error) {
    throw std::runtime_error("chart config: key 'charts' must be an array");
  }

  ChartConfig config;
  config.path = absolute_config_path;
  config.root = ResolveChartConfigRoot(absolute_config_path);

  std::size_t index = 0;
  for (simdjson::dom::element chart_element : charts_array) {
    const std::string context = "charts[" + std::to_string(index) + "]";
    ++index;

    simdjson::dom::object chart_object;
    const auto chart_error = chart_element.get_object().get(chart_object);
    if (chart_error) {
      throw std::runtime_error(context + " must be an object");
    }

    static const std::set<std::string_view> kChartAllowed{
        "id", "type", "input", "output", "options", "runOnSave"};
    RejectUnknownKeys(chart_object, kChartAllowed, context);

    const auto id = RequireString(chart_object, "id", context);
    const auto type = RequireString(chart_object, "type", context);
    if (type != "heatmap" && type != "bar" && type != "line" &&
        type != "markdown-table") {
      throw std::runtime_error(context + ": unknown chart type '" + type + "'");
    }

    const auto input = ResolveAgainstRoot(
        config.root, std::filesystem::path(RequireString(chart_object, "input", context)));

    std::optional<std::filesystem::path> output;
    if (const auto output_value = OptionalField(chart_object, "output", context)) {
      output = ResolveAgainstRoot(
          config.root, std::filesystem::path(AsString(*output_value, "output", context)));
    }

    const auto options = RequireObject(RequireField(chart_object, "options", context),
                                       "options", context);
    const auto run_on_save = OptionalBool(chart_object, "runOnSave", false, context);
    if (type == "heatmap") {
      config.charts.push_back(MakeHeatmapChartSpec(
          id, input, output, run_on_save, ParseHeatmapOptions(options, context)));
    } else if (type == "bar") {
      config.charts.push_back(MakeBarChartSpec(
          id, input, output, run_on_save, ParseBarOptions(options, context)));
    } else if (type == "line") {
      config.charts.push_back(MakeLineChartSpec(
          id, input, output, run_on_save, ParseLineOptions(options, context)));
    } else {
      config.charts.push_back(MakeMarkdownTableChartSpec(
          id, input, output, run_on_save, ParseMarkdownTableOptions(options, context)));
    }
  }

  return config;
}

}  // namespace csvzall::pipeline::common

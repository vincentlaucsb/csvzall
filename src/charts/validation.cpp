#include "csv_chart.hpp"

#include <exception>

namespace csvzall::charts {

std::optional<std::string> ValidateChartSpecFields(const ChartSpec& spec) {
  if (spec.id.empty()) {
    return "charts: chart id is required";
  }
  if (spec.type != "heatmap" && spec.type != "bar" && spec.type != "line" &&
      spec.type != "markdown-table") {
    return "charts: unknown chart type '" + spec.type + "'";
  }
  if (!spec.output) {
    return "charts: chart '" + spec.id + "' requires an output path";
  }
  if (spec.type == "heatmap") {
    if (spec.heatmap.date_column.empty()) {
      return "heatmap: date column is required";
    }
    try {
      (void)ResolveHeatmapDateRange(spec.heatmap);
      (void)ParseHeatmapOrientation(spec.heatmap.orientation);
    } catch (const std::exception& ex) {
      return std::string("heatmap: ") + ex.what();
    }
    return std::nullopt;
  }
  if (spec.type == "bar") {
    if (spec.bar.label_column.empty()) {
      return "bar: label column is required";
    }
    if (spec.bar.value_column.empty() && spec.bar.values.empty()) {
      return "bar: value column is required";
    }
    try {
      (void)IsGroupedBarPresentation(spec.bar.presentation);
    } catch (const std::exception& ex) {
      return std::string("bar: ") + ex.what();
    }
    return std::nullopt;
  }
  if (spec.type == "line") {
    if (spec.line.x_column.empty()) {
      return "line: x column is required";
    }
    if (spec.line.y_column.empty() && spec.line.values.empty()) {
      return "line: y column is required";
    }
    if (spec.line.values.size() > 1 && !spec.line.series_column.empty()) {
      return "line: series column cannot be combined with multiple value columns";
    }
    return std::nullopt;
  }
  if (spec.type == "markdown-table") {
    if (!spec.markdown_table.sql.empty() && !spec.markdown_table.columns.empty()) {
      return "markdown-table: sql cannot be combined with columns";
    }
    return std::nullopt;
  }
  return "charts: unknown chart type '" + spec.type + "'";
}

}  // namespace csvzall::charts

#include "csv_chart_common.hpp"

#include <svgplot/svgplot.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace csvzall::charts {

CsvChartResult RenderLineCsv(csv::CSVReader& reader,
                             const std::vector<std::string>& headers,
                             const LineSpec& spec,
                             const CsvChartOptions& options) {
  const auto x_index =
      FindColumnIndex(headers, spec.x_column, options.exact_column_matching);
  if (!x_index) {
    throw std::runtime_error("x column not found: " + spec.x_column);
  }
  const auto value_specs = EffectiveValues(spec.values, spec.y_column);
  if (value_specs.empty()) {
    throw std::runtime_error("y column is required");
  }

  std::optional<std::size_t> series_index;
  if (!spec.series_column.empty()) {
    series_index =
        FindColumnIndex(headers, spec.series_column, options.exact_column_matching);
    if (!series_index) {
      throw std::runtime_error("series column not found: " + spec.series_column);
    }
  }

  CsvChartResult result;
  if (value_specs.size() > 1) {
    std::vector<std::size_t> value_indices;
    value_indices.reserve(value_specs.size());
    for (const auto& value_spec : value_specs) {
      const auto index =
          FindColumnIndex(headers, value_spec.column, options.exact_column_matching);
      if (!index) {
        throw std::runtime_error("y column not found: " + value_spec.column);
      }
      value_indices.push_back(*index);
    }

    std::vector<std::vector<svgplot::Point>> points_by_value(value_specs.size());
    for (auto& row : reader) {
      const auto x = ParseNumericField(row[*x_index], spec.x_column, "line x");
      for (std::size_t i = 0; i < value_specs.size(); ++i) {
        points_by_value[i].push_back({
            x,
            ParseNumericField(row[value_indices[i]], value_specs[i].column, "line y")});
      }
      ++result.rows_processed;
      AccumulateRowBytes(row, result);
    }

    std::vector<svgplot::Series> series;
    series.reserve(value_specs.size());
    for (std::size_t i = 0; i < value_specs.size(); ++i) {
      auto& points = points_by_value[i];
      std::sort(points.begin(), points.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.x < rhs.x;
      });
      series.push_back({ValueLabel(value_specs[i]), std::move(points), ValueColor(value_specs[i], i)});
    }

    svgplot::ChartOptions chart_options;
    chart_options.title = spec.title;
    chart_options.x_label = spec.x_label;
    chart_options.y_label = spec.y_label;
    result.svg = svgplot::line_chart(series, chart_options).str();
    return result;
  }

  const auto y_index =
      FindColumnIndex(headers, value_specs.front().column, options.exact_column_matching);
  if (!y_index) {
    throw std::runtime_error("y column not found: " + value_specs.front().column);
  }

  std::vector<std::string> order;
  std::map<std::string, std::vector<svgplot::Point>> points_by_series;
  for (auto& row : reader) {
    const auto series = series_index
        ? row[*series_index].get<std::string>()
        : std::string{"Series"};
    if (!points_by_series.contains(series)) {
      order.push_back(series);
    }
    points_by_series[series].push_back({
        ParseNumericField(row[*x_index], spec.x_column, "line x"),
        ParseNumericField(row[*y_index], value_specs.front().column, "line y")});
    ++result.rows_processed;
    AccumulateRowBytes(row, result);
  }

  std::vector<svgplot::Series> series;
  series.reserve(order.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    auto& points = points_by_series[order[i]];
    std::sort(points.begin(), points.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.x < rhs.x;
    });
    series.push_back({order[i], std::move(points), svgplot::default_series_color(i)});
  }

  svgplot::ChartOptions chart_options;
  chart_options.title = spec.title;
  chart_options.x_label = spec.x_label;
  chart_options.y_label = spec.y_label;
  result.svg = svgplot::line_chart(series, chart_options).str();
  return result;
}

}  // namespace csvzall::charts

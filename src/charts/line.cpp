#include "csv_chart_common.hpp"
#include "line_time.hpp"

#include <svgplot/svgplot.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace csvzall::charts {
namespace {

class LineXAxis {
public:
  double parse(csv::CSVField field, const std::string& column) {
    const auto date = ParseLineDate(field.get<std::string>());
    if (dates_ && *dates_ != date.has_value()) {
      throw std::runtime_error("line x must contain only numbers or only ISO dates/timestamps in column: " + column);
    }
    dates_ = date.has_value();
    if (!date) return ParseNumericField(field, column, "line x");
    first_day_ = first_day_ ? std::min(*first_day_, std::floor(*date)) : std::floor(*date);
    return *date;
  }

  std::string render(std::vector<svgplot::Series>& series, const LineSpec& spec) const {
    svgplot::ChartOptions options;
    options.title = spec.title;
    options.x_label = spec.x_label;
    options.y_label = spec.y_label;
    if (first_day_) {
      for (auto& item : series) {
        for (auto& point : item.points) point.x -= *first_day_;
      }
      const auto origin = std::chrono::sys_days{std::chrono::days{static_cast<int>(*first_day_)}};
      options.x_label = (spec.x_label.empty() ? spec.x_column : spec.x_label) +
          " (days since " + pipeline::common::FormatIsoDate(origin) + " UTC)";
    }
    return svgplot::line_chart(series, options).str();
  }

private:
  std::optional<bool> dates_;
  std::optional<double> first_day_;
};

}  // namespace

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
  LineXAxis x_axis;
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
      const auto x = x_axis.parse(row[*x_index], spec.x_column);
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
      series.push_back({
          ValueLabel(value_specs[i]),
          std::move(points),
          ValueColor(value_specs[i], i, spec.color_scheme)});
    }

    result.svg = x_axis.render(series, spec);
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
        x_axis.parse(row[*x_index], spec.x_column),
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

  result.svg = x_axis.render(series, spec);
  return result;
}

}  // namespace csvzall::charts

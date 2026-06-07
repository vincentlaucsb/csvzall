#include "csv_chart_common.hpp"

#include <svgplot/svgplot.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <utility>

namespace csvzall::charts {
namespace {

std::string NormalizedBarPresentation(std::string_view raw) {
  auto value = Trim(raw);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  value.erase(
      std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
        return ch == '_' || ch == ' ' || ch == '-';
      }),
      value.end());
  return value;
}

}  // namespace

bool IsGroupedBarPresentation(std::string_view raw) {
  const auto value = NormalizedBarPresentation(raw);
  if (value.empty() || value == "stacked") {
    return false;
  }
  if (value == "grouped") {
    return true;
  }
  throw std::runtime_error(
      "presentation must be stacked or grouped: " + std::string(raw));
}

CsvChartResult RenderBarCsv(csv::CSVReader& reader,
                            const std::vector<std::string>& headers,
                            const BarSpec& spec,
                            const CsvChartOptions& options) {
  const auto label_index =
      FindColumnIndex(headers, spec.label_column, options.exact_column_matching);
  if (!label_index) {
    throw std::runtime_error("label column not found: " + spec.label_column);
  }

  svgplot::ChartOptions chart_options;
  chart_options.title = spec.title;
  chart_options.x_label = spec.x_label;
  chart_options.y_label = spec.y_label;

  CsvChartResult result;
  const auto value_specs = EffectiveValues(spec.values, spec.value_column);
  if (value_specs.empty()) {
    throw std::runtime_error("value column is required");
  }
  if (value_specs.size() <= 1) {
    const auto value_index =
        FindColumnIndex(headers, value_specs.front().column, options.exact_column_matching);
    if (!value_index) {
      throw std::runtime_error("value column not found: " + value_specs.front().column);
    }

    std::vector<std::string> order;
    std::map<std::string, double> totals;
    for (auto& row : reader) {
      const auto label = row[*label_index].get<std::string>();
      if (!totals.contains(label)) {
        order.push_back(label);
      }
      totals[label] += ParseNumericField(row[*value_index], value_specs.front().column, "bar");
      ++result.rows_processed;
      AccumulateRowBytes(row, result);
    }

    std::vector<svgplot::Bar> bars;
    bars.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
      bars.push_back({
          order[i],
          totals[order[i]],
          svgplot::default_series_color(i)});
    }
    result.svg = svgplot::bar_chart(bars, chart_options).str();
    return result;
  }

  std::vector<std::size_t> value_indices;
  value_indices.reserve(value_specs.size());
  for (const auto& value_spec : value_specs) {
    const auto index =
        FindColumnIndex(headers, value_spec.column, options.exact_column_matching);
    if (!index) {
      throw std::runtime_error("value column not found: " + value_spec.column);
    }
    value_indices.push_back(*index);
  }

  std::vector<std::string> order;
  std::map<std::string, std::vector<double>> totals;
  for (auto& row : reader) {
    const auto label = row[*label_index].get<std::string>();
    if (!totals.contains(label)) {
      order.push_back(label);
      totals[label] = std::vector<double>(value_specs.size(), 0.0);
    }
    for (std::size_t i = 0; i < value_specs.size(); ++i) {
      totals[label][i] += ParseNumericField(row[value_indices[i]], value_specs[i].column, "bar");
    }
    ++result.rows_processed;
    AccumulateRowBytes(row, result);
  }

  svgplot::BarChart chart;
  const auto grouped = IsGroupedBarPresentation(spec.presentation);
  for (const auto& label : order) {
    std::vector<svgplot::BarSegment> segments;
    segments.reserve(value_specs.size());
    for (std::size_t i = 0; i < value_specs.size(); ++i) {
      segments.push_back({
          ValueLabel(value_specs[i]),
          totals[label][i],
          ValueColor(value_specs[i], i, spec.color_scheme)});
    }
    if (grouped) {
      chart.grouped_bar(label, std::move(segments));
    } else {
      chart.stacked_bar(label, std::move(segments));
    }
  }
  result.svg = chart.render(chart_options).str();
  return result;
}

}  // namespace csvzall::charts

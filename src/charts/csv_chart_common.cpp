#include "csv_chart_common.hpp"

#include <svgplot/svgplot.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace csvzall::charts {

std::string Trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

namespace {

std::string ToLowerAscii(std::string_view text) {
  std::string lower;
  lower.reserve(text.size());
  for (const char ch : text) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return lower;
}

}  // namespace

std::optional<std::size_t> FindColumnIndex(const std::vector<std::string>& headers,
                                           std::string_view name,
                                           bool exact_column_matching) {
  for (std::size_t i = 0; i < headers.size(); ++i) {
    if (headers[i] == name) {
      return i;
    }
  }

  if (exact_column_matching) {
    return std::nullopt;
  }

  const std::string wanted = ToLowerAscii(name);
  for (std::size_t i = 0; i < headers.size(); ++i) {
    if (ToLowerAscii(headers[i]) == wanted) {
      return i;
    }
  }

  return std::nullopt;
}

void AccumulateRowBytes(const csv::CSVRow& row, CsvChartResult& result) {
  for (std::size_t i = 0; i < row.size(); ++i) {
    result.bytes_processed += static_cast<std::uint64_t>(row[i].get<std::string>().size());
  }
}

double ParseNumericField(csv::CSVField field,
                         const std::string& column,
                         std::string_view chart_type) {
  if (field.is_null() || field.get<std::string>().empty()) {
    return 0.0;
  }
  long double parsed = 0.0;
  if (!field.try_get(parsed) || !std::isfinite(static_cast<double>(parsed))) {
    throw std::runtime_error("non-numeric " + std::string(chart_type) +
                             " value in column: " + column);
  }
  return static_cast<double>(parsed);
}

std::vector<ChartValueSpec> EffectiveValues(const std::vector<ChartValueSpec>& values,
                                            const std::string& legacy_column) {
  if (!values.empty()) {
    return values;
  }
  if (legacy_column.empty()) {
    return {};
  }
  return {ChartValueSpec{legacy_column, legacy_column, ""}};
}

std::string ValueLabel(const ChartValueSpec& spec) {
  return spec.label.empty() ? spec.column : spec.label;
}

std::string ValueColor(const ChartValueSpec& spec, std::size_t index) {
  return spec.color.empty() ? svgplot::default_series_color(index) : spec.color;
}

}  // namespace csvzall::charts

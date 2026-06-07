#include "csv_chart_common.hpp"

#include <svgplot/svgplot.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace csvzall::charts {
namespace {

using Day = std::chrono::sys_days;

Day ParseIsoDate(std::string_view text) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  char dash1 = '\0';
  char dash2 = '\0';
  std::istringstream input{std::string(text)};
  input >> year >> dash1 >> month >> dash2 >> day;
  if (!input || dash1 != '-' || dash2 != '-') {
    throw std::runtime_error("invalid heatmap date: " + std::string(text));
  }
  const std::chrono::year_month_day ymd{
      std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
  if (!ymd.ok()) {
    throw std::runtime_error("invalid heatmap date: " + std::string(text));
  }
  return Day{ymd};
}

std::string FormatIsoDate(Day day) {
  const auto ymd = std::chrono::year_month_day{day};
  std::ostringstream output;
  output << static_cast<int>(ymd.year()) << '-'
         << std::setw(2) << std::setfill('0') << static_cast<unsigned>(ymd.month()) << '-'
         << std::setw(2) << std::setfill('0') << static_cast<unsigned>(ymd.day());
  return output.str();
}

Day TodayLocalDate() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  return Day{std::chrono::year{local.tm_year + 1900} /
             std::chrono::month{static_cast<unsigned>(local.tm_mon + 1)} /
             std::chrono::day{static_cast<unsigned>(local.tm_mday)}};
}

std::chrono::year_month_day ClampYearOffset(std::chrono::year_month_day ymd, int years) {
  const auto target_year = ymd.year() - std::chrono::years{years};
  auto target = target_year / ymd.month() / ymd.day();
  if (target.ok()) {
    return target;
  }
  const auto last_day = std::chrono::year_month_day_last{
      target_year, std::chrono::month_day_last{ymd.month()}};
  return std::chrono::year_month_day{last_day};
}

Day StartForLookback(Day end, std::string_view raw_lookback) {
  auto lookback = Trim(raw_lookback);
  if (lookback.empty()) {
    throw std::runtime_error("lookback is empty");
  }
  lookback.erase(
      std::remove_if(lookback.begin(), lookback.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t';
      }),
      lookback.end());

  std::size_t pos = 0;
  while (pos < lookback.size() && std::isdigit(static_cast<unsigned char>(lookback[pos])) != 0) {
    ++pos;
  }
  if (pos == 0) {
    throw std::runtime_error("lookback must start with a positive number");
  }
  const auto amount = std::stoi(lookback.substr(0, pos));
  if (amount <= 0) {
    throw std::runtime_error("lookback must be positive");
  }
  std::string unit = lookback.substr(pos);
  std::transform(unit.begin(), unit.end(), unit.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (unit.empty() || unit == "d" || unit == "day" || unit == "days") {
    return end - std::chrono::days{amount};
  }
  if (unit == "y" || unit == "year" || unit == "years") {
    return Day{ClampYearOffset(std::chrono::year_month_day{end}, amount)};
  }
  throw std::runtime_error("lookback unit must be days or years: " + lookback);
}

std::string NormalizedOrientation(std::string_view raw) {
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

std::pair<std::string, std::string> ResolveHeatmapDateRange(const HeatmapSpec& spec) {
  if (spec.lookback.empty()) {
    if (spec.start_date.empty() || spec.end_date.empty()) {
      throw std::runtime_error("start and end dates are required unless lookback is set");
    }
    return {spec.start_date, spec.end_date};
  }
  if (!spec.start_date.empty()) {
    throw std::runtime_error("lookback cannot be combined with start");
  }
  const auto end = spec.end_date.empty() ? TodayLocalDate() : ParseIsoDate(spec.end_date);
  const auto start = StartForLookback(end, spec.lookback);
  return {FormatIsoDate(start), FormatIsoDate(end)};
}

svgplot::CalendarHeatmapOrientation ParseHeatmapOrientation(std::string_view raw) {
  const auto value = NormalizedOrientation(raw);
  if (value.empty() || value == "monthshorizontal" || value == "horizontal") {
    return svgplot::CalendarHeatmapOrientation::MonthsHorizontal;
  }
  if (value == "monthsvertical" || value == "vertical") {
    return svgplot::CalendarHeatmapOrientation::MonthsVertical;
  }
  throw std::runtime_error(
      "orientation must be months-horizontal or months-vertical: " +
      std::string(raw));
}

CsvChartResult RenderHeatmapCsv(csv::CSVReader& reader,
                                const std::vector<std::string>& headers,
                                const HeatmapSpec& spec,
                                const CsvChartOptions& options) {
  const auto date_index =
      FindColumnIndex(headers, spec.date_column, options.exact_column_matching);
  if (!date_index) {
    throw std::runtime_error("date column not found: " + spec.date_column);
  }

  std::optional<std::size_t> value_index;
  const auto value_specs = spec.values;
  if (value_specs.empty() && !spec.value_column.empty()) {
    value_index =
        FindColumnIndex(headers, spec.value_column, options.exact_column_matching);
    if (!value_index) {
      throw std::runtime_error("value column not found: " + spec.value_column);
    }
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

  std::optional<std::size_t> label_index;
  if (!spec.label_column.empty()) {
    label_index =
        FindColumnIndex(headers, spec.label_column, options.exact_column_matching);
    if (!label_index) {
      throw std::runtime_error("label column not found: " + spec.label_column);
    }
  }

  CsvChartResult result;
  std::vector<svgplot::HeatmapCell> cells;
  for (auto& row : reader) {
    const auto date_text = row[*date_index].get<std::string>();
    if (date_text.empty()) {
      throw std::runtime_error("empty date value in column: " + spec.date_column);
    }

    double value = 1.0;
    if (value_index) {
      auto field = row[*value_index];
      if (field.is_null() || field.get<std::string>().empty()) {
        value = 0.0;
      } else {
        long double parsed = 0.0;
        if (!field.try_get(parsed) || !std::isfinite(static_cast<double>(parsed))) {
          throw std::runtime_error("non-numeric heatmap value in column: " +
                                   spec.value_column +
                                   ". Choose no value/weight column for row-count heatmaps "
                                   "unless you need numeric weights such as minutes, volume, or count.");
        }
        value = static_cast<double>(parsed);
      }
    }

    std::string label;
    if (label_index) {
      label = row[*label_index].get<std::string>();
    }

    if (value_specs.empty()) {
      cells.push_back({svgplot::parse_date(date_text), value, std::move(label)});
    } else {
      bool label_used = false;
      for (std::size_t i = 0; i < value_specs.size(); ++i) {
        const auto parsed = ParseNumericField(
            row[value_indices[i]], value_specs[i].column, "heatmap");
        if (parsed <= 0.0) {
          continue;
        }
        cells.push_back({
            svgplot::parse_date(date_text),
            parsed,
            label_used ? std::string{} : label,
            {value_specs[i].column}});
        label_used = true;
      }
    }
    ++result.rows_processed;
    AccumulateRowBytes(row, result);
  }

  svgplot::HeatmapOptions chart_options;
  const auto [start_date, end_date] = ResolveHeatmapDateRange(spec);
  chart_options.title = spec.title;
  chart_options.start_date = svgplot::parse_date(start_date);
  chart_options.end_date = svgplot::parse_date(end_date);
  chart_options.orientation = ParseHeatmapOrientation(spec.orientation);
  if (!value_specs.empty()) {
    chart_options.categories.reserve(value_specs.size());
    for (std::size_t i = 0; i < value_specs.size(); ++i) {
      chart_options.categories.push_back({
          value_specs[i].column,
          ValueLabel(value_specs[i]),
          ValueColor(value_specs[i], i, "diverging")});
    }
  }
  result.svg = svgplot::heatmap_chart(cells, chart_options).str();
  return result;
}

}  // namespace csvzall::charts

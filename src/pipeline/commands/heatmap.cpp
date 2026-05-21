#include "commands.hpp"

#include "../common/chart_spec.hpp"
#include "../common/column_lookup.hpp"
#include "../common/row_utils.hpp"

#include <svgplot/svgplot.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace csvzall::pipeline::commands {
namespace {

using Day = std::chrono::sys_days;

static constexpr std::array<std::string_view, 8> kSeriesColors{
    "#2563eb", "#059669", "#dc2626", "#7c3aed",
    "#ea580c", "#0891b2", "#be123c", "#4b5563"};

std::string Trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

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
  const auto last_day = std::chrono::year_month_day_last{target_year, std::chrono::month_day_last{ymd.month()}};
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

std::pair<std::string, std::string> ResolveHeatmapDateRange(const common::HeatmapSpec& spec) {
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

bool EmitChartValidationError(const LoggerCallbacks& logger, std::string message) {
  if (logger.error) {
    logger.error(std::move(message));
  }
  return false;
}

bool ValidateChartSpecFields(const common::ChartSpec& spec,
                             const LoggerCallbacks& logger) {
  if (spec.id.empty()) {
    return EmitChartValidationError(logger, "charts: chart id is required");
  }
  if (spec.type != "heatmap" && spec.type != "bar" && spec.type != "line") {
    return EmitChartValidationError(logger, "charts: unknown chart type '" + spec.type + "'");
  }
  if (!spec.output) {
    return EmitChartValidationError(
        logger, "charts: chart '" + spec.id + "' requires an output path");
  }
  if (spec.type == "heatmap") {
    if (spec.heatmap.date_column.empty()) {
      return EmitChartValidationError(logger, "heatmap: date column is required");
    }
    try {
      (void)ResolveHeatmapDateRange(spec.heatmap);
    } catch (const std::exception& ex) {
      return EmitChartValidationError(logger, std::string("heatmap: ") + ex.what());
    }
    return true;
  }
  if (spec.type == "bar") {
    if (spec.bar.label_column.empty()) {
      return EmitChartValidationError(logger, "bar: label column is required");
    }
    if (spec.bar.value_column.empty()) {
      return EmitChartValidationError(logger, "bar: value column is required");
    }
    return true;
  }
  if (spec.line.x_column.empty()) {
    return EmitChartValidationError(logger, "line: x column is required");
  }
  if (spec.line.y_column.empty()) {
    return EmitChartValidationError(logger, "line: y column is required");
  }
  return true;
}

class HeatmapCommand : public CsvInputCommand {
public:
  HeatmapCommand(common::HeatmapSpec spec,
                 std::istream& input,
                 std::ostream& output,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats)
      : CsvInputCommand(input, options, logger, stats),
        spec_(std::move(spec)),
        output_(output) {}

protected:
  int run() override {
    try {
      const auto date_index =
          common::FindColumnIndex(headers(), spec_.date_column, options().exact_column_matching);
      if (!date_index) {
        throw std::runtime_error("date column not found: " + spec_.date_column);
      }

      std::optional<std::size_t> value_index;
      if (!spec_.value_column.empty()) {
        value_index =
            common::FindColumnIndex(headers(), spec_.value_column, options().exact_column_matching);
        if (!value_index) {
          throw std::runtime_error("value column not found: " + spec_.value_column);
        }
      }

      std::optional<std::size_t> label_index;
      if (!spec_.label_column.empty()) {
        label_index =
            common::FindColumnIndex(headers(), spec_.label_column, options().exact_column_matching);
        if (!label_index) {
          throw std::runtime_error("label column not found: " + spec_.label_column);
        }
      }

      std::vector<svgplot::HeatmapCell> cells;
      for (auto& row : reader()) {
        const auto date_text = row[*date_index].get<std::string>();
        if (date_text.empty()) {
          throw std::runtime_error("empty date value in column: " + spec_.date_column);
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
                                       spec_.value_column +
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

        cells.push_back({svgplot::parse_date(date_text), value, std::move(label)});
        ++stats().rows_processed;
        common::AccumulateRowBytes(row, stats());
      }

      svgplot::HeatmapOptions chart_options;
      const auto [start_date, end_date] = ResolveHeatmapDateRange(spec_);
      chart_options.title = spec_.title;
      chart_options.start_date = svgplot::parse_date(start_date);
      chart_options.end_date = svgplot::parse_date(end_date);
      output_ << svgplot::heatmap_chart(cells, chart_options).str() << '\n';
      return 0;
    } catch (const std::exception& ex) {
      if (logger().error) {
        logger().error(std::string("heatmap: ") + ex.what());
      }
      return 1;
    }
  }

private:
  common::HeatmapSpec spec_;
  std::ostream& output_;
};

class BarCommand : public CsvInputCommand {
public:
  BarCommand(common::BarSpec spec,
             std::istream& input,
             std::ostream& output,
             const RunOptions& options,
             const LoggerCallbacks& logger,
             RunStats& stats)
      : CsvInputCommand(input, options, logger, stats),
        spec_(std::move(spec)),
        output_(output) {}

protected:
  int run() override {
    try {
      const auto label_index =
          common::FindColumnIndex(headers(), spec_.label_column, options().exact_column_matching);
      if (!label_index) {
        throw std::runtime_error("label column not found: " + spec_.label_column);
      }
      const auto value_index =
          common::FindColumnIndex(headers(), spec_.value_column, options().exact_column_matching);
      if (!value_index) {
        throw std::runtime_error("value column not found: " + spec_.value_column);
      }

      std::vector<std::string> order;
      std::map<std::string, double> totals;
      for (auto& row : reader()) {
        const auto label = row[*label_index].get<std::string>();
        if (!totals.contains(label)) {
          order.push_back(label);
        }
        totals[label] += ParseNumericField(row[*value_index], spec_.value_column, "bar");
        ++stats().rows_processed;
        common::AccumulateRowBytes(row, stats());
      }

      std::vector<svgplot::Bar> bars;
      bars.reserve(order.size());
      for (std::size_t i = 0; i < order.size(); ++i) {
        bars.push_back({
            order[i],
            totals[order[i]],
            std::string(kSeriesColors[i % kSeriesColors.size()])});
      }

      svgplot::ChartOptions chart_options;
      chart_options.title = spec_.title;
      chart_options.x_label = spec_.x_label;
      chart_options.y_label = spec_.y_label;
      output_ << svgplot::bar_chart(bars, chart_options).str() << '\n';
      return 0;
    } catch (const std::exception& ex) {
      if (logger().error) {
        logger().error(std::string("bar: ") + ex.what());
      }
      return 1;
    }
  }

private:
  common::BarSpec spec_;
  std::ostream& output_;
};

class LineCommand : public CsvInputCommand {
public:
  LineCommand(common::LineSpec spec,
              std::istream& input,
              std::ostream& output,
              const RunOptions& options,
              const LoggerCallbacks& logger,
              RunStats& stats)
      : CsvInputCommand(input, options, logger, stats),
        spec_(std::move(spec)),
        output_(output) {}

protected:
  int run() override {
    try {
      const auto x_index =
          common::FindColumnIndex(headers(), spec_.x_column, options().exact_column_matching);
      if (!x_index) {
        throw std::runtime_error("x column not found: " + spec_.x_column);
      }
      const auto y_index =
          common::FindColumnIndex(headers(), spec_.y_column, options().exact_column_matching);
      if (!y_index) {
        throw std::runtime_error("y column not found: " + spec_.y_column);
      }

      std::optional<std::size_t> series_index;
      if (!spec_.series_column.empty()) {
        series_index =
            common::FindColumnIndex(headers(), spec_.series_column, options().exact_column_matching);
        if (!series_index) {
          throw std::runtime_error("series column not found: " + spec_.series_column);
        }
      }

      std::vector<std::string> order;
      std::map<std::string, std::vector<svgplot::Point>> points_by_series;
      for (auto& row : reader()) {
        const auto series = series_index
            ? row[*series_index].get<std::string>()
            : std::string{"Series"};
        if (!points_by_series.contains(series)) {
          order.push_back(series);
        }
        points_by_series[series].push_back({
            ParseNumericField(row[*x_index], spec_.x_column, "line x"),
            ParseNumericField(row[*y_index], spec_.y_column, "line y")});
        ++stats().rows_processed;
        common::AccumulateRowBytes(row, stats());
      }

      std::vector<svgplot::Series> series;
      series.reserve(order.size());
      for (std::size_t i = 0; i < order.size(); ++i) {
        auto& points = points_by_series[order[i]];
        std::sort(points.begin(), points.end(), [](const auto& lhs, const auto& rhs) {
          return lhs.x < rhs.x;
        });
        series.push_back({order[i], std::move(points), std::string(kSeriesColors[i % kSeriesColors.size()])});
      }

      svgplot::ChartOptions chart_options;
      chart_options.title = spec_.title;
      chart_options.x_label = spec_.x_label;
      chart_options.y_label = spec_.y_label;
      output_ << svgplot::line_chart(series, chart_options).str() << '\n';
      return 0;
    } catch (const std::exception& ex) {
      if (logger().error) {
        logger().error(std::string("line: ") + ex.what());
      }
      return 1;
    }
  }

private:
  common::LineSpec spec_;
  std::ostream& output_;
};

int RenderChartToStream(const common::ChartSpec& spec,
                        std::istream& input,
                        std::ostream& output,
                        const RunOptions& options,
                        const LoggerCallbacks& logger,
                        RunStats& stats) {
  if (spec.type == "heatmap") {
    return RunHeatmap(spec.heatmap, input, output, options, logger, stats);
  }
  if (spec.type == "bar") {
    BarCommand cmd(spec.bar, input, output, options, logger, stats);
    return cmd.execute();
  }
  if (spec.type == "line") {
    LineCommand cmd(spec.line, input, output, options, logger, stats);
    return cmd.execute();
  }
  if (logger.error) {
    logger.error("charts: unknown chart type '" + spec.type + "'");
  }
  return 1;
}

}  // namespace

int RunHeatmap(const std::string& date_column,
               const std::string& value_column,
               const std::string& label_column,
               const std::string& start_date,
               const std::string& end_date,
               const std::string& title,
               std::istream& input,
               std::ostream& output,
               const RunOptions& options,
               const LoggerCallbacks& logger,
               RunStats& stats) {
  common::HeatmapSpec spec;
  spec.date_column = date_column;
  spec.value_column = value_column;
  spec.label_column = label_column;
  spec.start_date = start_date;
  spec.end_date = end_date;
  spec.title = title;
  return RunHeatmap(spec, input, output, options, logger, stats);
}

int RunHeatmap(const common::HeatmapSpec& spec,
               std::istream& input,
               std::ostream& output,
               const RunOptions& options,
               const LoggerCallbacks& logger,
               RunStats& stats) {
  HeatmapCommand cmd(spec, input, output, options, logger, stats);
  return cmd.execute();
}

int RunChart(const common::ChartSpec& spec,
             const RunOptions& options,
             const LoggerCallbacks& logger,
             RunStats& stats) {
  if (!ValidateChartSpecFields(spec, logger)) {
    return 1;
  }

  std::ifstream input(spec.input, std::ios::binary);
  if (!input.is_open()) {
    if (logger.error) {
      logger.error("charts: missing input file for chart '" + spec.id +
                   "': " + spec.input.string());
    }
    return 1;
  }

  const auto parent = spec.output->parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      if (logger.error) {
        logger.error("charts: unable to create output directory for chart '" + spec.id +
                     "': " + parent.string() + ": " + ec.message());
      }
      return 1;
    }
  }

  std::ofstream output(*spec.output, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    if (logger.error) {
      logger.error("charts: unable to open output file for chart '" + spec.id +
                   "': " + spec.output->string());
    }
    return 1;
  }

  RunOptions chart_options = options;
  chart_options.input_is_stdin = false;
  chart_options.input_path = spec.input.string();
  const auto rc = RenderChartToStream(spec, input, output, chart_options, logger, stats);
  output.flush();
  if (rc == 0 && !output.good()) {
    if (logger.error) {
      logger.error("charts: failed to write output file for chart '" + spec.id +
                   "': " + spec.output->string());
    }
    return 1;
  }
  return rc;
}

int ValidateChart(const common::ChartSpec& spec,
                  const RunOptions& options,
                  const LoggerCallbacks& logger,
                  RunStats& stats) {
  if (!ValidateChartSpecFields(spec, logger)) {
    return 1;
  }

  std::ifstream input(spec.input, std::ios::binary);
  if (!input.is_open()) {
    if (logger.error) {
      logger.error("charts: missing input file for chart '" + spec.id +
                   "': " + spec.input.string());
    }
    return 1;
  }

  RunOptions chart_options = options;
  chart_options.input_is_stdin = false;
  chart_options.input_path = spec.input.string();
  std::ostringstream output;
  return RenderChartToStream(spec, input, output, chart_options, logger, stats);
}

}  // namespace csvzall::pipeline::commands

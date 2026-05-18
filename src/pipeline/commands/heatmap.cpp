#include "commands.hpp"

#include "../common/chart_spec.hpp"
#include "../common/column_lookup.hpp"
#include "../common/row_utils.hpp"

#include <svgplot/svgplot.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
                                       spec_.value_column);
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
  if (spec.type != "heatmap") {
    if (logger.error) {
      logger.error("charts: unknown chart type '" + spec.type + "'");
    }
    return 1;
  }
  if (!spec.output) {
    if (logger.error) {
      logger.error("charts: chart '" + spec.id + "' requires an output path");
    }
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
  const auto rc = RunHeatmap(spec.heatmap, input, output, chart_options, logger, stats);
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

}  // namespace csvzall::pipeline::commands

#include "markdown_calendar.hpp"

#include "column_lookup.hpp"
#include "gzip_stream.hpp"
#include "row_utils.hpp"

#include <csv.hpp>

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace csvzall::pipeline::common {
namespace {

using Day = std::chrono::sys_days;

Day ParseIsoDate(const std::string& text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
    throw std::runtime_error("Expected ISO date YYYY-MM-DD: " + text);
  }
  const int year = std::stoi(text.substr(0, 4));
  const unsigned month = static_cast<unsigned>(std::stoi(text.substr(5, 2)));
  const unsigned day = static_cast<unsigned>(std::stoi(text.substr(8, 2)));
  const std::chrono::year_month_day ymd{
      std::chrono::year{year},
      std::chrono::month{month},
      std::chrono::day{day}};
  if (!ymd.ok()) {
    throw std::runtime_error("Invalid ISO date: " + text);
  }
  return Day{ymd};
}

std::string MonthName(unsigned month) {
  static constexpr std::array<std::string_view, 12> names{
      "January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December"};
  return std::string(names.at(month - 1));
}

std::string EscapeMarkdownTableCell(std::string value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (ch == '|') {
      out += "\\|";
    } else if (ch == '\r') {
      continue;
    } else if (ch == '\n') {
      out += "<br>";
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

std::string DefaultMonthHeader(int year, unsigned month) {
  return MonthName(month) + " " + std::to_string(year);
}

std::string CellText(const std::map<Day, std::string>& content_by_date, Day day) {
  const auto ymd = std::chrono::year_month_day{day};
  const auto day_number = static_cast<unsigned>(ymd.day());
  auto it = content_by_date.find(day);
  if (it == content_by_date.end() || it->second.empty()) {
    return std::to_string(day_number);
  }
  return std::to_string(day_number) + "<br>" + EscapeMarkdownTableCell(it->second);
}

csv::CSVFormat MakeFormat(const RunOptions& options) {
  csv::CSVFormat format;
  if (options.delimiter) {
    format.delimiter(*options.delimiter);
  } else {
    format.delimiter({',', '|', '\t', ';', '^'});
  }
  format.quote('"').header_row(0);
  return format;
}

}  // namespace

int RenderMarkdownCalendarCsv(std::istream& input,
                              std::ostream& output,
                              const RunOptions& options,
                              const LoggerCallbacks& logger,
                              RunStats& stats,
                              const MarkdownCalendarOptions& calendar_options) {
  try {
    const Day start = ParseIsoDate(calendar_options.start_date);
    const Day end = ParseIsoDate(calendar_options.end_date);
    if (end < start) {
      throw std::runtime_error("Calendar end date must be on or after start date");
    }

    std::unique_ptr<std::istringstream> buffered_input;
    std::istream* parse_input = &input;
    if (options.input_is_stdin) {
      std::ostringstream raw;
      raw << input.rdbuf();
      buffered_input = std::make_unique<std::istringstream>(raw.str());
      parse_input = buffered_input.get();
    }

    csv::CSVReader reader =
        (!options.input_is_stdin && !options.input_path.empty() && options.input_path != "-")
            ? OpenCsvReader(options.input_path, options.zip_entry, MakeFormat(options))
            : csv::CSVReader(*parse_input, MakeFormat(options));
    const auto headers = reader.get_col_names();
    const auto date_index = FindColumnIndex(headers, "date", true);
    const auto content_index = FindColumnIndex(headers, "content", true);
    if (!date_index || !content_index) {
      throw std::runtime_error("Calendar CSV requires date and content columns");
    }

    std::map<Day, std::string> content_by_date;
    for (auto& row : reader) {
      const auto date_text = row[*date_index].get<std::string>();
      const auto day = ParseIsoDate(date_text);
      if (day >= start && day <= end) {
        const auto inserted =
            content_by_date.emplace(day, row[*content_index].get<std::string>());
        if (!inserted.second) {
          throw std::runtime_error("Duplicate calendar date: " + date_text);
        }
      }
      ++stats.rows_processed;
      AccumulateRowBytes(row, stats);
    }

    const auto start_ymd = std::chrono::year_month_day{start};
    const auto end_ymd = std::chrono::year_month_day{end};
    auto current_month = std::chrono::year_month{
        start_ymd.year(), start_ymd.month()};
    const auto last_month = std::chrono::year_month{
        end_ymd.year(), end_ymd.month()};

    bool first_month = true;
    while (current_month <= last_month) {
      if (!first_month) {
        output << '\n';
      }
      first_month = false;

      const int year = static_cast<int>(current_month.year());
      const unsigned month = static_cast<unsigned>(current_month.month());
      const std::string header = calendar_options.month_header
          ? calendar_options.month_header(year, month)
          : DefaultMonthHeader(year, month);
      output << "### " << header << "\n\n";
      output << "| Sun | Mon | Tue | Wed | Thu | Fri | Sat |\n";
      output << "| --- | --- | --- | --- | --- | --- | --- |\n";

      const Day first_day{std::chrono::year_month_day{
          current_month.year(), current_month.month(), std::chrono::day{1}}};
      const auto next_month = current_month + std::chrono::months{1};
      const Day next_month_first{std::chrono::year_month_day{
          next_month.year(), next_month.month(), std::chrono::day{1}}};
      const Day last_day = next_month_first - std::chrono::days{1};
      const unsigned first_weekday =
          std::chrono::weekday{first_day}.c_encoding();

      Day cursor = first_day - std::chrono::days{first_weekday};
      while (cursor <= last_day ||
             std::chrono::weekday{cursor}.c_encoding() != 0) {
        output << '|';
        for (unsigned i = 0; i < 7; ++i) {
          const auto cursor_ymd = std::chrono::year_month_day{cursor};
          const bool in_month = cursor_ymd.month() == current_month.month();
          const bool in_range = cursor >= start && cursor <= end;
          output << ' ';
          if (in_month && in_range) {
            output << CellText(content_by_date, cursor);
          }
          output << " |";
          cursor += std::chrono::days{1};
        }
        output << '\n';
      }

      current_month += std::chrono::months{1};
    }

    return 0;
  } catch (const std::exception& ex) {
    if (logger.error) {
      logger.error(std::string("calendar: ") + ex.what());
    }
    return 1;
  }
}

}  // namespace csvzall::pipeline::common

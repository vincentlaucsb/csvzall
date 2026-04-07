#pragma once

#include <csv.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace csvzall::head {

struct Result {
  std::uint64_t rows_processed = 0;
  std::uint64_t bytes_processed = 0;
};

inline std::string SanitizeCell(std::string value) {
  for (char& ch : value) {
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      ch = ' ';
    }
  }
  return value;
}

inline std::string TruncateCell(std::string_view value, std::size_t width) {
  if (value.size() <= width) {
    return std::string(value);
  }

  if (width <= 3) {
    return std::string(width, '.');
  }

  return std::string(value.substr(0, width - 3)) + "...";
}

inline void PrintSeparator(const std::vector<std::size_t>& widths, std::ostream& out) {
  out << '+';
  for (const auto width : widths) {
    out << std::string(width + 2, '-') << '+';
  }
  out << '\n';
}

inline void PrintTableRow(const std::vector<std::string>& values, const std::vector<std::size_t>& widths,
                          std::ostream& out) {
  out << '|';
  for (std::size_t i = 0; i < widths.size(); ++i) {
    std::string value;
    if (i < values.size()) {
      value = TruncateCell(values[i], widths[i]);
    }
    out << ' ' << std::left << std::setw(static_cast<int>(widths[i])) << value << " |";
  }
  out << '\n';
}

template <typename LoggerT>
int Run(std::size_t row_limit, std::istream& input, std::ostream& output, bool input_is_stdin,
        LoggerT& logger, Result& result) {
  try {
    std::unique_ptr<std::istringstream> buffered_stdin;
    std::istream* parse_input = &input;
    if (input_is_stdin) {
      std::ostringstream raw;
      raw << input.rdbuf();
      buffered_stdin = std::make_unique<std::istringstream>(raw.str());
      parse_input = buffered_stdin.get();
      logger.Verbose("Buffered stdin for CSV parsing.");
    }

    csv::CSVFormat format;
    format.delimiter(',').quote('"').header_row(0);
    csv::CSVReader reader(*parse_input, format);

    const auto headers = reader.get_col_names();
    if (headers.empty()) {
      logger.Error("Input appears to have no header row.");
      return 1;
    }

    std::vector<std::vector<std::string>> rows;
    rows.reserve(row_limit);

    csv::CSVRow row;
    while (rows.size() < row_limit && reader.read_row(row)) {
      std::vector<std::string> values;
      values.reserve(headers.size());

      for (std::size_t i = 0; i < headers.size(); ++i) {
        if (i < row.size()) {
          auto cell = row[i].get<std::string>();
          result.bytes_processed += static_cast<std::uint64_t>(cell.size());
          values.push_back(SanitizeCell(std::move(cell)));
        } else {
          values.emplace_back("");
        }
      }

      rows.push_back(std::move(values));
      result.rows_processed++;
    }

    std::vector<std::size_t> widths(headers.size(), 0);
    constexpr std::size_t kMaxColumnWidth = 40;

    for (std::size_t i = 0; i < headers.size(); ++i) {
      widths[i] = std::min(headers[i].size(), kMaxColumnWidth);
    }

    for (const auto& values : rows) {
      for (std::size_t i = 0; i < values.size(); ++i) {
        widths[i] = std::max(widths[i], std::min(values[i].size(), kMaxColumnWidth));
      }
    }

    PrintSeparator(widths, output);
    PrintTableRow(headers, widths, output);
    PrintSeparator(widths, output);
    for (const auto& values : rows) {
      PrintTableRow(values, widths, output);
    }
    PrintSeparator(widths, output);

    logger.Verbose("Displayed " + std::to_string(rows.size()) + " row(s) plus header.");
    return 0;
  } catch (const std::exception& ex) {
    logger.Error(std::string("Failed to parse CSV for head: ") + ex.what());
    return 1;
  }
}

}  // namespace csvzall::head

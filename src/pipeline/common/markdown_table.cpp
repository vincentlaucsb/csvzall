#include "markdown_table.hpp"

#include <algorithm>
#include <ostream>
#include <utility>

namespace csvzall::pipeline::common {

std::string EscapeMarkdownTableCell(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    if (c == '\\') {
      escaped += "\\\\";
    } else if (c == '|') {
      escaped += "\\|";
    } else if (c == '\n') {
      escaped += "<br>";
    } else if (c != '\r') {
      escaped += c;
    }
  }
  return escaped;
}

void WriteMarkdownTable(std::ostream& output,
                        const std::vector<std::string>& headers,
                        const std::vector<std::vector<std::string>>& rows) {
  std::vector<std::string> escaped_headers;
  escaped_headers.reserve(headers.size());
  for (const auto& header : headers) {
    escaped_headers.push_back(EscapeMarkdownTableCell(header));
  }

  std::vector<std::vector<std::string>> escaped_rows;
  escaped_rows.reserve(rows.size());
  for (const auto& row : rows) {
    std::vector<std::string> escaped_row;
    escaped_row.reserve(row.size());
    for (const auto& cell : row) {
      escaped_row.push_back(EscapeMarkdownTableCell(cell));
    }
    escaped_rows.push_back(std::move(escaped_row));
  }

  std::vector<std::size_t> widths(escaped_headers.size(), 0);
  for (std::size_t i = 0; i < escaped_headers.size(); ++i) {
    widths[i] = escaped_headers[i].size();
  }
  for (const auto& row : escaped_rows) {
    for (std::size_t i = 0; i < row.size() && i < widths.size(); ++i) {
      widths[i] = std::max(widths[i], row[i].size());
    }
  }

  auto write_row = [&](const std::vector<std::string>& cells) {
    output << '|';
    for (std::size_t i = 0; i < widths.size(); ++i) {
      const std::string empty;
      const std::string& cell = i < cells.size() ? cells[i] : empty;
      output << ' ' << cell;
      for (std::size_t p = cell.size(); p < widths[i]; ++p) {
        output << ' ';
      }
      output << " |";
    }
    output << '\n';
  };

  write_row(escaped_headers);
  output << '|';
  for (const auto width : widths) {
    output << ' ';
    for (std::size_t p = 0; p < std::max<std::size_t>(width, 3); ++p) {
      output << '-';
    }
    output << " |";
  }
  output << '\n';

  for (const auto& row : escaped_rows) {
    write_row(row);
  }
}

}  // namespace csvzall::pipeline::common

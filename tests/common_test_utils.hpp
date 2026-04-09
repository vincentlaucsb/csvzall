#pragma once

#include <string>
#include <sstream>
#include <vector>

namespace csvzall::tests {

// Helper to create test CSV data from headers and rows
inline std::string MakeTestCsv(const std::vector<std::string>& headers,
                               const std::vector<std::vector<std::string>>& rows) {
  std::ostringstream oss;
  
  // Write header
  for (std::size_t i = 0; i < headers.size(); ++i) {
    if (i > 0) oss << ',';
    oss << headers[i];
  }
  oss << '\n';
  
  // Write rows
  for (const auto& row : rows) {
    for (std::size_t i = 0; i < row.size(); ++i) {
      if (i > 0) oss << ',';
      // Simple quote handling for CSV
      if (row[i].find(',') != std::string::npos || row[i].find('"') != std::string::npos) {
        oss << '"' << row[i] << '"';
      } else {
        oss << row[i];
      }
    }
    oss << '\n';
  }
  
  return oss.str();
}

// Helper to extract CSV cells from output (basic, assumes no quoted commas)
inline std::vector<std::vector<std::string>> ParseCsv(const std::string& csv_text) {
  std::vector<std::vector<std::string>> result;
  std::istringstream stream(csv_text);
  std::string line;
  
  while (std::getline(stream, line)) {
    if (line.empty()) continue;
    
    std::vector<std::string> row;
    std::istringstream line_stream(line);
    std::string cell;
    
    while (std::getline(line_stream, cell, ',')) {
      // Trim quotes if present
      if (!cell.empty() && cell.front() == '"' && cell.back() == '"') {
        cell = cell.substr(1, cell.size() - 2);
      }
      row.push_back(cell);
    }
    
    if (!row.empty()) {
      result.push_back(row);
    }
  }
  
  return result;
}

}  // namespace csvzall::tests

#include "support.hpp"

#include <random>

namespace csvzall::pipeline::commands::view_internal {
csv::CSVFormat MakeViewFormat(const RunOptions& options) {
  csv::CSVFormat format;
  if (options.delimiter) {
    format.delimiter(*options.delimiter);
  } else {
    format.delimiter({',', '|', '\t', ';', '^'});
  }
  format.quote('"').header_row(0);
  return format;
}

std::string GenerateSessionToken() {
  std::random_device rd;
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  static constexpr char hex[] = "0123456789abcdef";

  std::string token;
  token.reserve(32);
  for (int i = 0; i < 16; ++i) {
    const auto byte = dist(rd);
    token.push_back(hex[(byte >> 4) & 0x0f]);
    token.push_back(hex[byte & 0x0f]);
  }
  return token;
}
}  // namespace csvzall::pipeline::commands::view_internal
#include "column_lookup.hpp"

#include <cctype>

namespace csvzall::pipeline::common {

std::string Trim(std::string value) {
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }

  std::size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
    --last;
  }

  return value.substr(first, last - first);
}

bool IsIdentifier(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  if (!(std::isalpha(static_cast<unsigned char>(text[0])) != 0 || text[0] == '_')) {
    return false;
  }
  for (char ch : text) {
    if (!(std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_')) {
      return false;
    }
  }
  return true;
}

std::string ToLowerAscii(std::string_view text) {
  std::string lower;
  lower.reserve(text.size());
  for (char ch : text) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return lower;
}

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

}  // namespace csvzall::pipeline::common

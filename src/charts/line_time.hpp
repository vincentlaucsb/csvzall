#pragma once

#include "../pipeline/common/flexible_date.hpp"

#include <chrono>
#include <optional>
#include <string_view>

namespace csvzall::charts {

// ISO calendar dates and timestamps, expressed in UTC days. Zone-less times
// are UTC too, so chart positions never depend on the viewing device's zone.
inline std::optional<double> ParseLineDate(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return std::nullopt;
  text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
  if (text.size() < 10 || text[4] != '-' || text[7] != '-') return std::nullopt;
  const auto day = pipeline::common::ParseFlexibleDate(
      text.substr(0, 10), pipeline::common::DateOrder::MonthDay);
  if (!day) return std::nullopt;
  const auto days = static_cast<double>(day->time_since_epoch().count());
  if (text.size() == 10) return days;
  if (text[10] != 'T' && text[10] != ' ') return std::nullopt;
  std::size_t pos = 11;
  const auto two_digits = [&]() -> std::optional<int> {
    if (pos + 2 > text.size() || text[pos] < '0' || text[pos] > '9' ||
        text[pos + 1] < '0' || text[pos + 1] > '9') return std::nullopt;
    const auto value = (text[pos] - '0') * 10 + text[pos + 1] - '0';
    pos += 2;
    return value;
  };
  const auto hour = two_digits();
  if (!hour || *hour > 23 || pos >= text.size() || text[pos++] != ':') return std::nullopt;
  const auto minute = two_digits();
  if (!minute || *minute > 59) return std::nullopt;
  double seconds = *hour * 3600.0 + *minute * 60.0;
  if (pos < text.size() && text[pos] == ':') {
    ++pos;
    const auto second = two_digits();
    if (!second || *second > 59) return std::nullopt;
    seconds += *second;
    if (pos < text.size() && text[pos] == '.') {
      ++pos;
      const auto start = pos;
      double place = 0.1;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        seconds += (text[pos++] - '0') * place;
        place *= 0.1;
      }
      if (pos == start) return std::nullopt;
    }
  }
  if (pos < text.size() && text[pos] == 'Z') {
    ++pos;
  } else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
    const int sign = text[pos++] == '+' ? 1 : -1;
    const auto zone_hour = two_digits();
    if (!zone_hour || *zone_hour > 23 || pos >= text.size() || text[pos++] != ':') return std::nullopt;
    const auto zone_minute = two_digits();
    if (!zone_minute || *zone_minute > 59) return std::nullopt;
    seconds -= sign * (*zone_hour * 3600.0 + *zone_minute * 60.0);
  }
  if (pos != text.size()) return std::nullopt;
  return days + seconds / 86400.0;
}

}  // namespace csvzall::charts

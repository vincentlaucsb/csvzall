#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace csvzall::pipeline::common {

// How to read an all-numeric date whose year comes last, such as 6/9/2026.
enum class DateOrder {
  MonthDay,  // M/D/YYYY (American)
  DayMonth,  // D/M/YYYY
};

enum class DatePrecision {
  Day,
  Month,
  Year,
};

// Result of inspecting one value for column-wide month/day order inference.
enum class DateOrderClue {
  None,      // Year-first, or both leading parts <= 12: no ordering evidence.
  MonthDay,  // Second part > 12, so the value must be M/D/YYYY.
  DayMonth,  // First part > 12, so the value must be D/M/YYYY.
  Invalid,   // Not a full date shape this parser understands.
};

// A calendar date that may be known only to the month or year. Unknown
// components are 1, so the date is the first day of its precision period.
struct FlexibleDate {
  int year = 0;
  unsigned month = 1;
  unsigned day = 1;
  DatePrecision precision = DatePrecision::Day;
};

// Parses a full date: YYYY-MM-DD, M/D/YYYY, M-D-YYYY, D/M/YYYY, or D-M-YYYY.
// Either '-' or '/' separates parts, and surrounding whitespace is ignored.
// When the year is last, a first part > 12 forces D/M and a second part > 12
// forces M/D; otherwise `order` decides. Returns nullopt for anything else or
// for impossible dates such as 2/30/2026.
std::optional<std::chrono::sys_days> ParseFlexibleDate(std::string_view text,
                                                       DateOrder order);

DateOrderClue FindDateOrderClue(std::string_view text);

// Parses full dates like ParseFlexibleDate, plus partial dates for
// hand-maintained data:
//   month precision: 6/2026, 2026-06, 4/??/2026, 2026-04-??
//   year precision:  2026, ??/??/2026, 2026-??
// A run of '?' marks an unknown part. In a year-last value with one unknown
// part, the known part is the month regardless of `order`. A year-first value
// may carry a time suffix after 'T' or a space (2026-05-26T10:00:00), which is
// ignored. Returns nullopt when the text is not a date.
std::optional<FlexibleDate> ParsePartialDate(std::string_view text, DateOrder order);

std::string FormatIsoDate(std::chrono::sys_days day);

// Formats the first day of the date's precision period as YYYY-MM-DD.
std::string FormatIsoDate(const FlexibleDate& date);

std::string_view DatePrecisionName(DatePrecision precision);

}  // namespace csvzall::pipeline::common

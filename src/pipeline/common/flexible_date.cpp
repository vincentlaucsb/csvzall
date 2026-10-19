#include "flexible_date.hpp"

#include <array>
#include <cstdio>

namespace csvzall::pipeline::common {
namespace {

using Day = std::chrono::sys_days;

// Longer digit runs cannot be a day, month, or year and could overflow.
constexpr std::size_t kMaxDigitsPerPart = 9;

struct DatePart {
  std::string_view text;
  bool unknown = false;
  unsigned value = 0;
};

struct SplitDate {
  std::array<DatePart, 3> parts{};
  std::size_t count = 0;
};

std::string_view TrimDateText(std::string_view text) {
  constexpr std::string_view kWhitespace = " \t\r\n";
  const auto first = text.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(kWhitespace);
  return text.substr(first, last - first + 1);
}

bool IsDigit(char ch) {
  return ch >= '0' && ch <= '9';
}

std::optional<DatePart> ParsePart(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  DatePart part;
  part.text = text;
  if (text.find_first_not_of('?') == std::string_view::npos) {
    part.unknown = true;
    return part;
  }
  if (text.size() > kMaxDigitsPerPart) {
    return std::nullopt;
  }
  for (const char ch : text) {
    if (!IsDigit(ch)) {
      return std::nullopt;
    }
    part.value = part.value * 10 + static_cast<unsigned>(ch - '0');
  }
  return part;
}

// Splits on '-' or '/' into one to three parts.
std::optional<SplitDate> Split(std::string_view text) {
  SplitDate split;
  std::size_t start = 0;
  while (true) {
    if (split.count == split.parts.size()) {
      return std::nullopt;
    }
    const auto separator = text.find_first_of("-/", start);
    const auto piece = separator == std::string_view::npos
                           ? text.substr(start)
                           : text.substr(start, separator - start);
    const auto part = ParsePart(piece);
    if (!part) {
      return std::nullopt;
    }
    split.parts[split.count++] = *part;
    if (separator == std::string_view::npos) {
      return split;
    }
    start = separator + 1;
  }
}

bool IsYear(const DatePart& part) {
  return !part.unknown && part.text.size() == 4;
}

bool AllKnown(const SplitDate& split) {
  for (std::size_t i = 0; i < split.count; ++i) {
    if (split.parts[i].unknown) {
      return false;
    }
  }
  return true;
}

std::optional<Day> MakeDay(unsigned year, unsigned month, unsigned day) {
  if (month < 1 || month > 12 || day < 1 || day > 31) {
    return std::nullopt;
  }
  const std::chrono::year_month_day ymd{
      std::chrono::year{static_cast<int>(year)}, std::chrono::month{month},
      std::chrono::day{day}};
  if (!ymd.ok()) {
    return std::nullopt;
  }
  return Day{ymd};
}

// Full numeric date from exactly three known parts.
std::optional<Day> FullDate(const SplitDate& split, DateOrder order) {
  if (split.count != 3 || !AllKnown(split)) {
    return std::nullopt;
  }
  const auto& [first, second, third] = split.parts;
  if (first.text.size() == 4) {
    return MakeDay(first.value, second.value, third.value);
  }
  if (third.text.size() != 4) {
    return std::nullopt;
  }
  if (first.value > 12 && second.value <= 12) {
    return MakeDay(third.value, second.value, first.value);
  }
  if (second.value > 12 && first.value <= 12) {
    return MakeDay(third.value, first.value, second.value);
  }
  if (order == DateOrder::DayMonth) {
    return MakeDay(third.value, second.value, first.value);
  }
  return MakeDay(third.value, first.value, second.value);
}

std::optional<FlexibleDate> MonthDate(unsigned year, const DatePart& month) {
  if (!MakeDay(year, month.value, 1)) {
    return std::nullopt;
  }
  return FlexibleDate{static_cast<int>(year), month.value, 1, DatePrecision::Month};
}

FlexibleDate YearDate(unsigned year) {
  return FlexibleDate{static_cast<int>(year), 1, 1, DatePrecision::Year};
}

FlexibleDate DayDate(Day day) {
  const std::chrono::year_month_day ymd{day};
  return FlexibleDate{static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
                      static_cast<unsigned>(ymd.day()), DatePrecision::Day};
}

// Drops a time-of-day suffix from year-first values: 2026-05-26T10:00:00.
std::string_view StripTimeSuffix(std::string_view text) {
  if (text.size() < 5 || !IsDigit(text[0]) || !IsDigit(text[1]) || !IsDigit(text[2]) ||
      !IsDigit(text[3]) || (text[4] != '-' && text[4] != '/')) {
    return text;
  }
  const auto cut = text.find_first_of("T ");
  if (cut == std::string_view::npos || cut + 1 >= text.size() || !IsDigit(text[cut + 1])) {
    return text;
  }
  return text.substr(0, cut);
}

}  // namespace

std::optional<std::chrono::sys_days> ParseFlexibleDate(std::string_view text,
                                                       DateOrder order) {
  const auto split = Split(TrimDateText(text));
  if (!split) {
    return std::nullopt;
  }
  return FullDate(*split, order);
}

DateOrderClue FindDateOrderClue(std::string_view text) {
  const auto split = Split(TrimDateText(text));
  if (!split || split->count != 3 || !AllKnown(*split)) {
    return DateOrderClue::Invalid;
  }
  const auto& [first, second, third] = split->parts;
  if (first.text.size() == 4) {
    return DateOrderClue::None;
  }
  if (third.text.size() != 4) {
    return DateOrderClue::Invalid;
  }
  if (first.value > 12 && second.value <= 12) {
    return DateOrderClue::DayMonth;
  }
  if (second.value > 12 && first.value <= 12) {
    return DateOrderClue::MonthDay;
  }
  return DateOrderClue::None;
}

std::optional<FlexibleDate> ParsePartialDate(std::string_view text, DateOrder order) {
  const auto trimmed = StripTimeSuffix(TrimDateText(text));
  if (trimmed.empty()) {
    return std::nullopt;
  }
  const auto split = Split(trimmed);
  if (!split) {
    return std::nullopt;
  }
  const auto& parts = split->parts;

  switch (split->count) {
    case 1:
      if (IsYear(parts[0])) {
        return YearDate(parts[0].value);
      }
      return std::nullopt;

    case 2: {
      const DatePart* year = nullptr;
      const DatePart* month = nullptr;
      if (IsYear(parts[0])) {
        year = &parts[0];
        month = &parts[1];
      } else if (IsYear(parts[1])) {
        year = &parts[1];
        month = &parts[0];
      } else {
        return std::nullopt;
      }
      if (month->unknown) {
        return YearDate(year->value);
      }
      return MonthDate(year->value, *month);
    }

    case 3: {
      if (IsYear(parts[0])) {
        if (parts[1].unknown) {
          return YearDate(parts[0].value);
        }
        if (parts[2].unknown) {
          return MonthDate(parts[0].value, parts[1]);
        }
      } else if (IsYear(parts[2])) {
        if (parts[0].unknown && parts[1].unknown) {
          return YearDate(parts[2].value);
        }
        if (parts[0].unknown) {
          return MonthDate(parts[2].value, parts[1]);
        }
        if (parts[1].unknown) {
          return MonthDate(parts[2].value, parts[0]);
        }
      } else {
        return std::nullopt;
      }
      if (const auto day = FullDate(*split, order)) {
        return DayDate(*day);
      }
      return std::nullopt;
    }

    default:
      return std::nullopt;
  }
}

std::string FormatIsoDate(std::chrono::sys_days day) {
  return FormatIsoDate(DayDate(day));
}

std::string FormatIsoDate(const FlexibleDate& date) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", date.year, date.month, date.day);
  return buffer;
}

std::string_view DatePrecisionName(DatePrecision precision) {
  switch (precision) {
    case DatePrecision::Day:
      return "day";
    case DatePrecision::Month:
      return "month";
    case DatePrecision::Year:
      return "year";
  }
  return "day";
}

}  // namespace csvzall::pipeline::common

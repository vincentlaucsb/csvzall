#pragma once

#include "../../pipeline_types.hpp"

#include <functional>
#include <istream>
#include <ostream>
#include <string>

namespace csvzall::pipeline::common {

struct MarkdownCalendarOptions {
  std::string start_date;
  std::string end_date;
  std::function<std::string(int year, unsigned month)> month_header;
};

int RenderMarkdownCalendarCsv(std::istream& input,
                              std::ostream& output,
                              const RunOptions& options,
                              const LoggerCallbacks& logger,
                              RunStats& stats,
                              const MarkdownCalendarOptions& calendar_options);

}  // namespace csvzall::pipeline::common

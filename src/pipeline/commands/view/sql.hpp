#pragma once

#include "../view.hpp"

#include <string>
#include <string_view>

namespace csvzall::pipeline::commands::view_internal {

inline constexpr std::string_view kViewerSqlTableName = "data";

std::string DefaultViewerSqlQuery();
std::string BuildSqlQueryResultJson(const CsvViewData& data, std::string_view sql);

}  // namespace csvzall::pipeline::commands::view_internal

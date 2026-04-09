#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace csvzall::pipeline::common {

std::string Trim(std::string value);

bool IsIdentifier(std::string_view text);

std::optional<std::size_t> FindColumnIndex(const std::vector<std::string>& headers,
                                           std::string_view name,
                                           bool exact_column_matching);

}  // namespace csvzall::pipeline::common

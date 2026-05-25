#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace csvzall::pipeline::common {

std::string EscapeMarkdownTableCell(const std::string& value);

void WriteMarkdownTable(std::ostream& output,
                        const std::vector<std::string>& headers,
                        const std::vector<std::vector<std::string>>& rows);

}  // namespace csvzall::pipeline::common

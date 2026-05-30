#pragma once

#include "../view.hpp"

#include <csv.hpp>

#include <cstdint>
#include <string>

namespace csvzall::pipeline::commands::view_internal {

inline constexpr std::uint64_t kDefaultRowsPerPage = 500;
inline constexpr std::uint64_t kMaxRowsPerPage = 5000;
inline constexpr std::uint64_t kBytesPerMiB = 1024 * 1024;

csv::CSVFormat MakeViewFormat(const RunOptions& options);
std::string GenerateSessionToken();

}  // namespace csvzall::pipeline::commands::view_internal
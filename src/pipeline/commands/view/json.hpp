#pragma once

#include "../view.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace csvzall::pipeline::commands::view_internal {

std::string BuildSchemaJson(const CsvViewData& data, bool editable);
std::string BuildRowsJson(const CsvViewData& data,
                          std::uint64_t offset,
                          std::uint64_t limit,
                          const std::vector<std::vector<std::string>>& rows);
std::string BuildHealthJson();
std::string BuildStartupJson(const std::string& url);
std::string SaveResultJson(std::size_t charts_generated, std::string_view chart_error = {});

std::uint64_t JsonUintField(std::string_view body, std::string_view field);
std::string JsonStringField(std::string_view body, std::string_view field);
std::vector<std::string> JsonStringArrayField(std::string_view body, std::string_view field);
bool JsonBoolField(std::string_view body, std::string_view field);
std::string JsonStringFieldOr(std::string_view body,
                              std::string_view field,
                              std::string fallback);
bool JsonBoolFieldOr(std::string_view body, std::string_view field, bool fallback);
std::vector<std::string> JsonStringArrayFieldOr(std::string_view body,
                                                std::string_view field,
                                                std::vector<std::string> fallback);

}  // namespace csvzall::pipeline::commands::view_internal

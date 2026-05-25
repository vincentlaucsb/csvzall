#pragma once

#include "../view.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace csvzall::pipeline::commands::view_internal {

std::string BuildChartConfigListJson(const CsvViewData& data);
std::string GenerateCurrentCsvChart(const CsvViewData& data,
                                    std::string_view body,
                                    const LoggerCallbacks& logger);
std::size_t RenderRunOnSaveChartsForCurrentCsv(const CsvViewData& data,
                                               const LoggerCallbacks& logger);
std::string AppendHeatmapChartConfig(const CsvViewData& data,
                                     std::string_view body,
                                     const LoggerCallbacks& logger);

}  // namespace csvzall::pipeline::commands::view_internal
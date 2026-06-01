#pragma once

#include "chart_spec.hpp"

#include <csv.hpp>
#include <svgplot/svgplot.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace csvzall::charts {

struct CsvChartOptions {
  bool exact_column_matching = false;
};

struct CsvChartResult {
  std::string svg;
  std::uint64_t rows_processed = 0;
  std::uint64_t bytes_processed = 0;
};

std::string Trim(std::string_view value);

std::pair<std::string, std::string> ResolveHeatmapDateRange(const HeatmapSpec& spec);
svgplot::CalendarHeatmapOrientation ParseHeatmapOrientation(std::string_view raw);
bool IsGroupedBarPresentation(std::string_view raw);

std::optional<std::string> ValidateChartSpecFields(const ChartSpec& spec);

CsvChartResult RenderHeatmapCsv(csv::CSVReader& reader,
                                const std::vector<std::string>& headers,
                                const HeatmapSpec& spec,
                                const CsvChartOptions& options);
CsvChartResult RenderBarCsv(csv::CSVReader& reader,
                            const std::vector<std::string>& headers,
                            const BarSpec& spec,
                            const CsvChartOptions& options);
CsvChartResult RenderLineCsv(csv::CSVReader& reader,
                             const std::vector<std::string>& headers,
                             const LineSpec& spec,
                             const CsvChartOptions& options);

}  // namespace csvzall::charts

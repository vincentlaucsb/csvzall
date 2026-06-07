#pragma once

#include "csv_chart.hpp"

#include <csv.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace csvzall::charts {

std::optional<std::size_t> FindColumnIndex(const std::vector<std::string>& headers,
                                           std::string_view name,
                                           bool exact_column_matching);
void AccumulateRowBytes(const csv::CSVRow& row, CsvChartResult& result);
double ParseNumericField(csv::CSVField field,
                         const std::string& column,
                         std::string_view chart_type);
std::vector<ChartValueSpec> EffectiveValues(const std::vector<ChartValueSpec>& values,
                                            const std::string& legacy_column);
std::string ValueLabel(const ChartValueSpec& spec);
std::string ValueColor(const ChartValueSpec& spec, std::size_t index);
std::string ValueColor(const ChartValueSpec& spec,
                       std::size_t index,
                       std::string_view color_scheme);
std::string NormalizeChartColorScheme(std::string_view raw);

}  // namespace csvzall::charts

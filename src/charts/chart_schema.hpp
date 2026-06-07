#pragma once

#include <span>
#include <string>
#include <string_view>

namespace csvzall::charts {

struct ChartOptionDoc {
  std::string_view name;
  std::string_view detail;
};

struct ChartTypeDoc {
  std::string_view name;
  std::string_view summary;
  std::span<const ChartOptionDoc> options;
};

std::span<const ChartTypeDoc> ChartTypeDocs();
std::span<const ChartOptionDoc> ChartOptionDocsForType(std::string_view type);
std::span<const ChartOptionDoc> ChartValueObjectOptionDocs();
bool IsKnownChartType(std::string_view type);

std::string BuildChartSchemaReference();

}  // namespace csvzall::charts

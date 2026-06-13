#include "chart_schema.hpp"

#include <array>
#include <iomanip>
#include <sstream>

namespace csvzall::charts {
namespace {

constexpr std::array<ChartOptionDoc, 9> kHeatmapOptions{{
    {"date", "Date column. Defaults to \"date\"."},
    {"start",
     "Start date. Accepts YYYY-MM-DD, M/D/YYYY, M-D-YYYY, D/M/YYYY, or D-M-YYYY.\n"
     "                 Required with end unless lookback is set."},
    {"end",
     "End date. Accepts YYYY-MM-DD, M/D/YYYY, M-D-YYYY, D/M/YYYY, or D-M-YYYY.\n"
     "                 Optional with lookback; defaults to today."},
    {"lookback",
     "Positive number plus days or years: 30d, 90 days, 1y, 2 years.\n"
     "                 Cannot be combined with start; end is optional and defaults to today."},
    {"value", "Single numeric weight column. Omit to count rows."},
    {"values", "Multi-dataset numeric columns. See Multi-value options."},
    {"label", "Optional tooltip label column."},
    {"orientation", "months-horizontal, months-vertical, horizontal, or vertical."},
    {"title", "Optional SVG title."},
}};

constexpr std::array<ChartOptionDoc, 8> kBarOptions{{
    {"label", "Category column."},
    {"value", "Single numeric value column."},
    {"values", "Multi-dataset numeric columns. See Multi-value options."},
    {"colorScheme", "sequential or diverging for multi-value bars. Defaults to sequential."},
    {"presentation", "stacked or grouped. Defaults to stacked."},
    {"title", "Optional SVG title."},
    {"xLabel", "Optional X-axis label."},
    {"yLabel", "Optional Y-axis label."},
}};

constexpr std::array<ChartOptionDoc, 8> kLineOptions{{
    {"x", "X-axis column."},
    {"y", "Single numeric Y column."},
    {"values", "Multi-dataset numeric Y columns. Cannot be combined with series."},
    {"colorScheme", "sequential or diverging for multi-value lines. Defaults to sequential."},
    {"series", "Optional series/grouping column for single-y line charts."},
    {"title", "Optional SVG title."},
    {"xLabel", "Optional X-axis label."},
    {"yLabel", "Optional Y-axis label."},
}};

constexpr std::array<ChartOptionDoc, 2> kMarkdownTableOptions{{
    {"sql", "Custom SELECT against SQLite table `data`."},
    {"columns", "Array of CSV column names for a simple projection."},
}};

constexpr std::array<ChartOptionDoc, 3> kChartValueObjectOptions{{
    {"column", "CSV column name. Required."},
    {"label", "Optional display label. Defaults to column."},
    {"color", "Optional CSS color for chart renderers that support it."},
}};

constexpr std::array<ChartTypeDoc, 4> kChartTypes{{
    {"heatmap",
     "date, optional value/values/label/title/orientation, and either start+end or lookback.",
     kHeatmapOptions},
    {"bar",
     "label, value or values, optional colorScheme/title/xLabel/yLabel/presentation.",
     kBarOptions},
    {"line",
     "x, y or values, optional colorScheme/series/title/xLabel/yLabel.",
     kLineOptions},
    {"markdown-table",
     "optional sql SELECT, or columns array, or neither to export all CSV columns.",
     kMarkdownTableOptions},
}};

bool ContainsOption(std::span<const ChartOptionDoc> options, std::string_view name) {
  for (const auto& option : options) {
    if (option.name == name) {
      return true;
    }
  }
  return false;
}

void AppendWrappedSummary(std::ostringstream& output,
                          std::string_view type,
                          std::string_view summary) {
  output << "  " << std::left << std::setw(16) << type << summary << '\n';
}

void AppendOptionDoc(std::ostringstream& output, const ChartOptionDoc& option) {
  constexpr std::size_t kNameColumnWidth = 13;
  output << "    " << option.name;
  if (option.name.size() < kNameColumnWidth) {
    output << std::string(kNameColumnWidth - option.name.size(), ' ');
  } else {
    output << "  ";
  }
  output << option.detail << '\n';
}

}  // namespace

std::span<const ChartTypeDoc> ChartTypeDocs() {
  return kChartTypes;
}

std::span<const ChartOptionDoc> ChartOptionDocsForType(std::string_view type) {
  for (const auto& chart_type : kChartTypes) {
    if (chart_type.name == type) {
      return chart_type.options;
    }
  }
  return {};
}

std::span<const ChartOptionDoc> ChartValueObjectOptionDocs() {
  return kChartValueObjectOptions;
}

bool IsKnownChartType(std::string_view type) {
  return !ChartOptionDocsForType(type).empty();
}

std::string BuildChartSchemaReference() {
  std::ostringstream output;
  output << R"(charts config schema:
  Default config path: .csvzall/charts.json from the current directory.
  Use --config <path> with `charts run` to choose another config file.

Config file shape:
  {
    "charts": [
      {
        "id": "gym-heatmap",
        "type": "heatmap",
        "input": "gym.csv",
        "output": "charts/gym.svg",
        "options": { "date": "date", "lookback": "1y" }
      }
    ]
  }

Each chart object:
  id       Stable chart id. Pass it as `csvzall charts run <id>` to run one chart.
  type     One of: heatmap, bar, line, markdown-table.
  input    CSV path, resolved relative to the config root.
  output   Generated artifact path, required; existing files are overwritten.
  options  Type-specific object described below.

Options by type:
)";

  for (const auto& chart_type : ChartTypeDocs()) {
    AppendWrappedSummary(output, chart_type.name, chart_type.summary);
  }

  output << R"(
Drill-down by type:
)";
  for (const auto& chart_type : ChartTypeDocs()) {
    output << "  " << chart_type.name << " options:\n";
    for (const auto& option : chart_type.options) {
      AppendOptionDoc(output, option);
    }
    if (chart_type.name == "markdown-table") {
      output << "    Omit both sql and columns to export all CSV columns.\n";
    }
    output << '\n';
  }

  output << R"(Multi-value options:
  value/y  Single numeric column, such as "count".
  values   Array of strings or objects:
           ["gym", "bike"]
           [{"column":"gym","label":"Gym","color":"#2563eb"}].
  colorScheme controls generated colors for values without explicit color:
           sequential or diverging. Multi-value heatmaps use diverging.

  value object keys:
)";
  for (const auto& option : ChartValueObjectOptionDocs()) {
    AppendOptionDoc(output, option);
  }

  output << R"(
Validation and errors:
  --validate loads the config and checks inputs, columns, dates, and numeric
  values without writing outputs. It is useful for generated configs.
  Missing inputs, unknown chart types, invalid options, missing columns, and
  write failures produce stderr diagnostics and a non-zero exit.
)";
  return output.str();
}

}  // namespace csvzall::charts

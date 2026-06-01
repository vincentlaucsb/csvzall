#ifdef CSVZALL_HAVE_SVGPLOT

#include "commands.hpp"

#include "../../charts/csv_chart.hpp"
#include "../../sqlite/csv_loader.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace csvzall::pipeline::commands {
namespace {

bool EmitChartValidationError(const LoggerCallbacks& logger, std::string message) {
  if (logger.error) {
    logger.error(std::move(message));
  }
  return false;
}

bool ValidateChartSpecFields(const charts::ChartSpec& spec,
                             const LoggerCallbacks& logger) {
  if (const auto error = charts::ValidateChartSpecFields(spec)) {
    return EmitChartValidationError(logger, *error);
  }
  return true;
}

int RenderChartToStream(const charts::ChartSpec& spec,
                        std::istream& input,
                        std::ostream& output,
                        const RunOptions& options,
                        const LoggerCallbacks& logger,
                        RunStats& stats) {
  if (spec.type == "markdown-table") {
    std::string query = spec.markdown_table.sql;
    if (query.empty()) {
      if (spec.markdown_table.columns.empty()) {
        query = "SELECT * FROM data";
      } else {
        query = "SELECT ";
        for (std::size_t i = 0; i < spec.markdown_table.columns.size(); ++i) {
          if (i > 0) {
            query += ", ";
          }
          query += sqlite::QuoteIdentifier(spec.markdown_table.columns[i]);
        }
        query += " FROM data";
      }
    }
    return RunSqlQueryCsv(query, "data", "markdown", input, output, options, logger, stats);
  }
  if (spec.type == "heatmap") {
    return RunHeatmap(spec.heatmap, input, output, options, logger, stats);
  }
  if (spec.type == "bar") {
    return RunBar(spec.bar, input, output, options, logger, stats);
  }
  if (spec.type == "line") {
    return RunLine(spec.line, input, output, options, logger, stats);
  }
  if (logger.error) {
    logger.error("charts: unknown chart type '" + spec.type + "'");
  }
  return 1;
}

}  // namespace

int RunChart(const charts::ChartSpec& spec,
             const RunOptions& options,
             const LoggerCallbacks& logger,
             RunStats& stats) {
  if (!ValidateChartSpecFields(spec, logger)) {
    return 1;
  }

  std::ifstream input(spec.input, std::ios::binary);
  if (!input.is_open()) {
    if (logger.error) {
      logger.error("charts: missing input file for chart '" + spec.id +
                   "': " + spec.input.string());
    }
    return 1;
  }

  const auto parent = spec.output->parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      if (logger.error) {
        logger.error("charts: unable to create output directory for chart '" + spec.id +
                     "': " + parent.string() + ": " + ec.message());
      }
      return 1;
    }
  }

  std::ofstream output(*spec.output, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    if (logger.error) {
      logger.error("charts: unable to open output file for chart '" + spec.id +
                   "': " + spec.output->string());
    }
    return 1;
  }

  RunOptions chart_options = options;
  chart_options.input_is_stdin = false;
  chart_options.input_path = spec.input.string();
  const auto rc = RenderChartToStream(spec, input, output, chart_options, logger, stats);
  output.flush();
  if (rc == 0 && !output.good()) {
    if (logger.error) {
      logger.error("charts: failed to write output file for chart '" + spec.id +
                   "': " + spec.output->string());
    }
    return 1;
  }
  return rc;
}

int ValidateChart(const charts::ChartSpec& spec,
                  const RunOptions& options,
                  const LoggerCallbacks& logger,
                  RunStats& stats) {
  if (!ValidateChartSpecFields(spec, logger)) {
    return 1;
  }

  std::ifstream input(spec.input, std::ios::binary);
  if (!input.is_open()) {
    if (logger.error) {
      logger.error("charts: missing input file for chart '" + spec.id +
                   "': " + spec.input.string());
    }
    return 1;
  }

  RunOptions chart_options = options;
  chart_options.input_is_stdin = false;
  chart_options.input_path = spec.input.string();
  std::ostringstream output;
  return RenderChartToStream(spec, input, output, chart_options, logger, stats);
}

}  // namespace csvzall::pipeline::commands

#endif  // CSVZALL_HAVE_SVGPLOT

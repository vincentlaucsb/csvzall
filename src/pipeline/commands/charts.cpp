#include "commands.hpp"

#include "../common/chart_spec.hpp"

#include <filesystem>
#include <stdexcept>

namespace csvzall::pipeline::commands {

int RunCharts(const std::string& config_path,
              const std::string& chart_id,
              bool validate_only,
              const RunOptions& options,
              const LoggerCallbacks& logger,
              RunStats& stats) {
  try {
    // Invalid entries are reported only when selected (or when running all
    // charts), so one bad chart doesn't block unrelated ones.
    const auto config = common::LoadChartConfigAllowingInvalidCharts(
        config_path.empty() ? common::DefaultChartConfigPath()
                            : std::filesystem::path(config_path));

    bool matched = false;
    int result = 0;
    for (const auto& invalid : config.invalid_charts) {
      if (!chart_id.empty() && invalid.id != chart_id) {
        continue;
      }
      matched = true;
      result = 1;
      if (logger.error) {
        logger.error("charts: " + invalid.message);
      }
    }

    for (const auto& chart : config.charts) {
      if (!chart_id.empty() && chart.id != chart_id) {
        continue;
      }
      matched = true;
      const auto rc = validate_only
          ? ValidateChart(chart, options, logger, stats)
          : RunChart(chart, options, logger, stats);
      if (rc != 0) {
        result = rc;
      }
    }

    if (!matched) {
      if (logger.error) {
        logger.error(chart_id.empty()
                         ? "charts: config does not contain any charts"
                         : "charts: chart id not found: " + chart_id);
      }
      return 1;
    }
    if (result == 0 && validate_only && logger.info) {
      logger.info(chart_id.empty()
                      ? "charts: config is valid"
                      : "charts: chart config is valid: " + chart_id);
    }
    return result;
  } catch (const std::exception& ex) {
    if (logger.error) {
      logger.error(std::string("charts: ") + ex.what());
    }
    return 1;
  }
}

}  // namespace csvzall::pipeline::commands

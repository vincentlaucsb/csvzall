#ifdef CSVZALL_HAVE_SVGPLOT

#include "commands.hpp"

#include "../../charts/csv_chart.hpp"

#include <exception>
#include <ostream>
#include <string>
#include <utility>

namespace csvzall::pipeline::commands {
namespace {

charts::CsvChartOptions MakeCsvChartOptions(const RunOptions& options) {
  return {options.exact_column_matching};
}

void ApplyChartResult(const charts::CsvChartResult& result, RunStats& stats) {
  stats.rows_processed += result.rows_processed;
  stats.bytes_processed += result.bytes_processed;
}

}  // namespace

class HeatmapCommand : public CsvInputCommand {
public:
  HeatmapCommand(charts::HeatmapSpec spec,
                 std::istream& input,
                 std::ostream& output,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats)
      : CsvInputCommand(input, options, logger, stats),
        spec_(std::move(spec)),
        output_(output) {}

protected:
  int run() override {
    try {
      const auto result = charts::RenderHeatmapCsv(
          reader(), headers(), spec_, MakeCsvChartOptions(options()));
      output_ << result.svg << '\n';
      ApplyChartResult(result, stats());
      return 0;
    } catch (const std::exception& ex) {
      if (logger().error) {
        logger().error(std::string("heatmap: ") + ex.what());
      }
      return 1;
    }
  }

private:
  charts::HeatmapSpec spec_;
  std::ostream& output_;
};

int RunHeatmap(const std::string& date_column,
               const std::string& value_column,
               const std::string& label_column,
               const std::string& start_date,
               const std::string& end_date,
               const std::string& title,
               std::istream& input,
               std::ostream& output,
               const RunOptions& options,
               const LoggerCallbacks& logger,
               RunStats& stats) {
  charts::HeatmapSpec spec;
  spec.date_column = date_column;
  spec.value_column = value_column;
  spec.label_column = label_column;
  spec.start_date = start_date;
  spec.end_date = end_date;
  spec.title = title;
  return RunHeatmap(spec, input, output, options, logger, stats);
}

int RunHeatmap(const charts::HeatmapSpec& spec,
               std::istream& input,
               std::ostream& output,
               const RunOptions& options,
               const LoggerCallbacks& logger,
               RunStats& stats) {
  HeatmapCommand cmd(spec, input, output, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

#endif  // CSVZALL_HAVE_SVGPLOT

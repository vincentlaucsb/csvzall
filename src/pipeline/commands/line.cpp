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

class LineCommand : public CsvInputCommand {
public:
  LineCommand(charts::LineSpec spec,
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
      const auto result = charts::RenderLineCsv(
          reader(), headers(), spec_, MakeCsvChartOptions(options()));
      output_ << result.svg << '\n';
      ApplyChartResult(result, stats());
      return 0;
    } catch (const std::exception& ex) {
      if (logger().error) {
        logger().error(std::string("line: ") + ex.what());
      }
      return 1;
    }
  }

private:
  charts::LineSpec spec_;
  std::ostream& output_;
};

int RunLine(const charts::LineSpec& spec,
            std::istream& input,
            std::ostream& output,
            const RunOptions& options,
            const LoggerCallbacks& logger,
            RunStats& stats) {
  LineCommand cmd(spec, input, output, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

#endif  // CSVZALL_HAVE_SVGPLOT

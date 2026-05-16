#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../common/row_utils.hpp"

#include <svgplot/svgplot.hpp>

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace csvzall::pipeline::commands {
namespace {

class HeatmapCommand : public CsvInputCommand {
public:
  HeatmapCommand(std::string date_column,
                 std::string value_column,
                 std::string label_column,
                 std::string start_date,
                 std::string end_date,
                 std::string title,
                 std::istream& input,
                 std::ostream& output,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats)
      : CsvInputCommand(input, options, logger, stats),
        date_column_(std::move(date_column)),
        value_column_(std::move(value_column)),
        label_column_(std::move(label_column)),
        start_date_(std::move(start_date)),
        end_date_(std::move(end_date)),
        title_(std::move(title)),
        output_(output) {}

protected:
  int run() override {
    try {
      const auto date_index =
          common::FindColumnIndex(headers(), date_column_, options().exact_column_matching);
      if (!date_index) {
        throw std::runtime_error("date column not found: " + date_column_);
      }

      std::optional<std::size_t> value_index;
      if (!value_column_.empty()) {
        value_index =
            common::FindColumnIndex(headers(), value_column_, options().exact_column_matching);
        if (!value_index) {
          throw std::runtime_error("value column not found: " + value_column_);
        }
      }

      std::optional<std::size_t> label_index;
      if (!label_column_.empty()) {
        label_index =
            common::FindColumnIndex(headers(), label_column_, options().exact_column_matching);
        if (!label_index) {
          throw std::runtime_error("label column not found: " + label_column_);
        }
      }

      std::vector<svgplot::HeatmapCell> cells;
      for (auto& row : reader()) {
        const auto date_text = row[*date_index].get<std::string>();
        if (date_text.empty()) {
          throw std::runtime_error("empty date value in column: " + date_column_);
        }

        double value = 1.0;
        if (value_index) {
          auto field = row[*value_index];
          if (field.is_null() || field.get<std::string>().empty()) {
            value = 0.0;
          } else {
            long double parsed = 0.0;
            if (!field.try_get(parsed) || !std::isfinite(static_cast<double>(parsed))) {
              throw std::runtime_error("non-numeric heatmap value in column: " + value_column_);
            }
            value = static_cast<double>(parsed);
          }
        }

        std::string label;
        if (label_index) {
          label = row[*label_index].get<std::string>();
        }

        cells.push_back({svgplot::parse_date(date_text), value, std::move(label)});
        ++stats().rows_processed;
        common::AccumulateRowBytes(row, stats());
      }

      svgplot::HeatmapOptions chart_options;
      chart_options.title = title_;
      chart_options.start_date = svgplot::parse_date(start_date_);
      chart_options.end_date = svgplot::parse_date(end_date_);
      output_ << svgplot::heatmap_chart(cells, chart_options).str() << '\n';
      return 0;
    } catch (const std::exception& ex) {
      if (logger().error) {
        logger().error(std::string("heatmap: ") + ex.what());
      }
      return 1;
    }
  }

private:
  std::string date_column_;
  std::string value_column_;
  std::string label_column_;
  std::string start_date_;
  std::string end_date_;
  std::string title_;
  std::ostream& output_;
};

}  // namespace

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
  HeatmapCommand cmd(date_column, value_column, label_column, start_date, end_date, title,
                     input, output, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

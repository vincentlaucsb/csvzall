#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../common/row_utils.hpp"

#include <csv.hpp>

#include <optional>
#include <string>

namespace csvzall::pipeline::commands {
namespace {

enum class ExtremaMode {
  Min,
  Max,
};

class ExtremaCommand : public CsvInputCommand {
public:
  ExtremaCommand(const std::string& column,
                 ExtremaMode mode,
                 std::istream& input,
                 std::ostream& output,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats)
      : CsvInputCommand(input, options, logger, stats),
        column_(column),
        mode_(mode),
        output_(output) {}

protected:
  int run() override {
    const auto column_index =
        common::FindColumnIndex(headers(), column_, options().exact_column_matching);
    if (!column_index) {
      if (logger().error) {
        logger().error(CommandName() + " column not found: " + column_);
      }
      return 1;
    }

    std::optional<std::string> best_text;
    std::optional<long double> best_number;
    bool numeric_mode = false;

    for (auto& row : reader()) {
      auto field = row[*column_index];
      if (field.is_null()) {
        ++stats().rows_processed;
        common::AccumulateRowBytes(row, stats());
        continue;
      }

      const std::string text = field.get<std::string>();
      long double number = 0.0;
      if (field.try_get(number)) {
        if (!best_text || !numeric_mode || Better(number, *best_number)) {
          best_text = text;
          best_number = number;
          numeric_mode = true;
        }
      } else if (!numeric_mode && (!best_text || Better(text, *best_text))) {
        best_text = text;
      }

      ++stats().rows_processed;
      common::AccumulateRowBytes(row, stats());
    }

    if (!best_text) {
      if (logger().error) {
        logger().error(CommandName() + " column has no non-empty values: " + column_);
      }
      return 1;
    }

    output_ << *best_text << '\n';
    return 0;
  }

private:
  [[nodiscard]] std::string CommandName() const {
    return mode_ == ExtremaMode::Max ? "Max" : "Min";
  }

  [[nodiscard]] bool Better(long double lhs, long double rhs) const {
    return mode_ == ExtremaMode::Max ? lhs > rhs : lhs < rhs;
  }

  [[nodiscard]] bool Better(const std::string& lhs, const std::string& rhs) const {
    return mode_ == ExtremaMode::Max ? lhs > rhs : lhs < rhs;
  }

  std::string column_;
  ExtremaMode mode_;
  std::ostream& output_;
};

}  // namespace

int RunMax(const std::string& column,
           std::istream& input,
           std::ostream& output,
           const RunOptions& options,
           const LoggerCallbacks& logger,
           RunStats& stats) {
  ExtremaCommand cmd(column, ExtremaMode::Max, input, output, options, logger, stats);
  return cmd.execute();
}

int RunMin(const std::string& column,
           std::istream& input,
           std::ostream& output,
           const RunOptions& options,
           const LoggerCallbacks& logger,
           RunStats& stats) {
  ExtremaCommand cmd(column, ExtremaMode::Min, input, output, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

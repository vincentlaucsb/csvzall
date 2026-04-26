#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../common/row_utils.hpp"

#include <csv.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace csvzall::pipeline::commands {

namespace {

enum class ReduceOp { Max, Min, Sum, Avg, Last };

ReduceOp ParseReduceOp(const std::string& s) {
  if (s == "max") return ReduceOp::Max;
  if (s == "min") return ReduceOp::Min;
  if (s == "sum") return ReduceOp::Sum;
  if (s == "avg") return ReduceOp::Avg;
  return ReduceOp::Last;
}

struct AggRecord {
  double value = 0.0;
  std::size_t count = 0;
  bool initialized = false;
};

void ApplyReduce(AggRecord& rec, double incoming, ReduceOp op) {
  if (!rec.initialized) {
    rec.value = incoming;
    rec.count = 1;
    rec.initialized = true;
    return;
  }
  switch (op) {
    case ReduceOp::Max:  rec.value = std::max(rec.value, incoming); break;
    case ReduceOp::Min:  rec.value = std::min(rec.value, incoming); break;
    case ReduceOp::Sum:  rec.value += incoming; break;
    case ReduceOp::Avg:  rec.value += incoming; rec.count++; break;
    case ReduceOp::Last: rec.value = incoming; break;
  }
}

double FinalValue(const AggRecord& rec, ReduceOp op) {
  if (op == ReduceOp::Avg && rec.count > 0) {
    return rec.value / static_cast<double>(rec.count);
  }
  return rec.value;
}

void WriteMarkdownTable(std::ostream& output,
                        const std::vector<std::string>& headers,
                        const std::vector<std::vector<std::string>>& rows) {
  std::vector<std::size_t> widths(headers.size(), 0);
  for (std::size_t i = 0; i < headers.size(); ++i) {
    widths[i] = headers[i].size();
  }
  for (const auto& row : rows) {
    for (std::size_t i = 0; i < row.size() && i < widths.size(); ++i) {
      widths[i] = std::max(widths[i], row[i].size());
    }
  }

  auto write_row = [&](const std::vector<std::string>& cells) {
    output << '|';
    for (std::size_t i = 0; i < cells.size(); ++i) {
      output << ' ' << cells[i];
      for (std::size_t p = cells[i].size(); p < widths[i]; ++p) output << ' ';
      output << " |";
    }
    output << '\n';
  };

  auto write_sep = [&]() {
    output << '|';
    for (std::size_t i = 0; i < widths.size(); ++i) {
      output << '-';
      for (std::size_t p = 0; p < widths[i]; ++p) output << '-';
      output << "-|";
    }
    output << '\n';
  };

  write_row(headers);
  write_sep();
  for (const auto& row : rows) {
    write_row(row);
  }
}

}  // namespace

class TimeseriesCommand : public CsvTransformCommand {
public:
  TimeseriesCommand(const std::string& x_column, const std::string& y_column,
                    const std::string& series_column, const std::string& reduce,
                    const std::string& format,
                    std::istream& input, std::ostream& output,
                    const RunOptions& options, const LoggerCallbacks& logger,
                    RunStats& stats)
      : CsvTransformCommand(input, output, options, logger, stats),
        x_column_(x_column), y_column_(y_column),
        series_column_(series_column), reduce_str_(reduce),
        format_(format) {}

protected:
  int run() override {
    const auto x_idx_opt = common::FindColumnIndex(headers(), x_column_, options().exact_column_matching);
    if (!x_idx_opt) {
      if (logger().error) logger().error("x column not found: " + x_column_);
      return 1;
    }
    const std::size_t x_idx = *x_idx_opt;

    const auto y_idx_opt = common::FindColumnIndex(headers(), y_column_, options().exact_column_matching);
    if (!y_idx_opt) {
      if (logger().error) logger().error("y column not found: " + y_column_);
      return 1;
    }
    const std::size_t y_idx = *y_idx_opt;

    std::optional<std::size_t> series_idx;
    const bool has_series = !series_column_.empty();
    if (has_series) {
      const auto series_idx_opt = common::FindColumnIndex(headers(), series_column_, options().exact_column_matching);
      if (!series_idx_opt) {
        if (logger().error) logger().error("series column not found: " + series_column_);
        return 1;
      }
      series_idx = series_idx_opt;
    }

    const ReduceOp op = ParseReduceOp(reduce_str_);

    std::vector<std::string> series_order;
    std::unordered_map<std::string, std::map<std::string, AggRecord>> data;

    for (auto& row : reader()) {
      const std::string x_val = row[x_idx].get<std::string>();
      double y_val = 0.0;
      if (!row[y_idx].try_get(y_val)) {
        if (logger().verbose) logger().verbose("Skipping row with non-numeric y at x=" + x_val);
        stats().rows_processed++;
        common::AccumulateRowBytes(row, stats());
        continue;
      }

      const std::string series_key = has_series
          ? row[*series_idx].get<std::string>()
          : std::string{};

      if (data.find(series_key) == data.end()) {
        series_order.push_back(series_key);
      }

      ApplyReduce(data[series_key][x_val], y_val, op);
      stats().rows_processed++;
      common::AccumulateRowBytes(row, stats());
    }

    std::vector<std::string> out_headers;
    if (has_series) out_headers.push_back("series");
    out_headers.push_back("x");
    out_headers.push_back("y");

    std::vector<std::vector<std::string>> out_rows;
    for (const auto& series_key : series_order) {
      const auto& x_map = data.at(series_key);
      for (const auto& [x_val, rec] : x_map) {
        std::vector<std::string> out_row;
        if (has_series) out_row.push_back(series_key);
        out_row.push_back(x_val);
        out_row.push_back(common::DoubleToString(FinalValue(rec, op)));
        out_rows.push_back(std::move(out_row));
      }
    }

    if (format_ == "markdown") {
      WriteMarkdownTable(output(), out_headers, out_rows);
    } else {
      auto writer = csv::make_csv_writer(output()).set_auto_flush(false);
      writer << out_headers;
      for (auto& r : out_rows) {
        writer << r;
      }
      writer.flush();
    }

    return 0;
  }

private:
  std::string x_column_;
  std::string y_column_;
  std::string series_column_;
  std::string reduce_str_;
  std::string format_;
};

int RunTimeseries(const std::string& x_column, const std::string& y_column,
                  const std::string& series_column, const std::string& reduce,
                  const std::string& format,
                  std::istream& input, std::ostream& output,
                  const RunOptions& options, const LoggerCallbacks& logger,
                  RunStats& stats) {
  TimeseriesCommand cmd(x_column, y_column, series_column, reduce, format,
                        input, output, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

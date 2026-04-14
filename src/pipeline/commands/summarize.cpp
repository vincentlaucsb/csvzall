#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../common/row_utils.hpp"

#include <csv.hpp>

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace csvzall::pipeline::commands {

class SummarizeCommand : public CsvTransformCommand {
public:
  SummarizeCommand(const std::string& group_by_column, const std::string& max_column,
                   const std::vector<std::string>& show_columns,
                   std::istream& input, std::ostream& output,
                   const RunOptions& options, const LoggerCallbacks& logger,
                   RunStats& stats)
      : CsvTransformCommand(input, output, options, logger, stats),
        group_by_column_(group_by_column), max_column_(max_column),
        show_columns_(show_columns) {}

protected:
  int run() override {
    const auto group_idx = common::FindColumnIndex(headers(), group_by_column_,
                                                   options().exact_column_matching);
    if (!group_idx.has_value()) {
      if (logger().error) {
        logger().error("Group-by column not found: " + group_by_column_);
      }
      return 1;
    }

    const auto max_idx = common::FindColumnIndex(headers(), max_column_,
                                                 options().exact_column_matching);
    if (!max_idx.has_value()) {
      if (logger().error) {
        logger().error("Max column not found: " + max_column_);
      }
      return 1;
    }

    std::vector<std::size_t> show_indexes;
    for (const auto& col : show_columns_) {
      const auto idx = common::FindColumnIndex(headers(), col, options().exact_column_matching);
      if (!idx.has_value()) {
        if (logger().error) {
          logger().error("Show column not found: " + col);
        }
        return 1;
      }
      show_indexes.push_back(*idx);
    }

    struct GroupRecord {
      double max_value = -std::numeric_limits<double>::infinity();
      std::vector<std::string> winning_row;
    };

    std::vector<std::pair<std::string, GroupRecord>> groups;
    std::unordered_map<std::string, std::size_t> group_index_map;

    for (auto& row : reader()) {
      const std::string group_key = row[*group_idx].get<std::string>();

      double value = 0.0;
      if (!row[*max_idx].try_get(value)) {
        continue;
      }

      auto it = group_index_map.find(group_key);
      if (it == group_index_map.end()) {
        group_index_map[group_key] = groups.size();
        GroupRecord rec;
        rec.max_value = value;
        rec.winning_row = std::vector<std::string>(row);
        groups.emplace_back(group_key, rec);
      } else if (value > groups[it->second].second.max_value) {
        groups[it->second].second.max_value = value;
        groups[it->second].second.winning_row = std::vector<std::string>(row);
      }

      stats().rows_processed++;
      common::AccumulateRowBytes(row, stats());
    }

    auto writer = csv::make_csv_writer_buffered(output());
    std::vector<std::string> out_headers;
    out_headers.push_back(group_by_column_);
    for (const auto& col : show_columns_) {
      out_headers.push_back(col);
    }
    out_headers.push_back("max_" + max_column_);
    writer << out_headers;

    for (const auto& [key, record] : groups) {
      std::vector<std::string> out_row;
      out_row.push_back(key);
      for (const auto idx : show_indexes) {
        if (idx < record.winning_row.size()) {
          out_row.push_back(record.winning_row[idx]);
        } else {
          out_row.emplace_back();
        }
      }
      out_row.push_back(common::DoubleToString(record.max_value));
      writer << out_row;
    }

    writer.flush();
    return 0;
  }

private:
  std::string group_by_column_;
  std::string max_column_;
  std::vector<std::string> show_columns_;
};

int RunSummarize(const std::string& group_by_column, const std::string& max_column,
                 const std::vector<std::string>& show_columns,
                 std::istream& input, std::ostream& output,
                 const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  SummarizeCommand cmd(group_by_column, max_column, show_columns,
                       input, output, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

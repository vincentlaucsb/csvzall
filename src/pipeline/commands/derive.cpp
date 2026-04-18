#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../sqlite/csv_loader.hpp"
#include "../sqlite/sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <csv.hpp>

#include <string>
#include <vector>

namespace csvzall::pipeline::commands {
class DeriveCommand : public CsvTransformCommand {
public:
  DeriveCommand(const std::string& assignment, std::istream& input,
                std::ostream& output, const RunOptions& options,
                const LoggerCallbacks& logger, RunStats& stats)
      : CsvTransformCommand(input, output, options, logger, stats),
        assignment_(assignment) {}

protected:
  int run() override {
    const auto equals_pos = assignment_.find('=');
    if (equals_pos == std::string::npos) {
      if (logger().error) {
        logger().error("Invalid derive assignment. Expected format: NewCol = expression");
      }
      return 1;
    }

    const std::string new_column = common::Trim(assignment_.substr(0, equals_pos));
    const std::string expr_text  = common::Trim(assignment_.substr(equals_pos + 1));

    if (new_column.empty() || expr_text.empty()) {
      if (logger().error) {
        logger().error("Invalid derive assignment. Both column name and expression are required.");
      }
      return 1;
    }

    if (common::FindColumnIndex(headers(), new_column, options().exact_column_matching).has_value()) {
      if (logger().error) {
        logger().error("Derived column already exists: " + new_column);
      }
      return 1;
    }

    sqlite::SqliteDb sdb = sqlite::OpenSqliteDb(options());

    if (!sqlite::LoadCsvIntoTable(reader(), headers(), sdb.db(), "t", options(), logger())) {
      return 1;
    }

    const std::string sql =
        "SELECT *, " + expr_text + " AS " + sqlite::QuoteIdentifier(new_column) + " FROM \"t\"";

    try {
      SQLite::Statement query(sdb.db(), sql);

      auto writer = csv::make_csv_writer_buffered(output());
      std::vector<std::string> out_headers = headers();
      out_headers.push_back(new_column);
      writer << out_headers;

      while (query.executeStep()) {
        std::vector<std::string> out_row;
        out_row.reserve(static_cast<std::size_t>(query.getColumnCount()));
        for (int i = 0; i < query.getColumnCount(); ++i) {
          out_row.emplace_back(
              query.getColumn(i).isNull() ? std::string{} : query.getColumn(i).getString());
        }
        writer << out_row;
        stats().rows_processed++;
      }

      writer.flush();
    } catch (const SQLite::Exception& ex) {
      if (logger().error) {
        logger().error(std::string("SQLite derive error: ") + ex.what());
      }
      return 1;
    }

    return 0;
  }

private:
  std::string assignment_;
};

int RunDerive(const std::string& assignment, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  DeriveCommand cmd(assignment, input, output, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

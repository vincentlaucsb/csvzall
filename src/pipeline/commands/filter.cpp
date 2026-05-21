#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../sqlite/csv_loader.hpp"
#include "../sqlite/sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <csv.hpp>

#include <string>
#include <vector>

namespace csvzall::pipeline::commands {

class FilterCommand : public CsvTransformCommand {
public:
  FilterCommand(const std::string& where_clause, std::istream& input,
                std::ostream& output, const RunOptions& options,
                const LoggerCallbacks& logger, RunStats& stats)
      : CsvTransformCommand(input, output, options, logger, stats),
        where_clause_(where_clause) {}

protected:
  int run() override {
    const std::string where_text = common::Trim(where_clause_);
    if (where_text.empty()) {
      if (logger().error) {
        logger().error("Filter WHERE clause cannot be empty.");
      }
      return 1;
    }

    sqlite::SqliteDb sdb = sqlite::OpenSqliteDb(options());
    const auto column_affinities = sqlite::InferColumnAffinities(reader(), headers());
    if (reset_reader() != 0) {
      return 1;
    }

    if (!sqlite::LoadCsvIntoTable(
            reader(), headers(), sdb.db(), "t", column_affinities, options(), logger())) {
      return 1;
    }

    const std::string sql = R"(SELECT * FROM "t" WHERE )" + where_text;

    try {
      SQLite::Statement query(sdb.db(), sql);

      auto writer = csv::make_csv_writer(output()).set_auto_flush(false);
      writer << headers();

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
        logger().error(std::string("SQLite filter error: ") + ex.what());
      }
      return 1;
    }

    return 0;
  }

private:
  std::string where_clause_;
};

int RunFilter(const std::string& expression, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats) {
  FilterCommand cmd(expression, input, output, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

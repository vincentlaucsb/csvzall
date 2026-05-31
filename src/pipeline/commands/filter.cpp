#include "commands.hpp"
#include "sqlite_options.hpp"

#include "../common/column_lookup.hpp"
#include "../sqlite/csv_loader.hpp"
#include "../sqlite/sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <string>

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

    sqlite::SqliteDb sdb = sqlite::OpenSqliteDb(MakeSqliteDbOpenOptions(options()));
    if (!sqlite::LoadCsvIntoTableWithInferredAffinities(
            [this]() -> csv::CSVReader& { return reader(); }, headers(), sdb.db(), "t",
            [this]() { return reset_reader() == 0; },
            MakeCsvLoadOptions(options()), MakeSqliteLogCallbacks(logger()))) {
      return 1;
    }

    const std::string sql = R"(SELECT * FROM "t" WHERE )" + where_text;

    try {
      SQLite::Statement query(sdb.db(), sql);
      stats().rows_processed +=
          sqlite::WriteStatementRowsAsCsv(query, headers(), output());
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

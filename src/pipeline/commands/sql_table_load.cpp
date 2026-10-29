#include "commands.hpp"
#include "sqlite_options.hpp"

#include "../../sqlite/csv_loader.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <string>

namespace csvzall::pipeline::commands {
namespace {

// Loads the command's CSV input into a table inside a caller-owned database.
class CsvTableLoadCommand : public CsvInputCommand {
public:
  CsvTableLoadCommand(SQLite::Database& db,
                      const std::string& table_name,
                      std::istream& input,
                      const RunOptions& options,
                      const LoggerCallbacks& logger,
                      RunStats& stats)
      : CsvInputCommand(input, options, logger, stats), db_(db), table_name_(table_name) {}

protected:
  int run() override {
    return sqlite::LoadCsvIntoTableWithInferredAffinities(
               [this]() -> csv::CSVReader& { return reader(); }, headers(), db_, table_name_,
               [this]() { return reset_reader() == 0; }, MakeCsvLoadOptions(options()),
               MakeSqliteLogCallbacks(logger()))
               ? 0
               : 1;
  }

private:
  SQLite::Database& db_;
  std::string table_name_;
};

}  // namespace

int LoadCsvInputIntoTable(SQLite::Database& db,
                          const std::string& table_name,
                          std::istream& input,
                          const RunOptions& options,
                          const LoggerCallbacks& logger,
                          RunStats& stats) {
  CsvTableLoadCommand cmd(db, table_name, input, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

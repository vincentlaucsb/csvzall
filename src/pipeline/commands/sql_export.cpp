#include "commands.hpp"
#include "sqlite_options.hpp"

#include "../../sqlite/csv_loader.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <memory>
#include <string>

namespace csvzall::pipeline::commands {

namespace {

// Derive destination path: replace or append .db extension.
// "data.csv"      -> "data.db"
// "data.csv.bak"  -> "data.csv.db"   (non-.csv extension, just append .db)
// "data"          -> "data.db"
std::string DeriveDatabasePath(const std::string& input_path) {
  std::filesystem::path p(input_path);
  if (p.extension() == ".csv") {
    p.replace_extension(".db");
  } else {
    p += ".db";
  }
  return p.string();
}

}  // namespace

class SqlExportCommand : public CsvInputCommand {
public:
  SqlExportCommand(const std::string& input_path,
                   const std::string& dest_path,
                   const std::string& table_name,
                   std::istream& input,
                   const RunOptions& options,
                   const LoggerCallbacks& logger,
                   RunStats& stats)
      : CsvInputCommand(input, options, logger, stats),
        input_path_(input_path),
        dest_path_(dest_path),
        table_name_(table_name) {}

protected:
  int run() override {
    // Resolve destination.
    std::string db_path = dest_path_;
    if (db_path.empty()) {
      if (input_path_.empty() || input_path_ == "-") {
        if (logger().error) {
          logger().error("sql: cannot infer destination database path from stdin. "
                         "Provide an explicit --dest path.");
        }
        return 1;
      }
      db_path = DeriveDatabasePath(input_path_);
    }

    std::error_code path_error;
    const bool destination_exists = std::filesystem::exists(db_path, path_error);
    if (path_error) {
      if (logger().error) {
        logger().error("sql: cannot inspect destination: " + db_path + ": " + path_error.message());
      }
      return 1;
    }
    if (destination_exists) {
      if (logger().error) {
        logger().error("sql: destination already exists: " + db_path +
                       ". Remove it first or choose a different --dest path.");
      }
      return 1;
    }

    try {
      auto db = std::make_unique<SQLite::Database>(
          db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

      if (!sqlite::LoadCsvIntoTableWithInferredAffinities(
              [this]() -> csv::CSVReader& { return reader(); }, headers(), *db, table_name_,
              [this]() { return reset_reader() == 0; },
              MakeCsvLoadOptions(options()), MakeSqliteLogCallbacks(logger()))) {
        // Windows cannot unlink an open SQLite database. Close before cleanup.
        db.reset();
        std::error_code ec;
        std::filesystem::remove(db_path, ec);
        return 1;
      }

      SQLite::Statement count_stmt(*db, "SELECT COUNT(*) FROM " + sqlite::QuoteIdentifier(table_name_));
      if (count_stmt.executeStep()) {
        stats().rows_processed =
            static_cast<std::uint64_t>(count_stmt.getColumn(0).getInt64());
      }

      if (logger().verbose) {
        logger().verbose("sql: wrote " + std::to_string(stats().rows_processed) +
                         " rows to " + db_path);
      }
    } catch (const SQLite::Exception& ex) {
      if (logger().error) {
        logger().error(std::string("sql: SQLite error: ") + ex.what());
      }
      std::error_code ec;
      std::filesystem::remove(db_path, ec);
      return 1;
    }

    return 0;
  }

private:
  std::string input_path_;
  std::string dest_path_;
  std::string table_name_;
};

int RunSqlExport(const std::string& input_path,
                 const std::string& dest_path,
                 const std::string& table_name,
                 std::istream& input,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats) {
  SqlExportCommand cmd(input_path, dest_path, table_name, input, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

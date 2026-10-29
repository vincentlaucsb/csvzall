#include "commands.hpp"
#include "sqlite_options.hpp"

#include "../../sqlite/csv_loader.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <random>
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

// SQLite object type ("table", "view", "index") already using name, compared
// case-insensitively like SQLite identifiers. Triggers live in their own
// namespace and never conflict with a table name.
std::optional<std::string> ExistingSchemaObjectType(SQLite::Database& db,
                                                    const std::string& name) {
  SQLite::Statement query(
      db,
      "SELECT type FROM sqlite_schema WHERE name = ? COLLATE NOCASE AND type <> 'trigger' "
      "ORDER BY type = 'table' DESC LIMIT 1");
  query.bind(1, name);
  if (query.executeStep()) {
    return query.getColumn(0).getString();
  }
  return std::nullopt;
}

std::string StagingTableName(const std::string& table_name) {
  std::mt19937_64 rng{std::random_device{}()};
  return table_name + "__csvzall_load_" + std::to_string(rng());
}

}  // namespace

class SqlExportCommand : public CsvInputCommand {
public:
  SqlExportCommand(const std::string& input_path,
                   const std::string& dest_path,
                   const std::string& table_name,
                   SqlExportTableMode mode,
                   std::istream& input,
                   const RunOptions& options,
                   const LoggerCallbacks& logger,
                   RunStats& stats)
      : CsvInputCommand(input, options, logger, stats),
        input_path_(input_path),
        dest_path_(dest_path),
        table_name_(table_name),
        mode_(mode) {}

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
    if (destination_exists && mode_ == SqlExportTableMode::CreateDatabase) {
      if (logger().error) {
        logger().error("sql: destination already exists: " + db_path +
                       ". Remove it first, choose a different --dest path, or pass "
                       "--add-table to add a table to it.");
      }
      return 1;
    }

    std::unique_ptr<SQLite::Database> db;
    std::string load_table = table_name_;
    bool replacing = false;
    // Delete only a file this run created; never an existing database.
    auto fail = [&]() {
      if (!destination_exists) {
        // Windows cannot unlink an open SQLite database. Close before cleanup.
        db.reset();
        std::error_code ec;
        std::filesystem::remove(db_path, ec);
      } else if (db && load_table != table_name_) {
        try {
          db->exec("DROP TABLE IF EXISTS " + sqlite::QuoteIdentifier(load_table));
        } catch (const SQLite::Exception&) {
        }
      }
      return 1;
    };

    try {
      db = std::make_unique<SQLite::Database>(
          db_path, destination_exists ? SQLite::OPEN_READWRITE
                                      : SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

      if (destination_exists) {
        if (const auto type = ExistingSchemaObjectType(*db, table_name_)) {
          if (mode_ == SqlExportTableMode::AddTable) {
            if (logger().error) {
              logger().error("sql: " + *type + " already exists in " + db_path + ": " +
                             table_name_ + ". Pass --replace-table to replace it.");
            }
            return 1;
          }
          if (*type != "table") {
            if (logger().error) {
              logger().error("sql: " + table_name_ + " in " + db_path + " is a " + *type +
                             ", not a table; refusing to replace it.");
            }
            return 1;
          }
          // Load beside the old table so a failed load leaves it untouched.
          replacing = true;
          load_table = StagingTableName(table_name_);
        }
      }

      // An existing database belongs to the user: keep SQLite journaling on so
      // a crash cannot corrupt it, and never switch a WAL database's mode.
      RunOptions load_options = options();
      if (destination_exists) {
        load_options.sqlite_journal_enabled = true;
      }
      if (!sqlite::LoadCsvIntoTableWithInferredAffinities(
              [this]() -> csv::CSVReader& { return reader(); }, headers(), *db, load_table,
              [this]() { return reset_reader() == 0; },
              MakeCsvLoadOptions(load_options), MakeSqliteLogCallbacks(logger()))) {
        return fail();
      }

      if (replacing) {
        // Legacy rename leaves views and triggers that name the old table
        // pointing at the replacement instead of failing the schema check.
        db->exec("PRAGMA legacy_alter_table = ON");
        SQLite::Transaction swap(*db);
        db->exec("DROP TABLE " + sqlite::QuoteIdentifier(table_name_));
        db->exec("ALTER TABLE " + sqlite::QuoteIdentifier(load_table) + " RENAME TO " +
                 sqlite::QuoteIdentifier(table_name_));
        swap.commit();
        db->exec("PRAGMA legacy_alter_table = OFF");
        load_table = table_name_;
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
      return fail();
    }

    return 0;
  }

private:
  std::string input_path_;
  std::string dest_path_;
  std::string table_name_;
  SqlExportTableMode mode_;
};

int RunSqlExport(const std::string& input_path,
                 const std::string& dest_path,
                 const std::string& table_name,
                 std::istream& input,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats) {
  return RunSqlExport(input_path, dest_path, table_name, SqlExportTableMode::CreateDatabase,
                      input, options, logger, stats);
}

int RunSqlExport(const std::string& input_path,
                 const std::string& dest_path,
                 const std::string& table_name,
                 SqlExportTableMode mode,
                 std::istream& input,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats) {
  SqlExportCommand cmd(input_path, dest_path, table_name, mode, input, options, logger, stats);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

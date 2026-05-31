#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

namespace csvzall::sqlite {

struct SqliteDbOpenOptions {
  std::string db_path;
  bool input_is_stdin = false;
  std::string input_path;
  std::size_t threshold_mb = 256;
};

// RAII wrapper around SQLite::Database.
//
// Optionally owns a temporary on-disk file: when owns_temp_file is true the
// file is deleted (best-effort) when this object is destroyed.  This covers
// normal exit and std::terminate; SIGKILL may leave orphaned files in the
// system temp directory.
class SqliteDb {
 public:
  // Opens or creates a database at path.
  // path == ":memory:" creates an in-process in-memory database.
  // If owns_temp_file is true, path is deleted when this object is destroyed.
  explicit SqliteDb(const std::string& path, bool owns_temp_file = false);

  ~SqliteDb();

  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;

  SqliteDb(SqliteDb&&) noexcept;
  SqliteDb& operator=(SqliteDb&&) = delete;

  SQLite::Database& db();

 private:
  std::unique_ptr<SQLite::Database> db_;
  std::string temp_path_;  // non-empty => we own the file and must delete it
};

// Factory: choose in-memory vs temp-file database according to RunOptions and
// the size of the input file.
//
// Decision rules (evaluated in order):
//   1. options.sqlite_db_path non-empty  -> use that path as-is (not deleted).
//   2. options.input_is_stdin or options.input_path empty/"-" -> :memory:
//   3. file_size(options.input_path) <= options.sqlite_threshold_mb MB -> :memory:
//   4. otherwise -> temp-file database in system temp dir (deleted on exit).
SqliteDb OpenSqliteDb(const SqliteDbOpenOptions& options);

// Register csvzall SQLite scalar functions that must be available anywhere
// CSV-backed SQL executes.
void RegisterSqliteRegexFunctions(SQLite::Database& db);

}  // namespace csvzall::sqlite

namespace csvzall::pipeline {
namespace sqlite = ::csvzall::sqlite;
}

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SQLiteCpp/SQLiteCpp.h>

#include "sql_functions.hpp"

namespace csvzall::sqlite {

struct SqliteDbOpenOptions {
  std::string db_path;
  bool input_is_stdin = false;
  std::string input_path;
  std::size_t threshold_mb = 256;
  // Further inputs loaded into the same database (multi-table queries). Their
  // sizes count toward threshold_mb; any stdin entry forces in-memory.
  std::vector<std::string> additional_input_paths;
  // Native extensions to load after csvzall's functions are registered. Only
  // set from local CLI flags, never from shared chart or viewer config.
  std::vector<std::string> load_extensions;
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
//   3. combined size of input_path and additional_input_paths <=
//      options.sqlite_threshold_mb MB -> :memory:
//   4. otherwise -> temp-file database in system temp dir (deleted on exit).
// Every database gets RegisterCsvzallSqliteFunctions(); load_extensions are then
// loaded with LoadSqliteExtensions().
SqliteDb OpenSqliteDb(const SqliteDbOpenOptions& options);

}  // namespace csvzall::sqlite

namespace csvzall::pipeline {
namespace sqlite = ::csvzall::sqlite;
}

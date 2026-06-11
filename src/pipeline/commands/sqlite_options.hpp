#pragma once

#include "../../sqlite/csv_loader.hpp"
#include "../../sqlite/sqlite_db.hpp"

#include "../../pipeline_types.hpp"

namespace csvzall::pipeline::commands {

inline ::csvzall::sqlite::SqliteDbOpenOptions MakeSqliteDbOpenOptions(
    const RunOptions& options) {
  return {
      options.sqlite_db_path,
      options.input_is_stdin,
      options.input_path,
      options.sqlite_threshold_mb,
  };
}

inline ::csvzall::sqlite::CsvLoadOptions MakeCsvLoadOptions(const RunOptions& options) {
  return {options.sqlite_journal_enabled};
}

inline ::csvzall::sqlite::SqliteLogCallbacks MakeSqliteLogCallbacks(
    const LoggerCallbacks& logger) {
  return {logger.error, logger.verbose};
}

}  // namespace csvzall::pipeline::commands

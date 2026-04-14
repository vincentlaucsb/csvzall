#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace csvzall::pipeline {

struct RunOptions {
  bool single_threaded = false;
  bool input_is_stdin = false;
  bool exact_column_matching = false;
  // Field delimiter for the input (and output for streaming commands).
  // nullopt = auto-detect from {',', '|', '\t', ';', '^'}.
  // A value forces the exact delimiter (e.g. '\t' for TSV).
  std::optional<char> delimiter = std::nullopt;
  // SQLite memory strategy: load into :memory: if file is at or below this
  // threshold; otherwise write a temporary on-disk database that is deleted on
  // exit.  Overridable via --sqlite-threshold-mb <n>.
  std::size_t sqlite_threshold_mb = 256;
  // Explicit SQLite database path.  When non-empty, the database is opened at
  // this path and is NOT deleted on exit (the caller owns it).  Overrides the
  // threshold check.  Mostly useful for stdin input where file size is unknown.
  std::string sqlite_db_path;
  // Absolute path of the input file currently being processed.  Empty or "-"
  // means stdin.  Used by the SQLite layer to decide in-memory vs temp-file.
  std::string input_path;
};

struct RunStats {
  std::uint64_t rows_processed = 0;
  std::uint64_t bytes_processed = 0;
};

struct LoggerCallbacks {
  std::function<void(const std::string&)> error;
  std::function<void(const std::string&)> verbose;
};

}  // namespace csvzall::pipeline

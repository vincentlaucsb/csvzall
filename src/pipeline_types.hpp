#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace csvzall::pipeline {

enum class ViewModeSelection {
  Auto,
  Materialized,
  Paged,
};

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
  // ZIP member to read when input_path points to a .zip archive. Empty means
  // auto-select only when the archive contains exactly one file entry.
  std::string zip_entry;
  // Bulk-load safety/perf tradeoff for CSV->SQLite inserts.
  // false (default): disable SQLite journaling during load for speed.
  // true: keep default SQLite journal mode (safer for crash recovery).
  bool sqlite_journal_enabled = false;
  // PostgreSQL COPY producer chunk size. Larger values can reduce queue churn
  // but increase peak memory; 10k was a good default on large local loads.
  std::size_t postgres_copy_batch_rows = 10000;
  // Number of PostgreSQL COPY workers. Values above 1 do not preserve physical
  // insertion order and use one PostgreSQL connection per worker.
  std::size_t postgres_parallel_copy_workers = 1;
  // Viewer loading strategy. Auto materializes ordinary local files at or below
  // view_materialize_threshold_mb and uses row-offset paging for larger files.
  ViewModeSelection view_mode = ViewModeSelection::Auto;
  std::size_t view_materialize_threshold_mb = 200;
  // Enable csvzall view's explicit editable mode. Editing always uses
  // materialized local-file data so save can rewrite the CSV deterministically.
  bool view_edit = false;
  // Optional development override for first-party viewer assets. When set,
  // view serves index.html, viewer.css, viewer.js, and viewer modules from this
  // directory on each request instead of using embedded copies.
  std::string view_asset_dir;
};

struct RunStats {
  std::uint64_t rows_processed = 0;
  std::uint64_t bytes_processed = 0;
};

struct LoggerCallbacks {
  std::function<void(const std::string&)> error;
  std::function<void(const std::string&)> verbose;
  std::function<void(const std::string&)> info;
  std::function<void(const std::string&, std::uint64_t)> progress_start;
  std::function<void(std::uint64_t)> progress_update;
  std::function<void()> progress_finish;
};

}  // namespace csvzall::pipeline

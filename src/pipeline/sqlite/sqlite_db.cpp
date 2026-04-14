#include "sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <random>
#include <string>

namespace csvzall::pipeline::sqlite {

namespace {

std::string MakeTempPath() {
  std::mt19937_64 rng{std::random_device{}()};
  const std::uint64_t rand_val = rng();
  const std::string name =
      "csvzall_" + std::to_string(rand_val) + ".db";
  return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

SqliteDb::SqliteDb(const std::string& path, bool owns_temp_file)
    : db_(std::make_unique<SQLite::Database>(
          path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)),
      temp_path_(owns_temp_file ? path : std::string{}) {}

SqliteDb::~SqliteDb() {
  db_.reset();  // Close DB before touching the file on disk.
  if (!temp_path_.empty()) {
    std::error_code ec;
    std::filesystem::remove(temp_path_, ec);  // Best-effort; ignore errors.
  }
}

SqliteDb::SqliteDb(SqliteDb&& other) noexcept
    : db_(std::move(other.db_)),
      temp_path_(std::move(other.temp_path_)) {
  other.temp_path_.clear();  // Source must not delete the file on its destroy.
}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept {
  if (this != &other) {
    db_.reset();
    if (!temp_path_.empty()) {
      std::error_code ec;
      std::filesystem::remove(temp_path_, ec);
    }
    db_ = std::move(other.db_);
    temp_path_ = std::move(other.temp_path_);
    other.temp_path_.clear();
  }
  return *this;
}

SQLite::Database& SqliteDb::db() {
  return *db_;
}

SqliteDb OpenSqliteDb(const RunOptions& options) {
  // Rule 1: explicit user-provided path (not managed by us).
  if (!options.sqlite_db_path.empty()) {
    return SqliteDb(options.sqlite_db_path, /*owns_temp_file=*/false);
  }

  // Rule 2: stdin or no path — size is unknown, always use in-memory.
  const bool is_stdin =
      options.input_is_stdin ||
      options.input_path.empty() ||
      options.input_path == "-";
  if (is_stdin) {
    return SqliteDb(":memory:", /*owns_temp_file=*/false);
  }

  // Rule 3: compare file size to threshold.
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(options.input_path, ec);
  if (ec) {
    // Cannot stat the file (e.g. a named pipe disguised as a path) — fall back.
    return SqliteDb(":memory:", /*owns_temp_file=*/false);
  }

  const auto threshold_bytes =
      static_cast<std::uintmax_t>(options.sqlite_threshold_mb) * 1024ULL * 1024ULL;

  if (file_size <= threshold_bytes) {
    return SqliteDb(":memory:", /*owns_temp_file=*/false);
  }

  // Rule 4: above threshold — use a self-deleting temp file.
  const std::string temp_path = MakeTempPath();
  return SqliteDb(temp_path, /*owns_temp_file=*/true);
}

}  // namespace csvzall::pipeline::sqlite

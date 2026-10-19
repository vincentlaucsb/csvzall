#include "sqlite_db.hpp"

#include "sql_functions.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <optional>
#include <random>
#include <string>

namespace csvzall::sqlite {

namespace {

std::string MakeTempPath() {
  std::mt19937_64 rng{std::random_device{}()};
  const std::uint64_t rand_val = rng();
  const std::string name =
      "csvzall_" + std::to_string(rand_val) + ".db";
  return (std::filesystem::temp_directory_path() / name).string();
}

bool IsStdinPath(const std::string& path) {
  return path.empty() || path == "-";
}

// Total size of every input that will be loaded, or nullopt when any input is
// stdin or cannot be inspected (e.g. a named pipe disguised as a path).
std::optional<std::uintmax_t> TotalInputBytes(const SqliteDbOpenOptions& options) {
  if (options.input_is_stdin || IsStdinPath(options.input_path)) {
    return std::nullopt;
  }
  std::uintmax_t total = 0;
  auto add = [&total](const std::string& path) {
    if (IsStdinPath(path)) {
      return false;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
      return false;
    }
    total += size;
    return true;
  };
  if (!add(options.input_path)) {
    return std::nullopt;
  }
  for (const auto& path : options.additional_input_paths) {
    if (!add(path)) {
      return std::nullopt;
    }
  }
  return total;
}

SqliteDb OpenWithExtensions(const std::string& path,
                            bool owns_temp_file,
                            const SqliteDbOpenOptions& options) {
  SqliteDb db(path, owns_temp_file);
  LoadSqliteExtensions(db.db(), options.load_extensions);
  return db;
}

}  // namespace

SqliteDb::SqliteDb(const std::string& path, bool owns_temp_file)
    : db_(std::make_unique<SQLite::Database>(
          path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)),
      temp_path_(owns_temp_file ? path : std::string{}) {
  RegisterCsvzallSqliteFunctions(*db_);
}

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

SQLite::Database& SqliteDb::db() {
  return *db_;
}

SqliteDb OpenSqliteDb(const SqliteDbOpenOptions& options) {
  // Rule 1: explicit user-provided path (not managed by us).
  if (!options.db_path.empty()) {
    return OpenWithExtensions(options.db_path, /*owns_temp_file=*/false, options);
  }

  // Rule 2: stdin or no path — size is unknown, always use in-memory.
  // Rule 3: compare the combined input size to the threshold.
  const auto input_bytes = TotalInputBytes(options);
  const auto threshold_bytes =
      static_cast<std::uintmax_t>(options.threshold_mb) * 1024ULL * 1024ULL;
  if (!input_bytes || *input_bytes <= threshold_bytes) {
    return OpenWithExtensions(":memory:", /*owns_temp_file=*/false, options);
  }

  // Rule 4: above threshold — use a self-deleting temp file.
  return OpenWithExtensions(MakeTempPath(), /*owns_temp_file=*/true, options);
}

}  // namespace csvzall::sqlite

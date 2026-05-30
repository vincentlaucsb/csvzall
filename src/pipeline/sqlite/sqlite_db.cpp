#include "sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <filesystem>
#include <random>
#include <regex>
#include <string>

namespace csvzall::pipeline::sqlite {

namespace {

struct RegexFunctionArgs {
  int value_index;
  int pattern_index;
};

struct CachedRegex {
  std::string pattern;
  std::regex compiled;
};

constexpr RegexFunctionArgs kRegexpOperatorArgs{1, 0};
constexpr RegexFunctionArgs kRegexpLikeArgs{0, 1};

std::string SqliteValueString(sqlite3_value* value) {
  const auto* text = sqlite3_value_text(value);
  const int byte_count = sqlite3_value_bytes(value);
  if (!text || byte_count <= 0) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(text),
                     static_cast<std::size_t>(byte_count));
}

std::regex MakeRegex(const std::string& pattern) {
  auto flags = std::regex_constants::ECMAScript;
  std::string normalized = pattern;
  if (normalized.rfind("(?i)", 0) == 0) {
    flags |= std::regex_constants::icase;
    normalized.erase(0, 4);
  }
  return std::regex(normalized, flags);
}

CachedRegex* CachedRegexFor(sqlite3_context* context,
                            sqlite3_value* pattern_value,
                            int pattern_index) {
  const std::string pattern = SqliteValueString(pattern_value);
  if (auto* cached = static_cast<CachedRegex*>(sqlite3_get_auxdata(context, pattern_index))) {
    if (cached->pattern == pattern) {
      return cached;
    }
  }

  auto* cached = new CachedRegex{pattern, MakeRegex(pattern)};
  sqlite3_set_auxdata(context, pattern_index, cached, [](void* ptr) {
    delete static_cast<CachedRegex*>(ptr);
  });
  return cached;
}

void RegexMatchFunction(sqlite3_context* context, int argc, sqlite3_value** argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "regexp requires exactly two arguments", -1);
    return;
  }

  const auto* args = static_cast<const RegexFunctionArgs*>(sqlite3_user_data(context));
  if (!args) {
    sqlite3_result_error(context, "regexp internal configuration error", -1);
    return;
  }

  if (sqlite3_value_type(argv[args->value_index]) == SQLITE_NULL ||
      sqlite3_value_type(argv[args->pattern_index]) == SQLITE_NULL) {
    sqlite3_result_int(context, 0);
    return;
  }

  try {
    const auto* cached = CachedRegexFor(context, argv[args->pattern_index], args->pattern_index);
    const std::string value = SqliteValueString(argv[args->value_index]);
    sqlite3_result_int(context, std::regex_search(value, cached->compiled) ? 1 : 0);
  } catch (const std::regex_error& ex) {
    const std::string message = std::string("invalid regex pattern: ") + ex.what();
    sqlite3_result_error(context, message.c_str(), -1);
  }
}

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
      temp_path_(owns_temp_file ? path : std::string{}) {
  RegisterSqliteRegexFunctions(*db_);
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

void RegisterSqliteRegexFunctions(SQLite::Database& db) {
  db.createFunction("regexp", 2, true,
                    const_cast<RegexFunctionArgs*>(&kRegexpOperatorArgs),
                    RegexMatchFunction);
  db.createFunction("regexp_like", 2, true,
                    const_cast<RegexFunctionArgs*>(&kRegexpLikeArgs),
                    RegexMatchFunction);
}

}  // namespace csvzall::pipeline::sqlite

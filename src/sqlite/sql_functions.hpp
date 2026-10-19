#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace SQLite {
class Database;
}

namespace csvzall::sqlite {

/**
 * @brief Register csvzall's SQL functions on a connection.
 *
 * Adds the compiled-in sqlean modules (crypto, fuzzy, regexp, stats, text,
 * uuid), `sqlean_version()`, and csvzall's `parse_date(value [, order])` and
 * `date_precision(value [, order])`. The regexp module provides `REGEXP`,
 * `regexp(pattern, value)`, `regexp_like`, `regexp_substr`, `regexp_capture`,
 * and `regexp_replace` with PCRE2 syntax in UTF-8 + Unicode-property mode.
 * SQLite's own built-in functions keep their standard behavior: csvzall builds
 * sqlean without its aliases that would shadow them (`ltrim`, `rtrim`,
 * `concat`, `concat_ws`, `octet_length`; see cmake/sqlean_patches.cmake).
 * @see @ref sqlite_capabilities "SQLite capabilities" for SQL examples and limits.
 */
void RegisterCsvzallSqliteFunctions(SQLite::Database& db);

// Compatibility name from before csvzall registered more than regex functions.
inline void RegisterSqliteRegexFunctions(SQLite::Database& db) {
  RegisterCsvzallSqliteFunctions(db);
}

/// The sqlean release compiled into csvzall, also returned by `sqlean_version()`.
std::string_view SqleanVersion();

/**
 * @brief Load native SQLite extensions into one connection.
 *
 * Extension loading is enabled through the C API only
 * (SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION) and disabled again afterwards, so the
 * SQL-level `load_extension()` function stays unauthorized. Callers must only
 * pass paths chosen by the local user, never paths read from shared config.
 * @throws std::runtime_error naming the path when a load fails.
 */
void LoadSqliteExtensions(SQLite::Database& db, const std::vector<std::string>& paths);

}  // namespace csvzall::sqlite

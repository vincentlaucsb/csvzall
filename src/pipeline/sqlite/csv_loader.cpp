#include "csv_loader.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <csv.hpp>

#include <sstream>
#include <string>
#include <vector>

#ifndef CSVZALL_SQLITE_BIND_USE_STRING_COPY
#define CSVZALL_SQLITE_BIND_USE_STRING_COPY 1
#endif

namespace csvzall::pipeline::sqlite {

// Returns a double-quoted SQL identifier with embedded " escaped by doubling.
std::string QuoteIdentifier(const std::string& name) {
  std::string out;
  out.reserve(name.size() + 2);
  out += '"';
  for (const char c : name) {
    out += c;
    if (c == '"') out += c;
  }
  out += '"';
  return out;
}

namespace {

// All columns use NUMERIC affinity. SQLite stores each value as INTEGER,
// REAL, or TEXT depending on the cell content — no per-column inference
// needed. "5" → INTEGER 5 → reads back as "5"; "5.5" → REAL 5.5 → "5.5";
// "active" → TEXT "active". Numeric comparisons in WHERE work correctly.
std::string BuildCreateTable(const std::string& table_name,
                              const std::vector<std::string>& headers) {
  std::ostringstream sql;
  sql << "CREATE TABLE " << QuoteIdentifier(table_name) << " (";
  for (std::size_t i = 0; i < headers.size(); ++i) {
    if (i > 0) sql << ", ";
    sql << QuoteIdentifier(headers[i]) << " NUMERIC";
  }
  sql << ')';
  return sql.str();
}

std::string BuildInsert(const std::string& table_name, std::size_t col_count) {
  std::ostringstream sql;
  sql << "INSERT INTO " << QuoteIdentifier(table_name) << " VALUES (";
  for (std::size_t i = 0; i < col_count; ++i) {
    if (i > 0) sql << ", ";
    sql << '?';
  }
  sql << ')';
  return sql.str();
}

}  // namespace

bool LoadCsvIntoTable(csv::CSVReader& reader,
                      const std::vector<std::string>& headers,
                      SQLite::Database& db,
                      const std::string& table_name,
                      const RunOptions& options,
                      const LoggerCallbacks& logger) {
  if (headers.empty()) {
    if (logger.error) logger.error("Cannot load CSV into SQLite: no column headers.");
    return false;
  }

  try {
    if (!options.sqlite_journal_enabled) {
      db.exec("PRAGMA journal_mode = OFF");
    }

    db.exec(BuildCreateTable(table_name, headers));

    SQLite::Statement stmt(db, BuildInsert(table_name, headers.size()));
    SQLite::Transaction tx(db);

    for (auto& row : reader) {
      const int n = static_cast<int>(headers.size());
      for (int i = 0; i < n; ++i) {
#if CSVZALL_SQLITE_BIND_USE_STRING_COPY
        stmt.bind(i + 1, row[i].get_sv().data()
                      ? std::string(row[i].get_sv()) : std::string{});
#else
        const auto sv = row[i].get_sv();
        stmt.bind(i + 1, sv.data(), static_cast<int>(sv.size()));
#endif
      }
      stmt.exec();
      stmt.reset();
    }

    tx.commit();
  } catch (const SQLite::Exception& ex) {
    if (logger.error) {
      logger.error(std::string("SQLite error while loading CSV: ") + ex.what());
    }
    return false;
  }

  return true;
}

}  // namespace csvzall::pipeline::sqlite

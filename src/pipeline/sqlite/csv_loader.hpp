#pragma once

#include <string>
#include <vector>

#include "../../pipeline_types.hpp"

// Forward declarations — consumers that need to call db methods must include
// the full SQLiteCpp and csv-parser headers themselves.
namespace csv { class CSVReader; }
namespace SQLite { class Database; }

namespace csvzall::pipeline::sqlite {

// Returns a double-quoted SQL identifier with embedded '"' escaped by doubling.
// E.g. my"col  →  "my""col"
// Use this wherever column or table names appear in generated SQL.
std::string QuoteIdentifier(const std::string& name);

// Load all rows from reader into a newly-created SQLite table.
//
// Schema conventions:
//   - All columns use NUMERIC affinity. SQLite stores each cell value as
//     INTEGER, REAL, or TEXT based on the cell content — no per-column
//     inference needed.  "5" stays "5"; "5.5" stays "5.5"; "active" stays
//     "active". Numeric WHERE comparisons work correctly.
//   - Column names are double-quoted in generated DDL; embedded " are escaped
//     by doubling ("my""col").
//
// Parameters:
//   reader      — positioned after the header row (csv-parser has parsed it).
//   headers     — the column names to use; must match the row field count.
//   db          — open, writable SQLite::Database.
//   table_name  — name of the table to create (must not already exist).
//   logger      — error/verbose callbacks; only error is used on failure.
//
// Returns true on success, false on any SQLite or structural error.
bool LoadCsvIntoTable(csv::CSVReader& reader,
                      const std::vector<std::string>& headers,
                      SQLite::Database& db,
                      const std::string& table_name,
                      const RunOptions& options,
                      const LoggerCallbacks& logger);

}  // namespace csvzall::pipeline::sqlite

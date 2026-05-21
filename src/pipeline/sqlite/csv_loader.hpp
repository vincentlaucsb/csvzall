#pragma once

#include <string>
#include <vector>

#include "../../pipeline_types.hpp"

// Forward declarations — consumers that need to call db methods must include
// the full SQLiteCpp and csv-parser headers themselves.
namespace csv { class CSVReader; }
namespace SQLite { class Database; }

namespace csvzall::pipeline::sqlite {

enum class SqliteColumnAffinity {
  Numeric,
  Text,
};

// Returns a double-quoted SQL identifier with embedded '"' escaped by doubling.
// E.g. my"col  →  "my""col"
// Use this wherever column or table names appear in generated SQL.
std::string QuoteIdentifier(const std::string& name);

// Infer SQLite column affinities from the remaining rows in reader.
//
// Numeric columns stay NUMERIC so SQL comparisons and arithmetic keep working.
// Columns with text, booleans, timestamps, or CSV_BIGINT values become TEXT so
// projections preserve exact lexical cell values such as long identifier IDs.
std::vector<SqliteColumnAffinity> InferColumnAffinities(
    csv::CSVReader& reader,
    const std::vector<std::string>& headers);

// Load all rows from reader into a newly-created SQLite table.
//
// Schema conventions:
//   - Column affinity is supplied by the caller, normally from
//     InferColumnAffinities() followed by a reader reset. NUMERIC columns let
//     SQLite compare numeric values naturally; TEXT columns preserve exact CSV
//     cells for identifiers and mixed/non-numeric data.
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
                      const std::vector<SqliteColumnAffinity>& column_affinities,
                      const RunOptions& options,
                      const LoggerCallbacks& logger);

}  // namespace csvzall::pipeline::sqlite

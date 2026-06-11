#include "csv_loader.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <csv.hpp>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef CSVZALL_SQLITE_BIND_USE_STRING_COPY
#define CSVZALL_SQLITE_BIND_USE_STRING_COPY 1
#endif

namespace csvzall::sqlite {

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

struct AffinityStats {
  bool saw_numeric = false;
  bool needs_text = false;
};

bool IsSqliteNumericType(csv::DataType type) {
  return (type >= csv::DataType::CSV_INT8 && type <= csv::DataType::CSV_INT64) ||
         type == csv::DataType::CSV_DOUBLE;
}

void ObserveType(AffinityStats& stats, csv::DataType type) {
  if (type == csv::DataType::CSV_NULL) {
    return;
  }
  if (IsSqliteNumericType(type)) {
    stats.saw_numeric = true;
    return;
  }
  stats.needs_text = true;
}

const char* AffinitySql(SqliteColumnAffinity affinity) {
  switch (affinity) {
    case SqliteColumnAffinity::Numeric:
      return "NUMERIC";
    case SqliteColumnAffinity::Text:
    default:
      return "TEXT";
  }
}

std::string BuildCreateTable(const std::string& table_name,
                             const std::vector<std::string>& headers,
                             const std::vector<SqliteColumnAffinity>& column_affinities) {
  std::ostringstream sql;
  sql << "CREATE TABLE " << QuoteIdentifier(table_name) << " (";
  for (std::size_t i = 0; i < headers.size(); ++i) {
    if (i > 0) sql << ", ";
    sql << QuoteIdentifier(headers[i]) << ' ' << AffinitySql(column_affinities[i]);
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

std::vector<SqliteColumnAffinity> InferColumnAffinities(
    csv::CSVReader& reader,
    const std::vector<std::string>& headers) {
  std::vector<AffinityStats> stats(headers.size());

  for (auto& row : reader) {
    const std::size_t n = std::min(headers.size(), row.size());
    for (std::size_t i = 0; i < n; ++i) {
      auto field = row[i];
      ObserveType(stats[i], field.type());
    }
  }

  std::vector<SqliteColumnAffinity> affinities;
  affinities.reserve(headers.size());
  for (const auto& column_stats : stats) {
    affinities.push_back(column_stats.saw_numeric && !column_stats.needs_text
                             ? SqliteColumnAffinity::Numeric
                             : SqliteColumnAffinity::Text);
  }
  return affinities;
}

bool LoadCsvIntoTable(csv::CSVReader& reader,
                      const std::vector<std::string>& headers,
                      SQLite::Database& db,
                      const std::string& table_name,
                      const std::vector<SqliteColumnAffinity>& column_affinities,
                      const CsvLoadOptions& options,
                      const SqliteLogCallbacks& logger) {
  if (headers.empty()) {
    if (logger.error) logger.error("Cannot load CSV into SQLite: no column headers.");
    return false;
  }
  if (column_affinities.size() != headers.size()) {
    if (logger.error) logger.error("Cannot load CSV into SQLite: column affinity count mismatch.");
    return false;
  }

  try {
    if (!options.sqlite_journal_enabled) {
      db.exec("PRAGMA journal_mode = OFF");
    }

    db.exec(BuildCreateTable(table_name, headers, column_affinities));

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

bool LoadCsvIntoTableWithInferredAffinities(
    const std::function<csv::CSVReader&()>& reader,
    const std::vector<std::string>& headers,
    SQLite::Database& db,
    const std::string& table_name,
    const std::function<bool()>& reset_reader,
    const CsvLoadOptions& options,
    const SqliteLogCallbacks& logger) {
  const auto column_affinities = InferColumnAffinities(reader(), headers);
  if (!reset_reader()) {
    return false;
  }
  return LoadCsvIntoTable(reader(), headers, db, table_name, column_affinities, options, logger);
}

std::vector<std::string> StatementColumnNames(SQLite::Statement& statement) {
  const int column_count = statement.getColumnCount();
  std::vector<std::string> names;
  names.reserve(static_cast<std::size_t>(column_count));
  for (int i = 0; i < column_count; ++i) {
    names.emplace_back(statement.getColumnName(i));
  }
  return names;
}

std::vector<std::string> StatementRowValues(SQLite::Statement& statement) {
  const int column_count = statement.getColumnCount();
  std::vector<std::string> values;
  values.reserve(static_cast<std::size_t>(column_count));
  for (int i = 0; i < column_count; ++i) {
    values.emplace_back(
        statement.getColumn(i).isNull() ? std::string{} : statement.getColumn(i).getString());
  }
  return values;
}

std::uint64_t WriteStatementRowsAsCsv(SQLite::Statement& statement,
                                      const std::vector<std::string>& headers,
                                      std::ostream& output) {
  auto writer = csv::make_csv_writer(output).set_auto_flush(false);
  writer << headers;

  std::uint64_t rows_written = 0;
  while (statement.executeStep()) {
    writer << StatementRowValues(statement);
    ++rows_written;
  }

  writer.flush();
  return rows_written;
}

}  // namespace csvzall::sqlite

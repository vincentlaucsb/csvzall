#include "sql.hpp"

#include "../../common/column_lookup.hpp"
#include "../../../sqlite/csv_loader.hpp"
#include "../../../sqlite/sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <csv.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace csvzall::pipeline::commands::view_internal {
namespace {

using Json = nlohmann::ordered_json;

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

const char* AffinitySql(sqlite::SqliteColumnAffinity affinity) {
  switch (affinity) {
    case sqlite::SqliteColumnAffinity::Numeric:
      return "NUMERIC";
    case sqlite::SqliteColumnAffinity::Text:
    default:
      return "TEXT";
  }
}

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool StartsWithReadOnlySqlVerb(const std::string& sql) {
  const auto trimmed = common::Trim(sql);
  const auto lowered = Lowercase(trimmed);
  return lowered.starts_with("select") || lowered.starts_with("with");
}

bool HasAdditionalStatement(std::string_view sql) {
  const auto semicolon = sql.find(';');
  if (semicolon == std::string_view::npos) {
    return false;
  }
  const auto rest = sql.substr(semicolon + 1);
  return std::any_of(rest.begin(),
                     rest.end(),
                     [](unsigned char ch) { return std::isspace(ch) == 0; });
}

std::vector<sqlite::SqliteColumnAffinity> InferAffinitiesFromRows(
    const std::vector<std::string>& headers,
    const std::vector<std::vector<std::string>>& rows) {
  std::vector<AffinityStats> stats(headers.size());
  for (const auto& row : rows) {
    const auto n = std::min(headers.size(), row.size());
    for (std::size_t i = 0; i < n; ++i) {
      ObserveType(stats[i], csv::internals::data_type(row[i]));
    }
  }

  std::vector<sqlite::SqliteColumnAffinity> affinities;
  affinities.reserve(headers.size());
  for (const auto& column_stats : stats) {
    affinities.push_back(column_stats.saw_numeric && !column_stats.needs_text
                             ? sqlite::SqliteColumnAffinity::Numeric
                             : sqlite::SqliteColumnAffinity::Text);
  }
  return affinities;
}

std::string BuildCreateTableSql(const std::vector<std::string>& headers,
                                const std::vector<sqlite::SqliteColumnAffinity>& affinities) {
  std::ostringstream sql;
  sql << "CREATE TABLE " << sqlite::QuoteIdentifier(std::string(kViewerSqlTableName)) << " (";
  for (std::size_t i = 0; i < headers.size(); ++i) {
    if (i > 0) {
      sql << ", ";
    }
    sql << sqlite::QuoteIdentifier(headers[i]) << ' ' << AffinitySql(affinities[i]);
  }
  sql << ')';
  return sql.str();
}

std::string BuildInsertSql(std::size_t column_count) {
  std::ostringstream sql;
  sql << "INSERT INTO " << sqlite::QuoteIdentifier(std::string(kViewerSqlTableName)) << " VALUES (";
  for (std::size_t i = 0; i < column_count; ++i) {
    if (i > 0) {
      sql << ", ";
    }
    sql << '?';
  }
  sql << ')';
  return sql.str();
}

void LoadViewerDataIntoSqlite(SQLite::Database& db, const CsvViewData& data) {
  const auto rows = data.read_rows(0, data.row_count());
  const auto& headers = data.headers();
  const auto affinities = InferAffinitiesFromRows(headers, rows);
  db.exec("PRAGMA journal_mode = OFF");
  db.exec(BuildCreateTableSql(headers, affinities));

  SQLite::Statement insert(db, BuildInsertSql(headers.size()));
  SQLite::Transaction transaction(db);
  for (const auto& row : rows) {
    for (std::size_t i = 0; i < headers.size(); ++i) {
      insert.bind(static_cast<int>(i + 1), i < row.size() ? row[i] : std::string{});
    }
    insert.exec();
    insert.reset();
  }
  transaction.commit();
}

}  // namespace

std::string DefaultViewerSqlQuery() {
  return "SELECT *\nFROM " + std::string(kViewerSqlTableName) + "\nLIMIT 100;";
}

std::string BuildSqlQueryResultJson(const CsvViewData& data, std::string_view sql) {
  auto query_text = common::Trim(std::string(sql));
  if (query_text.empty()) {
    query_text = "SELECT * FROM " + std::string(kViewerSqlTableName);
  }
  if (!StartsWithReadOnlySqlVerb(query_text)) {
    throw std::runtime_error("SQL preview only supports SELECT queries.");
  }
  if (HasAdditionalStatement(query_text)) {
    throw std::runtime_error("SQL preview accepts one statement at a time.");
  }

  SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  sqlite::RegisterSqliteRegexFunctions(db);
  LoadViewerDataIntoSqlite(db, data);

  SQLite::Statement query(db, query_text);
  if (query.getColumnCount() <= 0) {
    throw std::runtime_error("SQL query must return a result set.");
  }

  Json rows = Json::array();
  while (query.executeStep()) {
    rows.push_back(sqlite::StatementRowValues(query));
  }

  Json result;
  result["ok"] = true;
  result["tableName"] = kViewerSqlTableName;
  result["columns"] = sqlite::StatementColumnNames(query);
  result["rows"] = std::move(rows);
  result["totalRows"] = result["rows"].size();
  return result.dump();
}

}  // namespace csvzall::pipeline::commands::view_internal

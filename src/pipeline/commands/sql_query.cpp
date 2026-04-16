#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../sqlite/csv_loader.hpp"
#include "../sqlite/sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <csv.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace csvzall::pipeline::commands {

class SqlQueryCsvCommand : public CsvTransformCommand {
public:
  SqlQueryCsvCommand(const std::string& sql_query,
                     const std::string& table_name,
                     std::istream& input,
                     std::ostream& output,
                     const RunOptions& options,
                     const LoggerCallbacks& logger,
                     RunStats& stats)
      : CsvTransformCommand(input, output, options, logger, stats),
        sql_query_(sql_query),
        table_name_(table_name) {}

protected:
  int run() override {
    const std::string query_text = common::Trim(sql_query_);
    if (query_text.empty()) {
      if (logger().error) {
        logger().error("sql query: --sql cannot be empty.");
      }
      return 1;
    }

    const std::string table_name_text = common::Trim(table_name_);
    if (table_name_text.empty()) {
      if (logger().error) {
        logger().error("sql query: --table cannot be empty.");
      }
      return 1;
    }

    sqlite::SqliteDb sdb = sqlite::OpenSqliteDb(options());
    if (!sqlite::LoadCsvIntoTable(reader(), headers(), sdb.db(), table_name_text, logger())) {
      return 1;
    }

    try {
      SQLite::Statement query(sdb.db(), query_text);
      const int col_count = query.getColumnCount();
      if (col_count <= 0) {
        if (logger().error) {
          logger().error("sql query: statement did not return a result set.");
        }
        return 1;
      }

      auto writer = csv::make_csv_writer_buffered(output());

      std::vector<std::string> out_headers;
      out_headers.reserve(static_cast<std::size_t>(col_count));
      for (int i = 0; i < col_count; ++i) {
        out_headers.emplace_back(query.getColumnName(i));
      }
      writer << out_headers;

      while (query.executeStep()) {
        std::vector<std::string> out_row;
        out_row.reserve(static_cast<std::size_t>(col_count));
        for (int i = 0; i < col_count; ++i) {
          out_row.emplace_back(
              query.getColumn(i).isNull() ? std::string{} : query.getColumn(i).getString());
        }
        writer << out_row;
        stats().rows_processed++;
      }

      writer.flush();
    } catch (const SQLite::Exception& ex) {
      if (logger().error) {
        logger().error(std::string("sql query: SQLite error: ") + ex.what());
      }
      return 1;
    }

    return 0;
  }

private:
  std::string sql_query_;
  std::string table_name_;
};

int RunSqlQueryCsv(const std::string& sql_query,
                   const std::string& table_name,
                   std::istream& input,
                   std::ostream& output,
                   const RunOptions& options,
                   const LoggerCallbacks& logger,
                   RunStats& stats) {
  SqlQueryCsvCommand cmd(sql_query, table_name, input, output, options, logger, stats);
  return cmd.execute();
}

SqlQueryInputKind DetectSqlQueryInputKind(const std::string& path) {
  if (path.empty() || path == "-") {
    return SqlQueryInputKind::kUnknown;
  }

  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (ext == ".csv" || ext == ".txt") {
    return SqlQueryInputKind::kCsv;
  }
  if (ext == ".db" || ext == ".sqlite" || ext == ".sqlite3") {
    return SqlQueryInputKind::kSqlite;
  }
  return SqlQueryInputKind::kUnknown;
}

int RunSqlQueryDb(const std::string& sql_query,
                  const std::string& db_path,
                  std::ostream& output,
                  const LoggerCallbacks& logger,
                  RunStats& stats) {
  const std::string query_text = common::Trim(sql_query);
  if (query_text.empty()) {
    if (logger.error) {
      logger.error("sql query: --sql cannot be empty.");
    }
    return 1;
  }
  if (db_path.empty()) {
    if (logger.error) {
      logger.error("sql query: database path cannot be empty.");
    }
    return 1;
  }

  try {
    SQLite::Database db(db_path, SQLite::OPEN_READONLY);
    SQLite::Statement query(db, query_text);

    const int col_count = query.getColumnCount();
    if (col_count <= 0) {
      if (logger.error) {
        logger.error("sql query: statement did not return a result set.");
      }
      return 1;
    }

    auto writer = csv::make_csv_writer_buffered(output);
    std::vector<std::string> out_headers;
    out_headers.reserve(static_cast<std::size_t>(col_count));
    for (int i = 0; i < col_count; ++i) {
      out_headers.emplace_back(query.getColumnName(i));
    }
    writer << out_headers;

    while (query.executeStep()) {
      std::vector<std::string> out_row;
      out_row.reserve(static_cast<std::size_t>(col_count));
      for (int i = 0; i < col_count; ++i) {
        out_row.emplace_back(
            query.getColumn(i).isNull() ? std::string{} : query.getColumn(i).getString());
      }
      writer << out_row;
      stats.rows_processed++;
    }

    writer.flush();
  } catch (const SQLite::Exception& ex) {
    if (logger.error) {
      logger.error(std::string("sql query: SQLite error: ") + ex.what());
    }
    return 1;
  }

  return 0;
}

}  // namespace csvzall::pipeline::commands

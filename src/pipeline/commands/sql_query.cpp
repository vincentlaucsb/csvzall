#include "commands.hpp"

#include "../common/column_lookup.hpp"
#include "../sqlite/csv_loader.hpp"
#include "../sqlite/sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <csv.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace csvzall::pipeline::commands {

namespace {

enum class TokenKind {
  Identifier,
  IntegerLiteral,
  FloatLiteral,
  Slash,
  Other,
};

struct Token {
  TokenKind kind;
  std::string text;
};

std::string StripSqlStringsAndComments(const std::string& sql) {
  enum class State {
    Normal,
    SingleQuote,
    DoubleQuote,
    LineComment,
    BlockComment,
  };

  State state = State::Normal;
  std::string out;
  out.reserve(sql.size());

  for (std::size_t i = 0; i < sql.size(); ++i) {
    const char c = sql[i];
    const char n = (i + 1 < sql.size()) ? sql[i + 1] : '\0';

    switch (state) {
      case State::Normal:
        if (c == '\'' ) {
          state = State::SingleQuote;
          out += ' ';
        } else if (c == '"') {
          state = State::DoubleQuote;
          out += ' ';
        } else if (c == '-' && n == '-') {
          state = State::LineComment;
          out += ' ';
          out += ' ';
          ++i;
        } else if (c == '/' && n == '*') {
          state = State::BlockComment;
          out += ' ';
          out += ' ';
          ++i;
        } else {
          out += c;
        }
        break;

      case State::SingleQuote:
        if (c == '\'' && n == '\'') {
          out += ' ';
          out += ' ';
          ++i;
        } else if (c == '\'') {
          state = State::Normal;
          out += ' ';
        } else {
          out += ' ';
        }
        break;

      case State::DoubleQuote:
        if (c == '"' && n == '"') {
          out += ' ';
          out += ' ';
          ++i;
        } else if (c == '"') {
          state = State::Normal;
          out += ' ';
        } else {
          out += ' ';
        }
        break;

      case State::LineComment:
        if (c == '\n') {
          state = State::Normal;
          out += '\n';
        } else {
          out += ' ';
        }
        break;

      case State::BlockComment:
        if (c == '*' && n == '/') {
          state = State::Normal;
          out += ' ';
          out += ' ';
          ++i;
        } else {
          out += ' ';
        }
        break;
    }
  }

  return out;
}

bool IsIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool IsIdentBody(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::vector<Token> TokenizeForDivisionCheck(const std::string& sql) {
  std::vector<Token> tokens;
  for (std::size_t i = 0; i < sql.size();) {
    const char c = sql[i];

    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }

    if (IsIdentStart(c)) {
      std::size_t j = i + 1;
      while (j < sql.size() && IsIdentBody(sql[j])) {
        ++j;
      }
      tokens.push_back({TokenKind::Identifier, sql.substr(i, j - i)});
      i = j;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::size_t j = i + 1;
      bool has_dot = false;
      bool has_exp = false;
      while (j < sql.size()) {
        const char x = sql[j];
        if (std::isdigit(static_cast<unsigned char>(x))) {
          ++j;
          continue;
        }
        if (x == '.' && !has_dot && !has_exp) {
          has_dot = true;
          ++j;
          continue;
        }
        if ((x == 'e' || x == 'E') && !has_exp) {
          has_exp = true;
          ++j;
          if (j < sql.size() && (sql[j] == '+' || sql[j] == '-')) {
            ++j;
          }
          continue;
        }
        break;
      }

      tokens.push_back({has_dot || has_exp ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral,
                        sql.substr(i, j - i)});
      i = j;
      continue;
    }

    if (c == '/') {
      tokens.push_back({TokenKind::Slash, "/"});
      ++i;
      continue;
    }

    tokens.push_back({TokenKind::Other, std::string(1, c)});
    ++i;
  }

  return tokens;
}

bool IsLikelyIntegerOperand(TokenKind kind) {
  return kind == TokenKind::Identifier || kind == TokenKind::IntegerLiteral;
}

std::string EscapeMarkdownTableCell(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    if (c == '\\') {
      escaped += "\\\\";
    } else if (c == '|') {
      escaped += "\\|";
    } else if (c == '\n') {
      escaped += "<br>";
    } else if (c != '\r') {
      escaped += c;
    }
  }
  return escaped;
}

void WriteMarkdownTable(std::ostream& output,
                        const std::vector<std::string>& headers,
                        const std::vector<std::vector<std::string>>& rows) {
  std::vector<std::string> escaped_headers;
  escaped_headers.reserve(headers.size());
  for (const auto& header : headers) {
    escaped_headers.push_back(EscapeMarkdownTableCell(header));
  }

  std::vector<std::vector<std::string>> escaped_rows;
  escaped_rows.reserve(rows.size());
  for (const auto& row : rows) {
    std::vector<std::string> escaped_row;
    escaped_row.reserve(row.size());
    for (const auto& cell : row) {
      escaped_row.push_back(EscapeMarkdownTableCell(cell));
    }
    escaped_rows.push_back(std::move(escaped_row));
  }

  std::vector<std::size_t> widths(escaped_headers.size(), 0);
  for (std::size_t i = 0; i < escaped_headers.size(); ++i) {
    widths[i] = escaped_headers[i].size();
  }
  for (const auto& row : escaped_rows) {
    for (std::size_t i = 0; i < row.size() && i < widths.size(); ++i) {
      widths[i] = std::max(widths[i], row[i].size());
    }
  }

  auto write_row = [&](const std::vector<std::string>& cells) {
    output << '|';
    for (std::size_t i = 0; i < widths.size(); ++i) {
      const std::string empty;
      const std::string& cell = i < cells.size() ? cells[i] : empty;
      output << ' ' << cell;
      for (std::size_t p = cell.size(); p < widths[i]; ++p) {
        output << ' ';
      }
      output << " |";
    }
    output << '\n';
  };

  write_row(escaped_headers);
  output << '|';
  for (const auto width : widths) {
    output << ' ';
    for (std::size_t p = 0; p < std::max<std::size_t>(width, 3); ++p) {
      output << '-';
    }
    output << " |";
  }
  output << '\n';

  for (const auto& row : escaped_rows) {
    write_row(row);
  }
}

int WriteQueryResult(SQLite::Statement& query,
                     const std::string& format,
                     std::ostream& output,
                     RunStats& stats,
                     const LoggerCallbacks& logger) {
  const int col_count = query.getColumnCount();
  if (col_count <= 0) {
    if (logger.error) {
      logger.error("sql query: statement did not return a result set.");
    }
    return 1;
  }

  std::vector<std::string> out_headers;
  out_headers.reserve(static_cast<std::size_t>(col_count));
  for (int i = 0; i < col_count; ++i) {
    out_headers.emplace_back(query.getColumnName(i));
  }

  if (format == "csv") {
    auto writer = csv::make_csv_writer(output).set_auto_flush(false);
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
    return 0;
  }

  if (format == "markdown") {
    std::vector<std::vector<std::string>> out_rows;
    while (query.executeStep()) {
      std::vector<std::string> out_row;
      out_row.reserve(static_cast<std::size_t>(col_count));
      for (int i = 0; i < col_count; ++i) {
        out_row.emplace_back(
            query.getColumn(i).isNull() ? std::string{} : query.getColumn(i).getString());
      }
      out_rows.push_back(std::move(out_row));
      stats.rows_processed++;
    }

    WriteMarkdownTable(output, out_headers, out_rows);
    return 0;
  }

  if (logger.error) {
    logger.error("sql query: --format must be csv or markdown.");
  }
  return 1;
}

}  // namespace

class SqlQueryCsvCommand : public CsvTransformCommand {
public:
  SqlQueryCsvCommand(const std::string& sql_query,
                     const std::string& table_name,
                     const std::string& format,
                     std::istream& input,
                     std::ostream& output,
                     const RunOptions& options,
                     const LoggerCallbacks& logger,
                     RunStats& stats)
      : CsvTransformCommand(input, output, options, logger, stats),
        sql_query_(sql_query),
        table_name_(table_name),
        format_(format) {}

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
    const auto column_affinities = sqlite::InferColumnAffinities(reader(), headers());
    if (reset_reader() != 0) {
      return 1;
    }
    if (!sqlite::LoadCsvIntoTable(
            reader(), headers(), sdb.db(), table_name_text, column_affinities, options(), logger())) {
      return 1;
    }

    try {
      SQLite::Statement query(sdb.db(), query_text);
      return WriteQueryResult(query, format_, output(), stats(), logger());
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
  std::string format_;
};

int RunSqlQueryCsv(const std::string& sql_query,
                   const std::string& table_name,
                   const std::string& format,
                   std::istream& input,
                   std::ostream& output,
                   const RunOptions& options,
                   const LoggerCallbacks& logger,
                   RunStats& stats) {
  SqlQueryCsvCommand cmd(sql_query, table_name, format, input, output, options, logger, stats);
  return cmd.execute();
}

SqlQueryInputKind DetectSqlQueryInputKind(const std::string& path) {
  if (path.empty() || path == "-") {
    return SqlQueryInputKind::kUnknown;
  }

  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (ext == ".csv" || ext == ".txt" || ext == ".gz" || ext == ".zip") {
    return SqlQueryInputKind::kCsv;
  }
  if (ext == ".db" || ext == ".sqlite" || ext == ".sqlite3") {
    return SqlQueryInputKind::kSqlite;
  }
  return SqlQueryInputKind::kUnknown;
}

int RunSqlQueryDb(const std::string& sql_query,
                  const std::string& db_path,
                  const std::string& format,
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
    sqlite::RegisterSqliteRegexFunctions(db);
    SQLite::Statement query(db, query_text);
    return WriteQueryResult(query, format, output, stats, logger);
  } catch (const SQLite::Exception& ex) {
    if (logger.error) {
      logger.error(std::string("sql query: SQLite error: ") + ex.what());
    }
    return 1;
  }

  return 0;
}

bool ShouldWarnIntegerDivision(const std::string& sql_query) {
  const std::string sanitized = StripSqlStringsAndComments(sql_query);
  const std::vector<Token> tokens = TokenizeForDivisionCheck(sanitized);

  if (tokens.size() < 3) {
    return false;
  }

  for (std::size_t i = 1; i + 1 < tokens.size(); ++i) {
    if (tokens[i].kind != TokenKind::Slash) {
      continue;
    }
    const TokenKind lhs = tokens[i - 1].kind;
    const TokenKind rhs = tokens[i + 1].kind;
    if (IsLikelyIntegerOperand(lhs) && IsLikelyIntegerOperand(rhs)) {
      return true;
    }
  }

  return false;
}

}  // namespace csvzall::pipeline::commands

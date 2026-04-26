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

}  // namespace

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
    if (!sqlite::LoadCsvIntoTable(reader(), headers(), sdb.db(), table_name_text, options(), logger())) {
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

      auto writer = csv::make_csv_writer(output()).set_auto_flush(false);

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

    auto writer = csv::make_csv_writer(output).set_auto_flush(false);
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

#include <SQLiteCpp/SQLiteCpp.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"
#ifdef CSVZALL_HAVE_COMPRESSED_INPUT
#include <zlib.h>
#endif

using namespace csvzall;
namespace {
struct SqlWorkspace {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("csvzall_sql_edges_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  SqlWorkspace() { std::filesystem::create_directories(root); }
  ~SqlWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
  std::string path(const std::string& name) const { return (root / name).string(); }
};
}  // namespace

TEST_CASE("SQL export destination inference and explicit override", "[sql-edges]") {
  SqlWorkspace files;
  for (const auto& name : {"input.csv", "input.txt", "input"}) {
    DYNAMIC_SECTION(name) {
      std::istringstream input("x\n1\n2\n");
      pipeline::RunStats stats;
      const auto source = files.path(name);
      const auto expected =
          files.path(std::string(name) == "input.csv" ? "input.db" : std::string(name) + ".db");
      REQUIRE(pipeline::RunSqlExport(source, "", "data", input, tests::MakeTestOptions(),
                                     tests::MakeNullLogger(), stats) == 0);
      REQUIRE(std::filesystem::exists(expected));
      REQUIRE(stats.rows_processed == 2);
    }
  }
  std::istringstream input("x\n1\n");
  pipeline::RunStats stats;
  REQUIRE(pipeline::RunSqlExport(files.path("override.csv"), files.path("chosen.sqlite"), "data",
                                 input, tests::MakeTestOptions(), tests::MakeNullLogger(),
                                 stats) == 0);
  REQUIRE_FALSE(std::filesystem::exists(files.path("override.db")));
}

TEST_CASE("SQL export preserves existing destinations and diagnoses invalid paths", "[sql-edges]") {
  SqlWorkspace files;
  for (const bool directory : {false, true}) {
    DYNAMIC_SECTION("directory=" << directory) {
      const auto dest = files.path(directory ? "directory" : "existing.db");
      if (directory)
        std::filesystem::create_directory(dest);
      else {
        std::ofstream file(dest, std::ios::binary);
        const std::string bytes("preserve exact bytes\0tail", 24);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      }
      std::istringstream input("x\n1\n");
      pipeline::RunStats stats;
      std::string error;
      REQUIRE(pipeline::RunSqlExport("-", dest, "data", input, tests::MakeTestOptions(),
                                     {[&](const std::string& s) { error = s; }, nullptr},
                                     stats) == 1);
      REQUIRE(error.find("destination already exists") != std::string::npos);
      REQUIRE(stats.rows_processed == 0);
      if (directory)
        REQUIRE(std::filesystem::is_directory(dest));
      else {
        std::ifstream file(dest, std::ios::binary);
        std::ostringstream bytes;
        bytes << file.rdbuf();
        REQUIRE(bytes.str() == std::string("preserve exact bytes\0tail", 24));
      }
    }
  }
  std::istringstream input("x\n1\n");
  pipeline::RunStats stats;
  std::string error;
  REQUIRE(pipeline::RunSqlExport("-", files.path("missing/dest.db"), "data", input,
                                 tests::MakeTestOptions(),
                                 {[&](const std::string& s) { error = s; }, nullptr}, stats) == 1);
  REQUIRE(error.find("sql: SQLite error:") != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(files.path("missing/dest.db")));
}

TEST_CASE("SQL export quotes table names and cleans up failed loads", "[sql-edges]") {
  SqlWorkspace files;
  SECTION("quoted table") {
    std::istringstream input("x\n1\n");
    pipeline::RunStats stats;
    REQUIRE(pipeline::RunSqlExport("-", files.path("quoted.db"), "a\"b", input,
                                   tests::MakeTestOptions(), tests::MakeNullLogger(), stats) == 0);
    REQUIRE(stats.rows_processed == 1);
    SQLite::Database db(files.path("quoted.db"), SQLite::OPEN_READONLY);
    REQUIRE(db.execAndGet("SELECT x FROM \"a\"\"b\"").getInt() == 1);
  }
  SECTION("reserved SQLite table fails inside loader") {
    std::istringstream input("x\n1\n");
    pipeline::RunStats stats;
    std::string error;
    REQUIRE(pipeline::RunSqlExport(
                "-", files.path("failed.db"), "sqlite_reserved", input, tests::MakeTestOptions(),
                {[&](const std::string& s) { error = s; }, nullptr}, stats) == 1);
    REQUIRE(error.find("SQLite error while loading CSV:") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(files.path("failed.db")));
  }
  SECTION("header only") {
    std::istringstream input("x,y\n");
    pipeline::RunStats stats;
    REQUIRE(pipeline::RunSqlExport("-", files.path("empty.db"), "data", input,
                                   tests::MakeTestOptions(), tests::MakeNullLogger(), stats) == 0);
    REQUIRE(stats.rows_processed == 0);
  }
}

TEST_CASE("SQL query CSV validation returns diagnostics with optional logger", "[sql-edges]") {
  struct Case {
    std::string sql, table, format, diagnostic;
  };
  for (const auto& c : std::vector<Case>{
           {" \t\n", "data", "csv", "--sql cannot be empty"},
           {"SELECT 1", " \t", "csv", "--table cannot be empty"},
           {"SELECT FROM", "data", "csv", "SQLite error:"},
           {"-- only a comment", "data", "csv", "did not return a result set"},
           {";", "data", "csv", "did not return a result set"},
           {"UPDATE data SET x=2", "data", "csv", "did not return a result set"},
           {"SELECT x FROM data", "data", "json", "--format must be csv or markdown"}}) {
    for (const bool capture : {false, true}) {
      DYNAMIC_SECTION(c.sql << '/' << c.table << '/' << c.format << '/' << capture) {
        std::istringstream input("x\n1\n");
        std::ostringstream output;
        pipeline::RunStats stats;
        std::string error;
        auto logger = tests::MakeNullLogger();
        if (capture) logger.error = [&](const std::string& s) { error = s; };
        REQUIRE(pipeline::RunSqlQueryCsv(c.sql, c.table, c.format, input, output,
                                         tests::MakeTestOptions(), logger, stats) == 1);
        if (capture) REQUIRE(error.find(c.diagnostic) != std::string::npos);
        REQUIRE(output.str().empty());
        REQUIRE(stats.rows_processed == 0);
      }
    }
  }
}

TEST_CASE("SQL query writers preserve quoted values NULL and empty results", "[sql-edges]") {
  for (const auto& format : {"csv", "markdown"}) {
    for (const bool empty : {false, true}) {
      DYNAMIC_SECTION(format << '/' << empty) {
        std::istringstream input("x\n1\n");
        std::ostringstream output;
        pipeline::RunStats stats;
        const std::string sql =
            "SELECT 'comma,quote\"' AS text, NULL AS missing, 'a|b' AS pipe FROM \"a\"\"b\"" +
            std::string(empty ? " WHERE 0" : "");
        REQUIRE(pipeline::RunSqlQueryCsv(sql, "  a\"b  ", format, input, output,
                                         tests::MakeTestOptions(), tests::MakeNullLogger(),
                                         stats) == 0);
        REQUIRE(stats.rows_processed == (empty ? 0 : 1));
        if (std::string(format) == "csv") {
          const auto rows = tests::ParseCsv(output.str());
          REQUIRE(rows[0] == std::vector<std::string>{"text", "missing", "pipe"});
          REQUIRE(rows.size() == (empty ? 1 : 2));
          if (!empty) REQUIRE(rows[1] == std::vector<std::string>{"comma,quote\"", "", "a|b"});
        } else {
          REQUIRE(output.str().find("missing") != std::string::npos);
          if (!empty) REQUIRE(output.str().find("a\\|b") != std::string::npos);
        }
      }
    }
  }
}

TEST_CASE("SQL query database failures never create or mutate input", "[sql-edges]") {
  SqlWorkspace files;
  {
    SQLite::Database db(files.path("data.db"), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("CREATE TABLE data(x); INSERT INTO data VALUES(1)");
  }
  {
    std::ofstream invalid(files.path("invalid.db"));
    invalid << "not SQLite";
  }
  struct Case {
    std::string sql, path, diagnostic;
  };
  for (const auto& c : std::vector<Case>{
           {" ", files.path("data.db"), "--sql cannot be empty"},
           {"SELECT 1", "", "database path cannot be empty"},
           {"SELECT * FROM data", files.path("missing.db"), "SQLite error:"},
           {"SELECT * FROM data", files.path("invalid.db"), "SQLite error:"},
           {"SELECT FROM", files.path("data.db"), "SQLite error:"},
           {"DELETE FROM data RETURNING x", files.path("data.db"), "SQLite error:"},
           {"UPDATE data SET x=2", files.path("data.db"), "did not return a result set"}}) {
    for (const bool capture : {false, true}) {
      DYNAMIC_SECTION(c.sql << '/' << std::filesystem::path(c.path).filename().string() << '/'
                            << capture) {
        std::ostringstream output;
        pipeline::RunStats stats;
        std::string error;
        auto logger = tests::MakeNullLogger();
        if (capture) logger.error = [&](const std::string& s) { error = s; };
        REQUIRE(pipeline::RunSqlQueryDb(c.sql, c.path, "csv", output, logger, stats) == 1);
        if (capture) REQUIRE(error.find(c.diagnostic) != std::string::npos);
        REQUIRE(stats.rows_processed == 0);
      }
    }
  }
  REQUIRE_FALSE(std::filesystem::exists(files.path("missing.db")));
  SQLite::Database db(files.path("data.db"), SQLite::OPEN_READONLY);
  REQUIRE(db.execAndGet("SELECT x FROM data").getInt() == 1);
}

TEST_CASE("SQL input detection and warning token boundaries", "[sql-edges]") {
  for (const auto& path : {"x.GZ", "x.ZIP", "x.CSV", "x.txt"})
    REQUIRE(pipeline::DetectSqlQueryInputKind(path) == pipeline::SqlQueryInputKind::kCsv);
  for (const auto& path : {"", "-", "x.json", "x"})
    REQUIRE(pipeline::DetectSqlQueryInputKind(path) == pipeline::SqlQueryInputKind::kUnknown);
  for (const auto& sql :
       {"SELECT 1", "SELECT /", "SELECT 'it''s 3/2'", "SELECT \"a\"\"3/2\"", "SELECT 2e3/2",
        "SELECT 2/3E-2", "SELECT 2/3.0", "SELECT 1 /* 3/2 */", "SELECT 1 -- 3/2", "SELECT (3)/(2)"})
    REQUIRE_FALSE(pipeline::ShouldWarnIntegerDivision(sql));
  for (const auto& sql :
       {"SELECT _x/2", "SELECT 3/2 -- comment", "SELECT 1 /* x */ + a/b", "-- comment\nSELECT a/2"})
    REQUIRE(pipeline::ShouldWarnIntegerDivision(sql));
}

#ifdef CSVZALL_HAVE_COMPRESSED_INPUT
TEST_CASE("SQL gzip inputs support export inference and CSV query", "[sql-edges]") {
  SqlWorkspace files;
  const auto path = files.path("input.csv.gz");
  const std::string csv = "name,value\nalice,10\nbob,20\n";
  gzFile gzip = gzopen(path.c_str(), "wb");
  REQUIRE(gzip != nullptr);
  REQUIRE(gzwrite(gzip, csv.data(), static_cast<unsigned int>(csv.size())) ==
          static_cast<int>(csv.size()));
  REQUIRE(gzclose(gzip) == Z_OK);
  auto options = tests::MakeTestOptions();
  options.input_is_stdin = false;
  options.input_path = path;
  std::istringstream ignored;
  pipeline::RunStats exported;
  REQUIRE(pipeline::RunSqlExport(path, "", "data", ignored, options, tests::MakeNullLogger(),
                                 exported) == 0);
  REQUIRE(exported.rows_processed == 2);
  REQUIRE(std::filesystem::exists(path + ".db"));
  std::ostringstream output;
  pipeline::RunStats queried;
  REQUIRE(pipeline::RunSqlQueryCsv("SELECT name FROM data WHERE value > 10", "data", "csv", ignored,
                                   output, options, tests::MakeNullLogger(), queried) == 0);
  REQUIRE(tests::ParseCsv(output.str()) ==
          std::vector<std::vector<std::string>>{{"name"}, {"bob"}});
  REQUIRE(queried.rows_processed == 1);
}
#endif

TEST_CASE("SQL query reports output failures and preserves stream exception masks", "[sql-edges]") {
  SqlWorkspace files;
  {
    SQLite::Database db(files.path("data.db"), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("CREATE TABLE data(x); INSERT INTO data VALUES(1)");
  }
  struct RejectWrites : std::streambuf {
    bool sync_only;
    explicit RejectWrites(bool sync_failure) : sync_only(sync_failure) {}
    std::streamsize xsputn(const char*, std::streamsize count) override {
      return sync_only ? count : 0;
    }
    int_type overflow(int_type c) override { return sync_only ? c : traits_type::eof(); }
    int sync() override { return sync_only ? -1 : 0; }
  };
  for (const bool sync_only : {false, true}) {
    for (const bool database : {false, true}) {
      for (const bool exceptions : {false, true}) {
        for (const auto& format : {"csv", "markdown"}) {
          DYNAMIC_SECTION(sync_only << '/' << database << '/' << exceptions << '/' << format) {
            RejectWrites buffer(sync_only);
            std::ostream output(&buffer);
            const auto mask = exceptions ? std::ios::badbit | std::ios::failbit : std::ios::goodbit;
            output.exceptions(mask);
            std::istringstream input("x\n1\n");
            pipeline::RunStats stats;
            std::string error;
            pipeline::LoggerCallbacks logger{[&](const std::string& s) { error = s; }, nullptr};
            const int rc =
                database
                    ? pipeline::RunSqlQueryDb("SELECT x FROM data", files.path("data.db"), format,
                                              output, logger, stats)
                    : pipeline::RunSqlQueryCsv("SELECT x FROM data", "data", format, input, output,
                                               tests::MakeTestOptions(), logger, stats);
            REQUIRE(rc == 1);
            REQUIRE(error == "sql query: failed to write output.");
            REQUIRE(output.exceptions() == mask);
          }
        }
      }
    }
  }
}

TEST_CASE("SQL query CSV catches invalid explicit SQLite working path", "[sql-edges]") {
  SqlWorkspace files;
  std::istringstream input("x\n1\n");
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  options.sqlite_db_path = files.path("missing/work.db");
  pipeline::RunStats stats;
  std::string error;
  REQUIRE(pipeline::RunSqlQueryCsv("SELECT x FROM data", "data", "csv", input, output, options,
                                   {[&](const std::string& s) { error = s; }, nullptr},
                                   stats) == 1);
  REQUIRE(error.find("sql query: SQLite error:") != std::string::npos);
}

TEST_CASE("SQL query DB supports Markdown empty results and rejects invalid formats",
          "[sql-edges]") {
  SqlWorkspace files;
  {
    SQLite::Database db(files.path("data.db"), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("CREATE TABLE data(x); INSERT INTO data VALUES(1)");
  }
  for (const auto& format : {"markdown", "invalid"}) {
    DYNAMIC_SECTION(format) {
      std::ostringstream output;
      pipeline::RunStats stats;
      std::string error;
      const int rc = pipeline::RunSqlQueryDb(
          "SELECT x FROM data WHERE 0", files.path("data.db"), format, output,
          {[&](const std::string& s) { error = s; }, nullptr}, stats);
      if (std::string(format) == "markdown") {
        REQUIRE(rc == 0);
        REQUIRE(output.str() == "| x |\n| --- |\n");
      } else {
        REQUIRE(rc == 1);
        REQUIRE(error == "sql query: --format must be csv or markdown.");
      }
      REQUIRE(stats.rows_processed == 0);
    }
  }
}

TEST_CASE("SQL export infers numeric affinities and preserves lexical text", "[sql-edges]") {
  SqlWorkspace files;
  std::istringstream input(tests::MakeTestCsv(
      {"integer", "real", "mixed", "boolean", "date", "big"},
      {{"1", "1.5", "001", "true", "2026-01-01", "123456789012345678901234567890"},
       {"2", "2.5", "text", "false", "2026-01-02", "123456789012345678901234567891"}}));
  pipeline::RunStats stats;
  std::string verbose;
  REQUIRE(
      pipeline::RunSqlExport("-", files.path("types.db"), "data", input, tests::MakeTestOptions(),
                             {nullptr, [&](const std::string& s) { verbose = s; }}, stats) == 0);
  REQUIRE(verbose.find("sql: wrote 2 rows to ") == 0);
  SQLite::Database db(files.path("types.db"), SQLite::OPEN_READONLY);
  SQLite::Statement columns(db, "PRAGMA table_info(data)");
  for (const auto& type : {"NUMERIC", "NUMERIC", "TEXT", "TEXT", "TEXT", "TEXT"}) {
    REQUIRE(columns.executeStep());
    REQUIRE(columns.getColumn(2).getString() == type);
  }
  REQUIRE_FALSE(columns.executeStep());
  SQLite::Statement row(db, "SELECT mixed, boolean, date, big FROM data ORDER BY integer LIMIT 1");
  REQUIRE(row.executeStep());
  REQUIRE(row.getColumn(0).getString() == "001");
  REQUIRE(row.getColumn(1).getString() == "true");
  REQUIRE(row.getColumn(2).getString() == "2026-01-01");
  REQUIRE(row.getColumn(3).getString() == "123456789012345678901234567890");
}

TEST_CASE("SQL export cannot infer stdin destinations with optional logger", "[sql-edges]") {
  for (const auto& path : {"", "-"}) {
    for (const bool capture : {false, true}) {
      DYNAMIC_SECTION(path << '/' << capture) {
        std::istringstream input("x\n1\n");
        pipeline::RunStats stats;
        std::string error;
        auto logger = tests::MakeNullLogger();
        if (capture) logger.error = [&](const std::string& s) { error = s; };
        REQUIRE(pipeline::RunSqlExport(path, "", "data", input, tests::MakeTestOptions(), logger,
                                       stats) == 1);
        if (capture) REQUIRE(error.find("Provide an explicit --dest path") != std::string::npos);
      }
    }
  }
}

TEST_CASE("SQL invalid inputs report header loader and filesystem failures", "[sql-edges]") {
  SqlWorkspace files;
  SECTION("CSV loader failure") {
    std::istringstream input("x\n1\n");
    std::ostringstream output;
    pipeline::RunStats stats;
    std::string error;
    REQUIRE(pipeline::RunSqlQueryCsv(
                "SELECT 1", "sqlite_reserved", "csv", input, output, tests::MakeTestOptions(),
                {[&](const std::string& s) { error = s; }, nullptr}, stats) == 1);
    REQUIRE(error.find("SQLite error while loading CSV:") != std::string::npos);
  }
  SECTION("no header") {
    for (const bool query : {false, true}) {
      DYNAMIC_SECTION(query) {
        std::istringstream input("");
        std::ostringstream output;
        pipeline::RunStats stats;
        std::string error;
        pipeline::LoggerCallbacks logger{[&](const std::string& s) { error = s; }, nullptr};
        const int rc = query ? pipeline::RunSqlQueryCsv("SELECT 1", "data", "csv", input, output,
                                                        tests::MakeTestOptions(), logger, stats)
                             : pipeline::RunSqlExport("-", files.path("empty.db"), "data", input,
                                                      tests::MakeTestOptions(), logger, stats);
        REQUIRE(rc == 1);
        REQUIRE(error.find("no header row") != std::string::npos);
        REQUIRE_FALSE(std::filesystem::exists(files.path("empty.db")));
      }
    }
  }
  SECTION("destination inspection error") {
    // POSIX reports an inspection error; Windows can classify this as absent
    // and defer the failure to SQLite. Both paths must return diagnostics.
    const auto path = files.path(std::string(300, 'x'));
    std::istringstream input("x\n1\n");
    pipeline::RunStats stats;
    std::string error;
    REQUIRE(pipeline::RunSqlExport("-", path, "data", input, tests::MakeTestOptions(),
                                   {[&](const std::string& s) { error = s; }, nullptr},
                                   stats) == 1);
#ifdef _WIN32
    REQUIRE((error.find("sql: cannot inspect destination:") == 0 ||
             error.find("sql: SQLite error:") == 0));
#else
    REQUIRE(error.find("sql: cannot inspect destination:") == 0);
#endif
  }
}

TEST_CASE("SQL writers escape headers and embedded newlines exactly", "[sql-edges]") {
  SECTION("CSV") {
    std::istringstream input("x\n1\n");
    std::ostringstream output;
    pipeline::RunStats stats;
    REQUIRE(pipeline::RunSqlQueryCsv(
                "SELECT 'line\nnext,\"quoted\"' AS \"a,\n\"\"b\" FROM data", "data", "csv",
                input, output, tests::MakeTestOptions(), tests::MakeNullLogger(), stats) == 0);
    REQUIRE(tests::ParseCsv(output.str()) ==
            std::vector<std::vector<std::string>>{{"a,\n\"b"}, {"line\nnext,\"quoted\""}});
    REQUIRE(stats.rows_processed == 1);
  }
  SECTION("Markdown DB") {
    SqlWorkspace files;
    {
      SQLite::Database db(files.path("data.db"), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    }
    std::ostringstream output;
    pipeline::RunStats stats;
    REQUIRE(pipeline::RunSqlQueryDb("SELECT 'x|y' AS \"a|b\", NULL AS n", files.path("data.db"),
                                    "markdown", output, tests::MakeNullLogger(), stats) == 0);
    REQUIRE(output.str() == "| a\\|b | n |\n| ---- | --- |\n| x\\|y |   |\n");
    REQUIRE(stats.rows_processed == 1);
  }
}

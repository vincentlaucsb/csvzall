#include <catch2/catch_test_macros.hpp>

#include <SQLiteCpp/SQLiteCpp.h>
#include <csv.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/sqlite/csv_loader.hpp"
#include "../src/sqlite/sqlite_db.hpp"
#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

namespace {

using Rows = std::vector<std::vector<std::string>>;

struct TableWorkspace {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("csvzall_sql_tables_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TableWorkspace() { std::filesystem::create_directories(root); }
  ~TableWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
  std::string path(const std::string& name) const { return (root / name).string(); }
  std::string write(const std::string& name, const std::string& text) const {
    std::ofstream(root / name, std::ios::binary) << text;
    return path(name);
  }
};

struct Captured {
  int rc = 0;
  std::string output;
  std::string error;
};

Captured QueryTables(const std::string& sql,
                     const std::vector<pipeline::SqlCsvTable>& tables,
                     const std::string& stdin_text = "") {
  std::istringstream stdin_input(stdin_text);
  std::ostringstream output;
  Captured result;
  pipeline::LoggerCallbacks logger{[&](const std::string& msg) { result.error = msg; }, nullptr};
  pipeline::RunStats stats;
  pipeline::RunOptions options;
  result.rc = pipeline::RunSqlQueryCsvTables(sql, tables, "csv", stdin_input, output, options,
                                             logger, stats);
  result.output = output.str();
  return result;
}

Captured Load(const std::string& csv,
              const std::string& dest,
              const std::string& table,
              pipeline::SqlExportTableMode mode) {
  std::istringstream input(csv);
  Captured result;
  pipeline::LoggerCallbacks logger{[&](const std::string& msg) { result.error = msg; }, nullptr};
  pipeline::RunStats stats;
  result.rc = pipeline::RunSqlExport("-", dest, table, mode, input, tests::MakeTestOptions(),
                                     logger, stats);
  return result;
}

Rows QueryDb(const std::string& db_path, const std::string& sql) {
  std::ostringstream output;
  pipeline::RunStats stats;
  REQUIRE(pipeline::RunSqlQueryDb(sql, db_path, "csv", output, tests::MakeNullLogger(), stats) ==
          0);
  return tests::ParseCsv(output.str());
}

}  // namespace

TEST_CASE("sql query joins several CSVs loaded as named tables", "[sql-tables]") {
  TableWorkspace files;
  const auto rides = files.write("rides.csv", "date,km\n2026-05-01,10\n2026-06-01,20\n2026-06-10,5\n");
  const auto log = files.write("log.csv", "Item,Date\nchain,5/26/2026\ntires,4/??/2026\n");

  const auto result = QueryTables(
      "SELECT log.Item, SUM(rides.km) AS km FROM log JOIN rides "
      "ON rides.date >= parse_date(log.Date) GROUP BY log.Item ORDER BY log.Item",
      {{"rides", rides}, {"log", log}});
  INFO(result.error);
  REQUIRE(result.rc == 0);
  CHECK(tests::ParseCsv(result.output) == Rows{{"Item", "km"}, {"chain", "25"}, {"tires", "35"}});
}

TEST_CASE("sql query multi-table loads stdin and infers affinity per table", "[sql-tables]") {
  TableWorkspace files;
  const auto ids = files.write("ids.csv", "id\n12345678901234567890\n");
  const auto result = QueryTables(
      "SELECT typeof(n.v), typeof(ids.id), ids.id FROM n, ids",
      {{"n", "-"}, {"ids", ids}}, "v\n7\n");
  INFO(result.error);
  REQUIRE(result.rc == 0);
  CHECK(tests::ParseCsv(result.output)[1] ==
        std::vector<std::string>{"integer", "text", "12345678901234567890"});
}

TEST_CASE("sql query multi-table validates table names and stdin use", "[sql-tables]") {
  TableWorkspace files;
  const auto a = files.write("a.csv", "x\n1\n");

  auto duplicate = QueryTables("SELECT 1", {{"t", a}, {"T", a}});
  CHECK(duplicate.rc == 1);
  CHECK(duplicate.error.find("duplicate --csv table name: T") != std::string::npos);

  auto two_stdin = QueryTables("SELECT 1", {{"a", "-"}, {"b", "-"}}, "x\n1\n");
  CHECK(two_stdin.rc == 1);
  CHECK(two_stdin.error.find("only one --csv table can read stdin") != std::string::npos);

  auto empty_name = QueryTables("SELECT 1", {{" ", a}});
  CHECK(empty_name.rc == 1);
  CHECK(empty_name.error.find("table name cannot be empty") != std::string::npos);

  auto missing = QueryTables("SELECT 1", {{"a", a}, {"b", files.path("missing.csv")}});
  CHECK(missing.rc == 1);
  CHECK(missing.error.find("table b: ") != std::string::npos);

  auto bad_sql = QueryTables("SELECT * FROM nope", {{"a", a}});
  CHECK(bad_sql.rc == 1);
  CHECK(bad_sql.error.find("no such table: nope") != std::string::npos);
}

TEST_CASE("OpenSqliteDb sizes multi-table inputs together", "[sql-tables]") {
  TableWorkspace files;
  const std::string half_mb(512 * 1024, 'x');
  sqlite::SqliteDbOpenOptions options;
  options.threshold_mb = 1;
  options.input_path = files.write("a.csv", half_mb);
  options.additional_input_paths = {files.write("b.csv", half_mb), files.write("c.csv", half_mb)};

  auto db = sqlite::OpenSqliteDb(options);
  SQLite::Statement query(db.db(), "SELECT file FROM pragma_database_list WHERE name = 'main'");
  REQUIRE(query.executeStep());
  CHECK_FALSE(query.getColumn(0).getString().empty());

  options.additional_input_paths = {"-"};
  auto stdin_db = sqlite::OpenSqliteDb(options);
  SQLite::Statement stdin_query(stdin_db.db(),
                                "SELECT file FROM pragma_database_list WHERE name = 'main'");
  REQUIRE(stdin_query.executeStep());
  CHECK(stdin_query.getColumn(0).getString().empty());
}

TEST_CASE("sql load --add-table adds a table to an existing database", "[sql-tables]") {
  TableWorkspace files;
  const auto db = files.path("work.db");
  using Mode = pipeline::SqlExportTableMode;

  REQUIRE(Load("km\n10\n20\n", db, "rides", Mode::CreateDatabase).rc == 0);
  const auto second = Load("km\n1\n", db, "log", Mode::CreateDatabase);
  CHECK(second.rc == 1);
  CHECK(second.error.find("--add-table") != std::string::npos);

  REQUIRE(Load("item\nchain\n", db, "log", Mode::AddTable).rc == 0);
  CHECK(QueryDb(db, "SELECT (SELECT SUM(km) FROM rides), (SELECT item FROM log)")[1] ==
        std::vector<std::string>{"30", "chain"});

  // The table name is taken, in any letter case: the database is left alone.
  const auto conflict = Load("item\nnew\n", db, "LOG", Mode::AddTable);
  CHECK(conflict.rc == 1);
  CHECK(conflict.error.find("table already exists") != std::string::npos);
  CHECK(conflict.error.find("--replace-table") != std::string::npos);
  CHECK(QueryDb(db, "SELECT item FROM log") == Rows{{"item"}, {"chain"}});

  // --add-table on a missing destination simply creates it.
  REQUIRE(Load("x\n1\n", files.path("fresh.db"), "data", Mode::AddTable).rc == 0);
}

TEST_CASE("sql load --replace-table swaps a table and keeps dependent views", "[sql-tables]") {
  TableWorkspace files;
  const auto db = files.path("work.db");
  using Mode = pipeline::SqlExportTableMode;

  REQUIRE(Load("km\n10\n", db, "rides", Mode::CreateDatabase).rc == 0);
  {
    SQLite::Database raw(db, SQLite::OPEN_READWRITE);
    raw.exec("CREATE VIEW total AS SELECT SUM(km) AS km FROM rides");
    raw.exec("PRAGMA journal_mode = WAL");
  }

  REQUIRE(Load("km,note\n1,a\n2,b\n", db, "rides", Mode::ReplaceTable).rc == 0);
  CHECK(QueryDb(db, "SELECT km FROM total") == Rows{{"km"}, {"3"}});
  CHECK(QueryDb(db, "SELECT note FROM rides ORDER BY km") == Rows{{"note"}, {"a"}, {"b"}});
  CHECK(QueryDb(db, "SELECT count(*) FROM sqlite_schema WHERE name LIKE '%csvzall_load%'") ==
        Rows{{"count(*)"}, {"0"}});
  // Loading into a user database must not switch it out of WAL mode.
  CHECK(QueryDb(db, "PRAGMA journal_mode") == Rows{{"journal_mode"}, {"wal"}});

  // Views are not tables and are never replaced.
  const auto view = Load("km\n1\n", db, "total", Mode::ReplaceTable);
  CHECK(view.rc == 1);
  CHECK(view.error.find("is a view, not a table") != std::string::npos);
}

TEST_CASE("sql load into an existing database never deletes it on failure", "[sql-tables]") {
  TableWorkspace files;
  const auto db = files.path("work.db");
  using Mode = pipeline::SqlExportTableMode;
  REQUIRE(Load("km\n10\n", db, "rides", Mode::CreateDatabase).rc == 0);

  // Duplicate column names make CREATE TABLE fail after the database is open.
  const auto add = Load("a,a\n1,2\n", db, "bad", Mode::AddTable);
  CHECK(add.rc == 1);
  const auto replace = Load("a,a\n1,2\n", db, "rides", Mode::ReplaceTable);
  CHECK(replace.rc == 1);

  REQUIRE(std::filesystem::exists(db));
  CHECK(QueryDb(db, "SELECT km FROM rides") == Rows{{"km"}, {"10"}});
  CHECK(QueryDb(db, "SELECT name FROM sqlite_schema ORDER BY name") == Rows{{"name"}, {"rides"}});

  // A file that is not a SQLite database is reported and left in place.
  const auto text = files.write("notes.db", "not a database\n");
  CHECK(Load("x\n1\n", text, "data", Mode::AddTable).rc == 1);
  REQUIRE(std::filesystem::exists(text));
}

TEST_CASE("sql load leaves no table behind when the load fails after CREATE TABLE",
          "[sql-tables]") {
  TableWorkspace files;
  const auto db = files.path("work.db");
  using Mode = pipeline::SqlExportTableMode;
  REQUIRE(Load("km\n10\n", db, "rides", Mode::CreateDatabase).rc == 0);

  {
    // A reader holding a SHARED lock lets CREATE TABLE and the inserts run but
    // makes COMMIT fail with "database is locked".
    SQLite::Database reader(db, SQLite::OPEN_READONLY);
    reader.exec("BEGIN");
    SQLite::Statement hold(reader, "SELECT km FROM rides");
    REQUIRE(hold.executeStep());

    const auto add = Load("item\nchain\n", db, "log", Mode::AddTable);
    CHECK(add.rc == 1);
    CHECK(add.error.find("locked") != std::string::npos);
    const auto replace = Load("km\n1\n", db, "rides", Mode::ReplaceTable);
    CHECK(replace.rc == 1);
  }

  CHECK(QueryDb(db, "SELECT name FROM sqlite_schema ORDER BY name") == Rows{{"name"}, {"rides"}});
  CHECK(QueryDb(db, "SELECT km FROM rides") == Rows{{"km"}, {"10"}});
  // The failed add left nothing to clean up, so a retry succeeds.
  REQUIRE(Load("item\nchain\n", db, "log", Mode::AddTable).rc == 0);
}

TEST_CASE("sql load table names do not conflict with same-named triggers", "[sql-tables]") {
  TableWorkspace files;
  const auto db = files.path("work.db");
  using Mode = pipeline::SqlExportTableMode;
  REQUIRE(Load("km\n10\n", db, "rides", Mode::CreateDatabase).rc == 0);
  {
    SQLite::Database raw(db, SQLite::OPEN_READWRITE);
    raw.exec("CREATE TRIGGER log AFTER INSERT ON rides BEGIN SELECT 1; END");
  }
  const auto add = Load("item\nchain\n", db, "log", Mode::AddTable);
  INFO(add.error);
  REQUIRE(add.rc == 0);
  CHECK(QueryDb(db, "SELECT item FROM log") == Rows{{"item"}, {"chain"}});
  REQUIRE(Load("item\nbrakes\n", db, "log", Mode::ReplaceTable).rc == 0);
  CHECK(QueryDb(db, "SELECT item FROM log") == Rows{{"item"}, {"brakes"}});
}

TEST_CASE("LoadCsvIntoTable rolls back CREATE TABLE when inserts fail", "[sql-tables]") {
  TableWorkspace files;
  SQLite::Database db(files.path("full.db"), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  db.exec("CREATE TABLE keep (x)");
  // Leave room for the new table's root page but not for its rows, so the
  // load fails with SQLITE_FULL after CREATE TABLE has run.
  const auto pages = db.execAndGet("PRAGMA page_count").getInt();
  db.exec("PRAGMA max_page_count = " + std::to_string(pages + 2));

  std::string csv = "value\n";
  for (int i = 0; i < 2000; ++i) {
    csv += std::string(200, 'x') + std::to_string(i) + "\n";
  }
  std::istringstream input(csv);
  csv::CSVReader reader(input);
  const auto headers = reader.get_col_names();
  std::string error;
  sqlite::SqliteLogCallbacks logger{[&](const std::string& msg) { error = msg; }, nullptr};
  CHECK_FALSE(sqlite::LoadCsvIntoTable(reader, headers, db, "big",
                                       {sqlite::SqliteColumnAffinity::Text},
                                       sqlite::CsvLoadOptions{true}, logger));
  CHECK(error.find("full") != std::string::npos);
  CHECK(db.execAndGet("SELECT count(*) FROM sqlite_schema WHERE name = 'big'").getInt() == 0);
}

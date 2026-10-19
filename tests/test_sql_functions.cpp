#include <catch2/catch_test_macros.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/pipeline/common/flexible_date.hpp"
#include "../src/sqlite/sql_functions.hpp"
#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

namespace {

using Rows = std::vector<std::vector<std::string>>;

struct QueryResult {
  int rc = 0;
  Rows rows;
  std::string error;
};

QueryResult RunQuery(const std::string& csv, const std::string& sql) {
  std::istringstream input(csv);
  std::ostringstream output;
  QueryResult result;
  pipeline::LoggerCallbacks logger{[&](const std::string& msg) { result.error = msg; }, nullptr};
  pipeline::RunStats stats;
  result.rc = pipeline::RunSqlQueryCsv(sql, "data", "csv", input, output,
                                       tests::MakeTestOptions(), logger, stats);
  if (result.rc == 0) {
    result.rows = tests::ParseCsv(output.str());
  }
  return result;
}

// A one-row CSV for queries that only exercise SQL expressions.
const std::string kOneRow = "x\n1\n";

std::string Scalar(const std::string& expression) {
  const auto result = RunQuery(kOneRow, "SELECT " + expression + " AS v FROM data");
  INFO(result.error);
  REQUIRE(result.rc == 0);
  REQUIRE(result.rows.size() == 2);
  return result.rows[1][0];
}

std::string ScalarError(const std::string& expression) {
  const auto result = RunQuery(kOneRow, "SELECT " + expression + " AS v FROM data");
  REQUIRE(result.rc != 0);
  return result.error;
}

}  // namespace

TEST_CASE("SQL regex uses PCRE2 with UTF-8 and Unicode properties", "[sql-functions]") {
  // Values and patterns both come from a UTF-8 CSV, as in issue #35.
  const auto csv = tests::MakeTestCsv(
      {"value", "pattern"},
      {{"Caf\xC3\xA9", "^Caf.$"},
       {"\xC3\xA9" "cole", "(?i)^\xC3\x89" "COLE$"},
       {"na\xC3\xAF" "ve", R"(^\w+$)"},
       {"School", "(?i)^school$"}});
  const auto result = RunQuery(csv, "SELECT regexp_like(value, pattern) FROM data");
  INFO(result.error);
  REQUIRE(result.rc == 0);
  REQUIRE(result.rows == Rows{{"regexp_like(value, pattern)"}, {"1"}, {"1"}, {"1"}, {"1"}});
}

TEST_CASE("SQL regex operator, inline flags, and long inputs", "[sql-functions]") {
  CHECK(Scalar("'Gym Day' REGEXP '(?i)gym'") == "1");
  CHECK(Scalar("regexp('^a', 'abc')") == "1");
  CHECK(Scalar("regexp_like('ab', 'a(?i)B')") == "1");
  // Inline flags apply from where they appear, not to the whole pattern.
  CHECK(Scalar("regexp_like('Ab', 'a(?i)b')") == "0");
  // std::regex ran out of stack near 80k characters; PCRE2 is heap-based.
  CHECK(Scalar("regexp_like(printf('%.100000c', 'a') || 'b', '^(a|b)*$')") == "1");
  CHECK(Scalar("length(printf('%.100000c', 'a'))") == "100000");
}

TEST_CASE("SQL regex NULL handling and errors", "[sql-functions]") {
  CHECK(Scalar("regexp_like(NULL, 'a')") == "0");
  CHECK(Scalar("NULL REGEXP 'a'") == "0");
  CHECK(Scalar("regexp_substr(NULL, 'a') IS NULL") == "1");
  // A NULL pattern is a query error rather than a silent non-match.
  CHECK(ScalarError("regexp_like('a', NULL)").find("missing regexp pattern") != std::string::npos);
  CHECK(ScalarError("regexp_like('a', '(')").find("missing closing parenthesis") !=
        std::string::npos);
}

TEST_CASE("SQL regex extraction and replacement", "[sql-functions]") {
  CHECK(Scalar("regexp_substr('abcdef', 'b.d')") == "bcd");
  CHECK(Scalar("regexp_capture('ride 42 km', '(\\d+) km', 1)") == "42");
  CHECK(Scalar(R"(regexp_replace('a1b2', '\d', '#'))") == "a#b#");
  CHECK(Scalar("regexp_replace('2026-06-09', '(\\d+)-(\\d+)-(\\d+)', '$3/$2/$1')") ==
        "09/06/2026");
}

TEST_CASE("SQL regex still matches the valid parts of non-UTF-8 text", "[sql-functions]") {
  // Latin-1 "Caf\xE9 gym": the invalid byte cannot match, the rest still can.
  const std::string csv = "value\nCaf\xE9 gym\n";
  const auto result =
      RunQuery(csv, "SELECT regexp_like(value, 'gym'), regexp_like(value, '^Caf') FROM data");
  INFO(result.error);
  REQUIRE(result.rc == 0);
  REQUIRE(result.rows[1] == std::vector<std::string>{"1", "1"});
}

TEST_CASE("SQL query runs the issue #35 acceptance expression", "[sql-functions]") {
  const auto csv = tests::MakeTestCsv({"x"}, {{"1"}, {"3"}, {"10"}});
  const auto result = RunQuery(
      csv,
      R"(SELECT sqrt(16), median(x), fuzzy_leven('kitten','sitting'), regexp_replace('a1b2','\d','#') FROM data)");
  INFO(result.error);
  REQUIRE(result.rc == 0);
  REQUIRE(result.rows[1] == std::vector<std::string>{"4.0", "3.0", "3", "a#b#"});
}

TEST_CASE("SQL math, stats, fuzzy, text, crypto, and uuid functions", "[sql-functions]") {
  CHECK(Scalar("pow(2, 10)") == "1024.0");
  CHECK(Scalar("floor(ln(exp(2)))") == "2.0");
  CHECK(Scalar("round(pi(), 4)") == "3.1416");
  CHECK(Scalar("fuzzy_soundex('Robert')") == "R163");
  CHECK(Scalar("fuzzy_jarowin('martha', 'marhta') > 0.9") == "1");
  CHECK(Scalar("text_upper('\xC3\xA9" "cole')") == "\xC3\x89" "COLE");
  CHECK(Scalar("text_ltrim('xxa', 'x')") == "a");
  CHECK(Scalar("md5('abc') = unhex('900150983cd24fb0d6963f7d28e17f72')") == "1");
  CHECK(Scalar("encode(sha256(''), 'hex')") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Scalar("length(uuid4())") == "36");
  CHECK(Scalar("sqlean_version()") == std::string(sqlite::SqleanVersion()));

  const auto csv = tests::MakeTestCsv({"x"}, {{"1"}, {"2"}, {"3"}, {"4"}});
  const auto result =
      RunQuery(csv, "SELECT percentile(x, 50), stddev(x) > 1.29 AND stddev(x) < 1.30 FROM data");
  INFO(result.error);
  REQUIRE(result.rc == 0);
  REQUIRE(result.rows[1] == std::vector<std::string>{"2.5", "1"});
}

TEST_CASE("SQL keeps SQLite built-ins authoritative over sqlean aliases", "[sql-functions]") {
  // SQLite built-in semantics, not sqlean's PostgreSQL-style aliases.
  CHECK(Scalar("ltrim('xxa', 'x')") == "a");
  CHECK(Scalar("rtrim('axx', 'x')") == "a");
  CHECK(Scalar("concat(NULL, 'a', 1)") == "a1");
  CHECK(Scalar("concat_ws('-', 'a', NULL, 'b')") == "a-b");
  CHECK(Scalar("octet_length('\xC3\xA9')") == "2");

  const auto shadows = RunQuery(
      kOneRow,
      "SELECT DISTINCT name FROM pragma_function_list WHERE builtin = 0 AND name IN "
      "(SELECT name FROM pragma_function_list WHERE builtin = 1)");
  INFO(shadows.error);
  REQUIRE(shadows.rc == 0);
  CHECK(shadows.rows == Rows{{"name"}});
}

TEST_CASE("SQL excludes the sqlean modules csvzall does not compile in", "[sql-functions]") {
  const auto result = RunQuery(
      kOneRow,
      "SELECT name FROM pragma_function_list WHERE name IN ("
      "'fileio_read', 'fileio_write', 'fileio_mkdir', 'fileio_symlink', 'readfile', "
      "'writefile', 'vsv', 'unicode_upper', 'unaccent', 'define', 'define_free', 'eval', "
      "'ipfamily', 'ipcontains', 'time_now', 'time_parse', 'math_sqrt', 'math_pow')");
  INFO(result.error);
  REQUIRE(result.rc == 0);
  CHECK(result.rows == Rows{{"name"}});
}

TEST_CASE("SQL load_extension() stays unauthorized", "[sql-functions]") {
  CHECK(ScalarError("load_extension('nothing')").find("not authorized") != std::string::npos);
}

TEST_CASE("LoadSqliteExtensions reports the failing path and keeps SQL loading blocked",
          "[sql-functions]") {
  SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  sqlite::RegisterCsvzallSqliteFunctions(db);
  try {
    sqlite::LoadSqliteExtensions(db, {"csvzall-missing-extension"});
    FAIL("expected a load failure");
  } catch (const std::runtime_error& ex) {
    CHECK(std::string(ex.what()).find("csvzall-missing-extension") != std::string::npos);
  }
  CHECK_THROWS_AS(db.exec("SELECT load_extension('csvzall-missing-extension')"),
                  SQLite::Exception);
  CHECK_NOTHROW(sqlite::LoadSqliteExtensions(db, {}));
}

TEST_CASE("sql query --db path loads requested extensions", "[sql-functions]") {
  std::ostringstream output;
  std::string error;
  pipeline::LoggerCallbacks logger{[&](const std::string& msg) { error = msg; }, nullptr};
  pipeline::RunStats stats;
  // A missing extension fails before the query runs, naming the path.
  REQUIRE(pipeline::RunSqlQueryDb("SELECT 1", ":memory:", "csv", output, logger, stats,
                                  {"csvzall-missing-extension"}) == 1);
  CHECK(error.find("csvzall-missing-extension") != std::string::npos);
}

TEST_CASE("parse_date and date_precision read hand-entered dates", "[sql-functions]") {
  const auto csv = tests::MakeTestCsv(
      {"Date"},
      {{"5/26/2026"}, {"4/??/2026"}, {"6/9/2026"}, {"2026-06-09"}, {"6/2026"}, {"2026-06"},
       {"2026"}, {"??/??/2026"}, {"2026-04-??"}, {"26/5/2026"}, {"2026-05-26T10:00:00"},
       {" 6/9/2026 "}, {"junk"}, {"2/30/2026"}, {"13/2026"}});
  const auto result = RunQuery(
      csv,
      "SELECT parse_date(Date), date_precision(Date), parse_date(Date, 'DMY') FROM data");
  INFO(result.error);
  REQUIRE(result.rc == 0);
  REQUIRE(result.rows == Rows{
                             {"parse_date(Date)", "date_precision(Date)", "parse_date(Date, 'DMY')"},
                             {"2026-05-26", "day", "2026-05-26"},
                             {"2026-04-01", "month", "2026-04-01"},
                             {"2026-06-09", "day", "2026-09-06"},
                             {"2026-06-09", "day", "2026-06-09"},
                             {"2026-06-01", "month", "2026-06-01"},
                             {"2026-06-01", "month", "2026-06-01"},
                             {"2026-01-01", "year", "2026-01-01"},
                             {"2026-01-01", "year", "2026-01-01"},
                             {"2026-04-01", "month", "2026-04-01"},
                             {"2026-05-26", "day", "2026-05-26"},
                             {"2026-05-26", "day", "2026-05-26"},
                             {"2026-06-09", "day", "2026-09-06"},
                             {"", "", ""},
                             {"", "", ""},
                             {"", "", ""},
                         });
}

TEST_CASE("parse_date handles NULL, numeric years, and bad order arguments", "[sql-functions]") {
  CHECK(Scalar("parse_date(NULL) IS NULL") == "1");
  CHECK(Scalar("date_precision(NULL) IS NULL") == "1");
  CHECK(Scalar("parse_date(2026)") == "2026-01-01");
  CHECK(Scalar("date(parse_date('4/??/2026'), '+14 days')") == "2026-04-15");
  CHECK(ScalarError("parse_date('1/2/2026', 'ymd')").find("order must be 'mdy' or 'dmy'") !=
        std::string::npos);
  CHECK(ScalarError("date_precision('1/2/2026', NULL)").find("date_precision: order") !=
        std::string::npos);
}

TEST_CASE("Flexible date parser keeps heatmap full-date rules", "[sql-functions]") {
  using pipeline::common::DateOrder;
  using pipeline::common::DateOrderClue;
  using pipeline::common::FindDateOrderClue;
  using pipeline::common::FormatIsoDate;
  using pipeline::common::ParseFlexibleDate;

  CHECK(FormatIsoDate(*ParseFlexibleDate("2026-6-9", DateOrder::DayMonth)) == "2026-06-09");
  CHECK(FormatIsoDate(*ParseFlexibleDate("13/2/2026", DateOrder::MonthDay)) == "2026-02-13");
  CHECK(FormatIsoDate(*ParseFlexibleDate("2/13/2026", DateOrder::DayMonth)) == "2026-02-13");
  CHECK(FormatIsoDate(*ParseFlexibleDate("2-3-2026", DateOrder::DayMonth)) == "2026-03-02");
  CHECK_FALSE(ParseFlexibleDate("6/2026", DateOrder::MonthDay));
  CHECK_FALSE(ParseFlexibleDate("4/??/2026", DateOrder::MonthDay));
  CHECK_FALSE(ParseFlexibleDate("1/2/26", DateOrder::MonthDay));
  CHECK_FALSE(ParseFlexibleDate("1234567890/1/2026", DateOrder::MonthDay));

  CHECK(FindDateOrderClue("2026-01-02") == DateOrderClue::None);
  CHECK(FindDateOrderClue("1/2/2026") == DateOrderClue::None);
  CHECK(FindDateOrderClue("13/2/2026") == DateOrderClue::DayMonth);
  CHECK(FindDateOrderClue("2/13/2026") == DateOrderClue::MonthDay);
  CHECK(FindDateOrderClue("1/2/26") == DateOrderClue::Invalid);
  CHECK(FindDateOrderClue("junk") == DateOrderClue::Invalid);
}

#ifdef CSVZALL_TEST_EXTENSION_PATH
TEST_CASE("LoadSqliteExtensions loads native code through the C API only", "[sql-functions]") {
  SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  sqlite::RegisterCsvzallSqliteFunctions(db);
  sqlite::LoadSqliteExtensions(db, {CSVZALL_TEST_EXTENSION_PATH});
  CHECK(db.execAndGet("SELECT csvzall_test_answer()").getInt() == 42);

  // The C-API door is closed again: SQL cannot load anything itself.
  SQLite::Statement load(db, "SELECT load_extension(?)");
  load.bind(1, CSVZALL_TEST_EXTENSION_PATH);
  CHECK_THROWS_AS(load.executeStep(), SQLite::Exception);
}

TEST_CASE("sql query loads extensions from RunOptions", "[sql-functions]") {
  std::istringstream input(kOneRow);
  std::ostringstream output;
  auto options = tests::MakeTestOptions();
  options.sqlite_load_extensions = {CSVZALL_TEST_EXTENSION_PATH};
  pipeline::RunStats stats;
  REQUIRE(pipeline::RunSqlQueryCsv("SELECT csvzall_test_answer() AS v FROM data", "data", "csv",
                                   input, output, options, tests::MakeNullLogger(), stats) == 0);
  CHECK(tests::ParseCsv(output.str()) == Rows{{"v"}, {"42"}});

  // Charts and other callers that leave the option empty never see it.
  std::istringstream plain_input(kOneRow);
  std::ostringstream plain_output;
  CHECK(pipeline::RunSqlQueryCsv("SELECT csvzall_test_answer() FROM data", "data", "csv",
                                 plain_input, plain_output, tests::MakeTestOptions(),
                                 tests::MakeNullLogger(), stats) == 1);
}
#endif

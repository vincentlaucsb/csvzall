#include <catch2/catch_test_macros.hpp>

#include "../src/sqlite/sqlite_db.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path TempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / name;
}

void WriteBytes(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary);
  out << bytes;
}

std::string MainDatabasePath(csvzall::sqlite::SqliteDb& db) {
  SQLite::Statement query(db.db(), "PRAGMA database_list");
  while (query.executeStep()) {
    if (query.getColumn(1).getString() == "main") {
      return query.getColumn(2).getString();
    }
  }
  return {};
}

std::filesystem::path EquivalentPath(const std::filesystem::path& path) {
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(path, ec);
  return ec ? path.lexically_normal() : canonical;
}

}  // namespace

TEST_CASE("SqliteDb: explicit db path is preserved after close") {
  const auto db_path = TempPath("csvzall_sqlite_explicit.db");
  std::filesystem::remove(db_path);

  csvzall::sqlite::SqliteDbOpenOptions options;
  options.db_path = db_path.string();

  {
    auto db = csvzall::sqlite::OpenSqliteDb(options);
    REQUIRE(EquivalentPath(MainDatabasePath(db)) == EquivalentPath(db_path));
    db.db().exec("CREATE TABLE data(value INTEGER)");
  }

  REQUIRE(std::filesystem::exists(db_path));
  std::filesystem::remove(db_path);
}

TEST_CASE("SqliteDb: stdin uses in-memory database") {
  csvzall::sqlite::SqliteDbOpenOptions options;
  options.input_is_stdin = true;
  options.input_path = "-";

  auto db = csvzall::sqlite::OpenSqliteDb(options);

  REQUIRE(MainDatabasePath(db).empty());
}

TEST_CASE("SqliteDb: small file below threshold uses in-memory database") {
  const auto input_path = TempPath("csvzall_sqlite_small.csv");
  WriteBytes(input_path, "a\n1\n");

  csvzall::sqlite::SqliteDbOpenOptions options;
  options.input_path = input_path.string();
  options.threshold_mb = 1;

  auto db = csvzall::sqlite::OpenSqliteDb(options);

  REQUIRE(MainDatabasePath(db).empty());
  std::filesystem::remove(input_path);
}

TEST_CASE("SqliteDb: file above threshold uses self-deleting temp database") {
  const auto input_path = TempPath("csvzall_sqlite_large.csv");
  WriteBytes(input_path, "a\n1\n");

  std::string sqlite_path;
  {
    csvzall::sqlite::SqliteDbOpenOptions options;
    options.input_path = input_path.string();
    options.threshold_mb = 0;

    auto db = csvzall::sqlite::OpenSqliteDb(options);
    sqlite_path = MainDatabasePath(db);

    REQUIRE(!sqlite_path.empty());
    REQUIRE(std::filesystem::exists(sqlite_path));
    db.db().exec("CREATE TABLE data(value INTEGER)");
  }

  REQUIRE(!std::filesystem::exists(sqlite_path));
  std::filesystem::remove(input_path);
}

TEST_CASE("SqliteDb: moved temp database is deleted only once") {
  const auto input_path = TempPath("csvzall_sqlite_move.csv");
  WriteBytes(input_path, "a\n1\n");

  std::string sqlite_path;
  {
    csvzall::sqlite::SqliteDbOpenOptions options;
    options.input_path = input_path.string();
    options.threshold_mb = 0;

    auto first = csvzall::sqlite::OpenSqliteDb(options);
    sqlite_path = MainDatabasePath(first);
    REQUIRE(std::filesystem::exists(sqlite_path));

    auto second = std::move(first);
    REQUIRE(MainDatabasePath(second) == sqlite_path);
  }

  REQUIRE(!std::filesystem::exists(sqlite_path));
  std::filesystem::remove(input_path);
}

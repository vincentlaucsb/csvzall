#pragma once

#ifdef CSVZALL_HAVE_POSTGRESQL

#include <pqxx/pqxx>

#include <string>

namespace csvzall::postgres {

// PostgreSQL connection options
struct ConnectionConfig {
  std::string host = "localhost";
  int port = 5432;
  std::string database;
  std::string user;
  std::string password;

  // Build a libpq connection string consumed by pqxx::connection.
  std::string to_connection_string() const;
};

// Thin RAII wrapper around pqxx::connection.
class PostgresConnection {
 public:
  explicit PostgresConnection(const ConnectionConfig& config);
  ~PostgresConnection() = default;

  PostgresConnection(const PostgresConnection&) = delete;
  PostgresConnection& operator=(const PostgresConnection&) = delete;
  PostgresConnection(PostgresConnection&&) noexcept = default;
  PostgresConnection& operator=(PostgresConnection&&) noexcept = default;

  [[nodiscard]] pqxx::connection& connection() { return connection_; }
  [[nodiscard]] const pqxx::connection& connection() const { return connection_; }

  [[nodiscard]] bool is_open() const { return connection_.is_open(); }

 private:
  pqxx::connection connection_;
};

[[nodiscard]] bool TableExists(pqxx::connection& conn, const std::string& table_name);

void DropTableIfExists(pqxx::connection& conn, const std::string& table_name);

[[nodiscard]] std::string QuoteIdentifier(const std::string& identifier);

}  // namespace csvzall::postgres

namespace csvzall::pipeline {
namespace postgres = ::csvzall::postgres;
}

#endif  // CSVZALL_HAVE_POSTGRESQL

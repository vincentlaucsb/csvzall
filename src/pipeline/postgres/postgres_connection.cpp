#include "postgres_connection.hpp"

#ifdef CSVZALL_HAVE_POSTGRESQL

#include <sstream>
#include <stdexcept>

namespace csvzall::pipeline::postgres {

std::string ConnectionConfig::to_connection_string() const {
  std::ostringstream oss;
  oss << "host=" << host << " ";
  oss << "port=" << port << " ";
  oss << "dbname=" << database << " ";
  oss << "user=" << user;
  if (!password.empty()) {
    oss << " password=" << password;
  }
  return oss.str();
}

PostgresConnection::PostgresConnection(const ConnectionConfig& config)
    : connection_(config.to_connection_string()) {
  if (!connection_.is_open()) {
    throw std::runtime_error("PostgreSQL connection is not open");
  }
}

std::string QuoteIdentifier(const std::string& identifier) {
  std::string out;
  out.reserve(identifier.size() + 2);
  out.push_back('"');
  for (const char ch : identifier) {
    if (ch == '"') {
      out += "\"\"";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('"');
  return out;
}

bool TableExists(pqxx::connection& conn, const std::string& table_name) {
  pqxx::work tx(conn);
  const pqxx::result r = tx.exec_params(
      "SELECT EXISTS ("
      "SELECT 1 FROM information_schema.tables "
      "WHERE table_schema='public' AND table_name=$1)",
      table_name);
  tx.commit();
  return !r.empty() && r[0][0].as<bool>();
}

void DropTableIfExists(pqxx::connection& conn, const std::string& table_name) {
  pqxx::work tx(conn);
  tx.exec("DROP TABLE IF EXISTS " + QuoteIdentifier(table_name));
  tx.commit();
}

}  // namespace csvzall::pipeline::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

#ifdef CSVZALL_HAVE_POSTGRESQL

#include "commands.hpp"

#include "../postgres/postgres_connection.hpp"
#include "../postgres/schema_inference.hpp"
#include "../postgres/row_loader.hpp"

#include <stdexcept>

namespace csvzall::pipeline::commands {

namespace {

enum class IfExistsMode { ERROR, DROP, APPEND };

IfExistsMode ParseIfExistsMode(const std::string& mode_str) {
  if (mode_str == "drop") return IfExistsMode::DROP;
  if (mode_str == "append") return IfExistsMode::APPEND;
  if (mode_str == "error" || mode_str.empty()) return IfExistsMode::ERROR;
  throw std::invalid_argument("postgres: --if-exists must be one of drop|append|error");
}

std::string ColumnToDdl(const postgres::InferredColumn& col) {
  std::string ddl = postgres::QuoteIdentifier(col.name) + " " + postgres::ColumnTypeToString(col.type);
  if (!col.nullable) {
    ddl += " NOT NULL";
  }
  return ddl;
}

void CreateTable(pqxx::connection& conn,
                 const std::string& table_name,
                 const std::vector<postgres::InferredColumn>& inferred_columns) {
  std::string ddl = "CREATE TABLE " + postgres::QuoteIdentifier(table_name) + " (";
  for (size_t i = 0; i < inferred_columns.size(); ++i) {
    if (i > 0) {
      ddl += ", ";
    }
    ddl += ColumnToDdl(inferred_columns[i]);
  }
  ddl += ")";

  pqxx::work tx(conn);
  tx.exec(ddl);
  tx.commit();
}

}  // namespace

class PostgresExportCommand : public CsvInputCommand {
 public:
  PostgresExportCommand(std::istream& input,
                        const RunOptions& options,
                        const LoggerCallbacks& logger,
                        RunStats& stats,
                        const postgres::ConnectionConfig& pg_config,
                        const std::string& table_name,
                        const IfExistsMode if_exists_mode,
                        const postgres::RowLoaderConfig& row_config)
      : CsvInputCommand(input, options, logger, stats),
        pg_config_(pg_config),
        table_name_(table_name),
        if_exists_mode_(if_exists_mode),
        row_config_(row_config) {}

 protected:
  int run() override {
    try {
      postgres::PostgresConnection conn(pg_config_);
      pqxx::connection& pq_conn = conn.connection();

      if (logger().verbose) {
        logger().verbose("Connected to PostgreSQL database: " + pg_config_.database);
      }

      bool table_exists = postgres::TableExists(pq_conn, table_name_);

      if (table_exists) {
        switch (if_exists_mode_) {
          case IfExistsMode::ERROR:
            if (logger().error) {
              logger().error("Table '" + table_name_ + "' already exists (use --if-exists drop|append)");
            }
            return 1;

          case IfExistsMode::DROP:
            if (logger().verbose) {
              logger().verbose("Dropping existing table: " + table_name_);
            }
            postgres::DropTableIfExists(pq_conn, table_name_);
            table_exists = false;
            break;

          case IfExistsMode::APPEND:
            if (logger().verbose) {
              logger().verbose("Appending to existing table: " + table_name_);
            }
            return LoadAppendMode(pq_conn);
        }
      }

      if (logger().verbose) {
        logger().verbose("Inferring schema from first 1000 rows...");
      }

      postgres::SchemaInference schema_inference(headers());
      std::vector<std::vector<std::string>> sampled_rows;
      sampled_rows.reserve(1000);

      for (const auto& row : reader()) {
        if (schema_inference.has_enough_samples()) {
          break;
        }
        auto values = row.get_row();
        schema_inference.observe_row(values);
        sampled_rows.push_back(std::move(values));
      }

      const auto inferred_columns = schema_inference.finalize();

      if (logger().verbose) {
        logger().verbose("Inferred schema from " + std::to_string(sampled_rows.size()) + " rows");
      }

      if (!table_exists) {
        try {
          CreateTable(pq_conn, table_name_, inferred_columns);
          if (logger().verbose) {
            logger().verbose("Created table: " + table_name_);
          }
        } catch (const std::exception& ex) {
          if (logger().error) {
            logger().error(std::string("Failed to create table: ") + ex.what());
          }
          return 1;
        }
      }

      postgres::RowLoader loader(pq_conn, table_name_, inferred_columns, row_config_);
      for (const auto& sampled : sampled_rows) {
        loader.add_row(sampled);
      }
      for (const auto& row : reader()) {
        loader.add_row(row.get_row());
      }
      loader.flush();

      stats().rows_processed = loader.rows_loaded();

      const auto filtered = loader.rows_skipped_filtered();
      const auto mismatched = loader.rows_skipped_type_mismatch();
      if (filtered > 0 || mismatched > 0) {
        if (logger().error) {
          logger().error("postgres: skipped " + std::to_string(filtered + mismatched) +
                        " rows (filtered=" + std::to_string(filtered) +
                        ", type_mismatch=" + std::to_string(mismatched) + ")");
        }
      }
      if (logger().verbose) {
        logger().verbose("postgres: inserted " + std::to_string(loader.rows_loaded()) + " rows");
      }

      return 0;

    } catch (const std::exception& ex) {
      if (logger().error) {
        logger().error(std::string("PostgreSQL export error: ") + ex.what());
      }
      return 1;
    }
  }

 private:
  postgres::ConnectionConfig pg_config_;
  std::string table_name_;
  IfExistsMode if_exists_mode_;
  postgres::RowLoaderConfig row_config_;

  int LoadAppendMode(pqxx::connection& conn) {
    std::vector<postgres::InferredColumn> columns;
    columns.reserve(headers().size());
    for (const auto& header : headers()) {
      columns.push_back({header, postgres::ColumnType::TEXT, true});
    }

    postgres::RowLoader loader(conn, table_name_, columns, row_config_);

    for (const auto& row : reader()) {
      loader.add_row(row.get_row());
    }
    loader.flush();

    stats().rows_processed = loader.rows_loaded();

    const auto filtered = loader.rows_skipped_filtered();
    const auto mismatched = loader.rows_skipped_type_mismatch();
    if (filtered > 0 || mismatched > 0) {
      if (logger().error) {
        logger().error("postgres: skipped " + std::to_string(filtered + mismatched) +
                      " rows (filtered=" + std::to_string(filtered) +
                      ", type_mismatch=" + std::to_string(mismatched) + ")");
      }
    }
    if (logger().verbose) {
      logger().verbose("postgres: inserted " + std::to_string(loader.rows_loaded()) + " rows");
    }

    return 0;
  }
};

int RunPostgresExport(std::istream& input,
                      const RunOptions& options,
                      const LoggerCallbacks& logger,
                      RunStats& stats,
                      const postgres::ConnectionConfig& pg_config,
                      const std::string& table_name,
                      const std::string& if_exists_mode,
                      const postgres::RowLoaderConfig& row_config) {
  const IfExistsMode mode = ParseIfExistsMode(if_exists_mode);
  PostgresExportCommand cmd(input, options, logger, stats, pg_config, table_name, mode, row_config);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

#endif  // CSVZALL_HAVE_POSTGRESQL

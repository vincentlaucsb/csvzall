#ifdef CSVZALL_HAVE_POSTGRESQL

#include "commands.hpp"

#include "../postgres/exporter.hpp"

#include <exception>
#include <string>
#include <utility>

namespace csvzall::pipeline::commands {

namespace {

postgres::LogCallbacks MakePostgresLogCallbacks(const LoggerCallbacks& logger) {
  return {
      logger.error,
      logger.verbose,
      logger.info,
      logger.progress_start,
      logger.progress_update,
      logger.progress_finish,
  };
}

postgres::LoadOptions MakePostgresLoadOptions(const RunOptions& options) {
  return {
      options.postgres_copy_batch_rows,
      options.postgres_parallel_copy_workers,
  };
}

}  // namespace

class PostgresInferCommand : public CsvInputCommand {
 public:
  PostgresInferCommand(std::istream& input,
                       const RunOptions& options,
                       const LoggerCallbacks& logger,
                       RunStats& stats,
                       std::string table_name)
      : CsvInputCommand(input, options, logger, stats),
        table_name_(std::move(table_name)) {}

 protected:
  int run() override {
    try {
      if (logger().verbose) {
        logger().verbose("Inferring PostgreSQL schema from full input...");
      }

      const auto postgres_logger = MakePostgresLogCallbacks(logger());
      const auto schema = postgres::InferSchema(reader(), headers(), postgres_logger);
      postgres::DumpInferredSchema(postgres_logger, table_name_, schema.columns, schema.observed_rows);
      stats().rows_processed = schema.observed_rows;

      if (logger().verbose) {
        logger().verbose("postgres infer: inspected " +
                         std::to_string(schema.observed_rows) + " rows");
      }

      return 0;
    } catch (const std::exception& ex) {
      if (logger().error) {
        logger().error(std::string("PostgreSQL schema inference error: ") + ex.what());
      }
      return 1;
    }
  }

 private:
  std::string table_name_;
};

class PostgresExportCommand : public CsvInputCommand {
 public:
  PostgresExportCommand(std::istream& input,
                        const RunOptions& options,
                        const LoggerCallbacks& logger,
                        RunStats& stats,
                        const postgres::ConnectionConfig& pg_config,
                        std::string table_name,
                        const postgres::IfExistsMode if_exists_mode)
      : CsvInputCommand(input, options, logger, stats),
        pg_config_(pg_config),
        table_name_(std::move(table_name)),
        if_exists_mode_(if_exists_mode) {}

 protected:
  int run() override {
    const auto result = postgres::ExportCsvToPostgres(
        reader(),
        headers(),
        [this]() { return reset_reader(); },
        pg_config_,
        table_name_,
        if_exists_mode_,
        MakePostgresLoadOptions(options()),
        MakePostgresLogCallbacks(logger()));

    stats().rows_processed = result.rows_loaded;
    return result.return_code;
  }

 private:
  postgres::ConnectionConfig pg_config_;
  std::string table_name_;
  postgres::IfExistsMode if_exists_mode_;
};

int RunPostgresExport(std::istream& input,
                      const RunOptions& options,
                      const LoggerCallbacks& logger,
                      RunStats& stats,
                      const postgres::ConnectionConfig& pg_config,
                      const std::string& table_name,
                      const std::string& if_exists_mode) {
  const auto mode = postgres::ParseIfExistsMode(if_exists_mode);
  PostgresExportCommand cmd(input, options, logger, stats, pg_config, table_name, mode);
  return cmd.execute();
}

int RunPostgresInfer(std::istream& input,
                     const RunOptions& options,
                     const LoggerCallbacks& logger,
                     RunStats& stats,
                     const std::string& table_name) {
  PostgresInferCommand cmd(input, options, logger, stats, table_name);
  return cmd.execute();
}

}  // namespace csvzall::pipeline::commands

#endif  // CSVZALL_HAVE_POSTGRESQL

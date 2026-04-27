#ifdef CSVZALL_HAVE_POSTGRESQL

#include "commands.hpp"

#include "../postgres/postgres_connection.hpp"
#include "../postgres/schema_inference.hpp"
#include "../postgres/row_loader.hpp"

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace csvzall::pipeline::commands {

namespace {

enum class IfExistsMode { kError, kDrop, kAppend };

IfExistsMode ParseIfExistsMode(const std::string& mode_str) {
  if (mode_str == "drop") return IfExistsMode::kDrop;
  if (mode_str == "append") return IfExistsMode::kAppend;
  if (mode_str == "error" || mode_str.empty()) return IfExistsMode::kError;
  throw std::invalid_argument("postgres: --if-exists must be one of drop|append|error");
}

std::string ColumnToDdl(const postgres::InferredColumn& col) {
  std::string ddl = postgres::QuoteIdentifier(col.name) + " " + postgres::ColumnTypeToString(col.type);
  if (!col.nullable) {
    ddl += " NOT NULL";
  }
  return ddl;
}

void CreateTable(pqxx::work& tx,
                 const std::string& table_name,
                 const std::vector<postgres::InferredColumn>& inferred_columns) {
  std::string ddl = "CREATE UNLOGGED TABLE " + postgres::QuoteIdentifier(table_name) + " (";
  for (size_t i = 0; i < inferred_columns.size(); ++i) {
    if (i > 0) {
      ddl += ", ";
    }
    ddl += ColumnToDdl(inferred_columns[i]);
  }
  ddl += ")";

  tx.exec(ddl);
}

void DumpInferredSchema(const LoggerCallbacks& logger,
                        const std::string& table_name,
                        const std::vector<postgres::InferredColumn>& inferred_columns,
                        const std::size_t observed_rows) {
  if (!logger.info) {
    return;
  }

  logger.info("postgres: inferred schema for " + table_name + " from " +
              std::to_string(observed_rows) + " observed rows:");

  for (const auto& col : inferred_columns) {
    std::ostringstream line;
    line << "  " << col.name << " " << postgres::ColumnTypeToString(col.type);
    if (!col.nullable) {
      line << " NOT NULL";
    }
    logger.info(line.str());
  }
}

std::size_t ObservedRows(const std::vector<postgres::SchemaInference::ColumnStats>& stats) {
  std::size_t rows = 0;
  for (const auto& col : stats) {
    const auto observed = static_cast<std::size_t>(col.total_non_null + col.null_count);
    if (observed > rows) {
      rows = observed;
    }
  }
  return rows;
}

constexpr std::size_t kPostgresCopyQueueDepth = 3;
constexpr std::size_t kPostgresInferChunkSize = 50000;

struct InferredPostgresSchema {
  std::vector<postgres::InferredColumn> columns;
  std::size_t observed_rows = 0;
};

InferredPostgresSchema InferPostgresSchema(csv::CSVReader& reader,
                                           const std::vector<std::string>& headers,
                                           const LoggerCallbacks& logger) {
  std::vector<postgres::SchemaInference::ColumnStats> column_stats;
  column_stats.reserve(headers.size());
  for (const auto& header : headers) {
    postgres::SchemaInference::ColumnStats stats;
    stats.name = header;
    column_stats.push_back(std::move(stats));
  }

  if (logger.progress_start) {
    logger.progress_start("postgres infer", 0);
  }

  try {
    csv::DataFrameExecutor executor;
    std::uint64_t rows_seen = 0;
    csv::chunk_parallel_apply(
        reader,
        executor,
        column_stats,
        [&logger, &rows_seen](csv::DataFrame<>::column_type column,
                              postgres::SchemaInference::ColumnStats& stats) {
          for (const auto& cell : column) {
            postgres::SchemaInference::observe_type(stats, cell.type());
          }

          if (column.index() == 0) {
            rows_seen += static_cast<std::uint64_t>(column.size());
            if (logger.progress_update) {
              logger.progress_update(rows_seen);
            }
          }
        },
        kPostgresInferChunkSize);
  } catch (...) {
    if (logger.progress_finish) {
      logger.progress_finish();
    }
    throw;
  }

  if (logger.progress_finish) {
    logger.progress_finish();
  }

  InferredPostgresSchema result;
  result.columns = postgres::SchemaInference::finalize_stats(column_stats);
  result.observed_rows = ObservedRows(column_stats);
  return result;
}

void CopyRowsWithPipeline(csv::CSVReader& reader,
                          postgres::RowLoader& loader,
                          const LoggerCallbacks& logger,
                          const std::uint64_t total_rows,
                          const std::size_t chunk_size) {
  if (chunk_size == 0) {
    throw std::invalid_argument("postgres: COPY batch size must be greater than 0");
  }

  struct QueueState {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::vector<csv::CSVRow>> queue;
    bool done = false;
    bool cancel = false;
    std::exception_ptr producer_error;
  };

  QueueState state;

  if (logger.progress_start) {
    logger.progress_start("postgres COPY", total_rows);
  }

  std::thread producer([&]() {
    try {
      std::vector<csv::CSVRow> rows;
      while (reader.read_chunk(rows, chunk_size)) {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.cv.wait(lock, [&]() {
          return state.cancel || state.queue.size() < kPostgresCopyQueueDepth;
        });
        if (state.cancel) {
          break;
        }
        state.queue.push_back(std::move(rows));
        lock.unlock();
        state.cv.notify_all();
      }
    } catch (...) {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.producer_error = std::current_exception();
    }

    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.done = true;
    }
    state.cv.notify_all();
  });

  std::uint64_t rows_seen = 0;
  try {
    while (true) {
      std::vector<csv::CSVRow> rows;
      {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.cv.wait(lock, [&]() {
          return state.done || !state.queue.empty();
        });

        if (state.queue.empty()) {
          break;
        }

        rows = std::move(state.queue.front());
        state.queue.pop_front();
      }
      state.cv.notify_all();

      rows_seen += rows.size();
      loader.add_rows(rows);
      if (logger.progress_update) {
        logger.progress_update(rows_seen);
      }
    }
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.cancel = true;
    }
    state.cv.notify_all();
    producer.join();
    if (logger.progress_finish) {
      logger.progress_finish();
    }
    throw;
  }

  producer.join();

  if (state.producer_error) {
    if (logger.progress_finish) {
      logger.progress_finish();
    }
    std::rethrow_exception(state.producer_error);
  }

  if (logger.progress_finish) {
    logger.progress_finish();
  }
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

      const auto schema = InferPostgresSchema(reader(), headers(), logger());
      DumpInferredSchema(logger(), table_name_, schema.columns, schema.observed_rows);
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
                        const std::string& table_name,
                        const IfExistsMode if_exists_mode)
      : CsvInputCommand(input, options, logger, stats),
        pg_config_(pg_config),
        table_name_(table_name),
        if_exists_mode_(if_exists_mode) {}

 protected:
  int run() override {
    try {
      postgres::PostgresConnection conn(pg_config_);
      pqxx::connection& pq_conn = conn.connection();

      if (logger().verbose) {
        logger().verbose("Connected to PostgreSQL database: " + pg_config_.database);
      }

      bool table_exists = postgres::TableExists(pq_conn, table_name_);
      bool drop_existing_table = false;

      if (table_exists) {
        switch (if_exists_mode_) {
          case IfExistsMode::kError:
            if (logger().error) {
              logger().error("Table '" + table_name_ + "' already exists (use --if-exists drop|append)");
            }
            return 1;

          case IfExistsMode::kDrop:
            if (logger().verbose) {
              logger().verbose("Dropping existing table: " + table_name_);
            }
            drop_existing_table = true;
            table_exists = false;
            break;

          case IfExistsMode::kAppend:
            if (logger().verbose) {
              logger().verbose("Appending to existing table: " + table_name_);
            }
            return LoadAppendMode(pq_conn);
        }
      }

      if (logger().verbose) {
        logger().verbose("Inferring schema from full input...");
      }

      const auto schema = InferPostgresSchema(reader(), headers(), logger());
      const auto& inferred_columns = schema.columns;
      const auto observed_rows = schema.observed_rows;
      DumpInferredSchema(logger(), table_name_, inferred_columns, observed_rows);

      if (logger().verbose) {
        logger().verbose("Inferred schema from " + std::to_string(observed_rows) + " rows");
      }

      if (int rc = reset_reader(); rc != 0) {
        return rc;
      }

      if (!table_exists) {
        try {
          pqxx::work tx(pq_conn);
          tx.exec("SET LOCAL synchronous_commit = off");
          if (drop_existing_table) {
            tx.exec("DROP TABLE IF EXISTS " + postgres::QuoteIdentifier(table_name_));
          }
          CreateTable(tx, table_name_, inferred_columns);
          if (logger().verbose) {
            logger().verbose("Created table: " + table_name_);
          }

          postgres::RowLoader loader(pq_conn, tx, table_name_, inferred_columns);
          CopyRowsWithPipeline(
              reader(), loader, logger(), observed_rows, options().postgres_copy_batch_rows);
          loader.flush();
          tx.commit();

          stats().rows_processed = loader.rows_loaded();

          const auto mismatched = loader.rows_skipped_type_mismatch();
          if (mismatched > 0) {
            if (logger().error) {
              logger().error("postgres: skipped " + std::to_string(mismatched) +
                            " rows (type_mismatch=" + std::to_string(mismatched) + ")");
            }
          }
          if (logger().verbose) {
            logger().verbose("postgres: inserted " + std::to_string(loader.rows_loaded()) + " rows");
          }
        } catch (const std::exception& ex) {
          if (logger().progress_finish) {
            logger().progress_finish();
          }
          if (logger().error) {
            logger().error(std::string("PostgreSQL export transaction failed: ") + ex.what());
          }
          return 1;
        }

        return 0;
      }

      pqxx::work tx(pq_conn);
      tx.exec("SET LOCAL synchronous_commit = off");
      postgres::RowLoader loader(pq_conn, tx, table_name_, inferred_columns);
      CopyRowsWithPipeline(
          reader(), loader, logger(), observed_rows, options().postgres_copy_batch_rows);
      loader.flush();
      tx.commit();

      stats().rows_processed = loader.rows_loaded();

      const auto mismatched = loader.rows_skipped_type_mismatch();
      if (mismatched > 0) {
        if (logger().error) {
          logger().error("postgres: skipped " + std::to_string(mismatched) +
                        " rows (type_mismatch=" + std::to_string(mismatched) + ")");
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

  int LoadAppendMode(pqxx::connection& conn) {
    std::vector<postgres::InferredColumn> columns;
    columns.reserve(headers().size());
    for (const auto& header : headers()) {
      columns.push_back({header, postgres::ColumnType::TEXT, true});
    }

    pqxx::work tx(conn);
    tx.exec("SET LOCAL synchronous_commit = off");
    postgres::RowLoader loader(conn, tx, table_name_, columns);

    CopyRowsWithPipeline(
        reader(), loader, logger(), 0, options().postgres_copy_batch_rows);
    loader.flush();
    tx.commit();

    stats().rows_processed = loader.rows_loaded();

    const auto mismatched = loader.rows_skipped_type_mismatch();
    if (mismatched > 0) {
      if (logger().error) {
        logger().error("postgres: skipped " + std::to_string(mismatched) +
                      " rows (type_mismatch=" + std::to_string(mismatched) + ")");
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
                      const std::string& if_exists_mode) {
  const IfExistsMode mode = ParseIfExistsMode(if_exists_mode);
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

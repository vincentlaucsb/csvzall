#pragma once

#ifdef CSVZALL_HAVE_POSTGRESQL

#include "postgres_connection.hpp"
#include "row_loader.hpp"
#include "schema_inference.hpp"

#include <csv.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace csvzall::postgres {

enum class IfExistsMode { kError, kDrop, kAppend };

struct LogCallbacks {
  std::function<void(const std::string&)> error;
  std::function<void(const std::string&)> verbose;
  std::function<void(const std::string&)> info;
  std::function<void(const std::string&, std::uint64_t)> progress_start;
  std::function<void(std::uint64_t)> progress_update;
  std::function<void()> progress_finish;
};

struct LoadOptions {
  std::size_t copy_batch_rows = 10000;
  std::size_t parallel_copy_workers = 1;
};

struct CopyResult {
  std::uint64_t rows_loaded = 0;
  std::uint64_t rows_skipped_type_mismatch = 0;
};

struct InferredSchema {
  std::vector<InferredColumn> columns;
  std::size_t observed_rows = 0;
};

struct ExportResult {
  bool success = true;
  int return_code = 0;
  std::uint64_t rows_loaded = 0;
  std::uint64_t rows_skipped_type_mismatch = 0;
  std::size_t observed_rows = 0;
};

IfExistsMode ParseIfExistsMode(const std::string& mode_str);

InferredSchema InferSchema(csv::CSVReader& reader,
                           const std::vector<std::string>& headers,
                           const LogCallbacks& logger);

void DumpInferredSchema(const LogCallbacks& logger,
                        const std::string& table_name,
                        const std::vector<InferredColumn>& inferred_columns,
                        std::size_t observed_rows);

void CopyRowsWithPipeline(csv::CSVReader& reader,
                          RowLoader& loader,
                          const LogCallbacks& logger,
                          std::uint64_t total_rows,
                          std::size_t chunk_size);

CopyResult CopyRowsWithParallelWorkers(csv::CSVReader& reader,
                                       const ConnectionConfig& pg_config,
                                       const std::string& table_name,
                                       const std::vector<InferredColumn>& columns,
                                       const LogCallbacks& logger,
                                       std::uint64_t total_rows,
                                       std::size_t chunk_size,
                                       std::size_t worker_count);

ExportResult ExportCsvToPostgres(csv::CSVReader& reader,
                                 const std::vector<std::string>& headers,
                                 const std::function<int()>& reset_reader,
                                 const ConnectionConfig& pg_config,
                                 const std::string& table_name,
                                 IfExistsMode if_exists_mode,
                                 const LoadOptions& options,
                                 const LogCallbacks& logger);

}  // namespace csvzall::postgres

namespace csvzall::pipeline {
namespace postgres = ::csvzall::postgres;
}

#endif  // CSVZALL_HAVE_POSTGRESQL

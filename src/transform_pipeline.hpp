#pragma once

#include "pipeline_types.hpp"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

#ifdef CSVZALL_HAVE_POSTGRESQL
#include "pipeline/postgres/postgres_connection.hpp"
#include "pipeline/postgres/row_loader.hpp"
#endif

namespace csvzall::pipeline {

int RunDerive(const std::string& assignment, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunFilter(const std::string& expression, std::istream& input, std::ostream& output,
              const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunSummarize(const std::string& group_by_column, const std::string& max_column,
                 const std::vector<std::string>& show_columns,
                 std::istream& input, std::ostream& output,
                 const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunTimeseries(const std::string& x_column, const std::string& y_column,
                  const std::string& series_column, const std::string& reduce,
                  const std::string& format,
                  std::istream& input, std::ostream& output,
                  const RunOptions& options, const LoggerCallbacks& logger, RunStats& stats);

int RunSqlExport(const std::string& input_path,
                 const std::string& dest_path,
                 const std::string& table_name,
                 std::istream& input,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats);

int RunSqlQueryCsv(const std::string& sql_query,
                   const std::string& table_name,
                   std::istream& input,
                   std::ostream& output,
                   const RunOptions& options,
                   const LoggerCallbacks& logger,
                   RunStats& stats);

enum class SqlQueryInputKind {
    kCsv,
    kSqlite,
    kUnknown,
};

SqlQueryInputKind DetectSqlQueryInputKind(const std::string& path);

int RunSqlQueryDb(const std::string& sql_query,
                  const std::string& db_path,
                  std::ostream& output,
                  const LoggerCallbacks& logger,
                  RunStats& stats);

bool ShouldWarnIntegerDivision(const std::string& sql_query);

#ifdef CSVZALL_HAVE_POSTGRESQL
int RunPostgresExport(std::istream& input,
                      const RunOptions& options,
                      const LoggerCallbacks& logger,
                      RunStats& stats,
                      const postgres::ConnectionConfig& pg_config,
                      const std::string& table_name,
                      const std::string& if_exists_mode,
                      const postgres::RowLoaderConfig& row_config);
#endif

}  // namespace csvzall::pipeline

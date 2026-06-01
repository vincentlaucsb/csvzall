#pragma once

#include "pipeline_types.hpp"
#include "charts/chart_spec.hpp"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

#ifdef CSVZALL_HAVE_POSTGRESQL
#include "postgres/postgres_connection.hpp"
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

int RunJsonExtract(const std::string& input_path,
                   const std::string& mapping_path,
                   std::ostream& output,
                   const LoggerCallbacks& logger,
                   RunStats& stats);

int RunMax(const std::string& column,
           std::istream& input,
           std::ostream& output,
           const RunOptions& options,
           const LoggerCallbacks& logger,
           RunStats& stats);

int RunMin(const std::string& column,
           std::istream& input,
           std::ostream& output,
           const RunOptions& options,
           const LoggerCallbacks& logger,
           RunStats& stats);

int RunAppend(const std::string& existing_path,
              const std::string& incoming_path,
              bool in_place,
              std::ostream& output,
              const LoggerCallbacks& logger,
              RunStats& stats);

int RunMerge(const std::string& existing_path,
             const std::string& incoming_path,
             const std::string& key_column,
             bool in_place,
             std::ostream& output,
             const LoggerCallbacks& logger,
             RunStats& stats);

int RunView(const std::string& input_path,
            std::ostream& output,
            const RunOptions& options,
            const LoggerCallbacks& logger,
            RunStats& stats,
            int requested_port,
            bool open_browser,
            bool serve_once,
            bool startup_json);

#ifdef CSVZALL_HAVE_SVGPLOT
int RunHeatmap(const std::string& date_column,
               const std::string& value_column,
               const std::string& label_column,
               const std::string& start_date,
               const std::string& end_date,
               const std::string& title,
               std::istream& input,
               std::ostream& output,
               const RunOptions& options,
               const LoggerCallbacks& logger,
               RunStats& stats);
int RunHeatmap(const common::HeatmapSpec& spec,
               std::istream& input,
               std::ostream& output,
               const RunOptions& options,
               const LoggerCallbacks& logger,
               RunStats& stats);
int RunCharts(const std::string& config_path,
              const std::string& chart_id,
              bool validate_only,
              const RunOptions& options,
              const LoggerCallbacks& logger,
              RunStats& stats);
#endif

int RunSqlExport(const std::string& input_path,
                 const std::string& dest_path,
                 const std::string& table_name,
                 std::istream& input,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats);

int RunSqlQueryCsv(const std::string& sql_query,
                   const std::string& table_name,
                   const std::string& format,
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
                  const std::string& format,
                  std::ostream& output,
                  const LoggerCallbacks& logger,
                  RunStats& stats);

bool ShouldWarnIntegerDivision(const std::string& sql_query);

#ifdef CSVZALL_HAVE_POSTGRESQL
int RunPostgresExport(std::istream& input,
                      const RunOptions& options,
                      const LoggerCallbacks& logger,
                      RunStats& stats,
                      const ::csvzall::postgres::ConnectionConfig& pg_config,
                      const std::string& table_name,
                      const std::string& if_exists_mode);

int RunPostgresInfer(std::istream& input,
                     const RunOptions& options,
                     const LoggerCallbacks& logger,
                     RunStats& stats,
                     const std::string& table_name);
#endif

}  // namespace csvzall::pipeline

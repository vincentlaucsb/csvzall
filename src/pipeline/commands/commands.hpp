#pragma once

#include <csv.hpp>
#include <istream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../pipeline_types.hpp"

namespace csvzall::pipeline::commands {

// ---------------------------------------------------------------------------
// CsvInputCommand
// Base for any command that consumes a CSV input stream.
// Owns the CSVReader, header list, options, logger, and stats.
// Does NOT own an output stream — that is the concern of subclasses.
// ---------------------------------------------------------------------------
class CsvInputCommand {
public:
  virtual ~CsvInputCommand() = default;

  int execute();

protected:
  CsvInputCommand(std::istream& input,
                  const RunOptions& options, const LoggerCallbacks& logger,
                  RunStats& stats);

  virtual int run() = 0;

  virtual csv::CSVFormat make_format() const;

  csv::CSVReader& reader() { return *reader_; }
  const std::vector<std::string>& headers() const { return headers_; }
  const RunOptions& options() const { return options_; }
  const LoggerCallbacks& logger() const { return logger_; }
  RunStats& stats() { return stats_; }

private:
  int init_reader();

  std::istream& input_;
  const RunOptions& options_;
  const LoggerCallbacks& logger_;
  RunStats& stats_;

  std::unique_ptr<std::istringstream> buffered_input_;
  std::unique_ptr<csv::CSVReader> reader_;
  std::vector<std::string> headers_;
};

// ---------------------------------------------------------------------------
// CsvTransformCommand
// Extends CsvInputCommand for commands that write CSV to an output stream.
// ---------------------------------------------------------------------------
class CsvTransformCommand : public CsvInputCommand {
protected:
  CsvTransformCommand(std::istream& input, std::ostream& output,
                      const RunOptions& options, const LoggerCallbacks& logger,
                      RunStats& stats);

  std::ostream& output() { return output_; }

private:
  std::ostream& output_;
};


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

// Export a CSV to a SQLite database file.
// input_path  — original file path (used to derive dest_path when dest_path is empty).
// dest_path   — explicit destination; if empty, derived from input_path (.csv -> .db).
// table_name  — name of the table to create inside the database.
int RunSqlExport(const std::string& input_path,
                 const std::string& dest_path,
                 const std::string& table_name,
                 std::istream& input,
                 const RunOptions& options,
                 const LoggerCallbacks& logger,
                 RunStats& stats);

// Run an arbitrary SQL query against a CSV loaded into a SQLite table.
// The result set is streamed as CSV to output.
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

// Detect SQL query input type from path extension.
// CSV extensions: .csv, .txt
// SQLite extensions: .db, .sqlite, .sqlite3
// Returns kUnknown for stdin ("-") or unrecognized extensions.
SqlQueryInputKind DetectSqlQueryInputKind(const std::string& path);

// Run an arbitrary SQL query against an existing SQLite database file.
// The result set is streamed as CSV to output.
int RunSqlQueryDb(const std::string& sql_query,
                  const std::string& db_path,
                  std::ostream& output,
                  const LoggerCallbacks& logger,
                  RunStats& stats);

}  // namespace csvzall::pipeline::commands

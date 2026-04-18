#include <argparse/argparse.hpp>
#include "head.hpp"
#include "transform_pipeline.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace {

class Logger {
 public:
  explicit Logger(bool verbose) : verbose_(verbose) {}

  void Info(const std::string& msg) const {
    std::cerr << "[info] " << msg << '\n';
  }

  void Error(const std::string& msg) const {
    std::cerr << "[error] " << msg << '\n';
  }

  void Verbose(const std::string& msg) const {
    if (verbose_) {
      std::cerr << "[verbose] " << msg << '\n';
    }
  }

  [[nodiscard]] bool is_verbose() const { return verbose_; }

 private:
  bool verbose_ = false;
};

struct ThroughputStats {
  std::uint64_t rows_processed = 0;
  std::uint64_t bytes_processed = 0;
  std::chrono::steady_clock::duration elapsed{};
};

std::istream* ResolveInput(const std::string& input_path, std::unique_ptr<std::ifstream>& file_holder,
                           Logger& logger) {
  if (input_path.empty() || input_path == "-") {
    logger.Verbose("Reading CSV from stdin");
    return &std::cin;
  }

  file_holder = std::make_unique<std::ifstream>(input_path, std::ios::binary);
  if (!file_holder->is_open()) {
    return nullptr;
  }

  logger.Verbose("Reading CSV from file: " + input_path);
  return file_holder.get();
}

void MaybePrintVerboseStats(const ThroughputStats& stats, const Logger& logger) {
  if (!logger.is_verbose()) {
    return;
  }

  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(stats.elapsed).count();
  const double seconds = ms > 0 ? static_cast<double>(ms) / 1000.0 : 0.0;
  const double mib = static_cast<double>(stats.bytes_processed) / (1024.0 * 1024.0);
  const double mib_per_sec = seconds > 0.0 ? (mib / seconds) : 0.0;

  std::ostringstream oss;
  oss << "rows=" << stats.rows_processed << ", elapsed=" << ms << "ms"
      << ", throughput=" << mib_per_sec << " MiB/s";
  logger.Verbose(oss.str());
}

// Empty string → nullopt (auto-detect). "tab" or "\t" → '\t'. "pipe" → '|'.
// Any other non-empty string → its first character.
std::optional<char> ParseDelimiter(const std::string& s) {
  if (s.empty()) return std::nullopt;
  if (s == "tab" || s == "\\t") return '\t';
  if (s == "pipe") return '|';
  return s[0];
}

// Register arguments shared by all CSV-consuming subcommands:
// positional input, --single-threaded, --verbose, -d/--delimiter.
void AddCsvInputArguments(argparse::ArgumentParser& cmd) {
  cmd.add_argument("input")
      .help("Input CSV file path, or '-' for stdin")
      .default_value(std::string{"-"})
      .nargs(argparse::nargs_pattern::optional);
  cmd.add_argument("--single-threaded")
      .help("Disable csv-parser multithreading")
      .default_value(false)
      .implicit_value(true);
  cmd.add_argument("--verbose")
      .help("Print throughput and diagnostic logs to stderr")
      .default_value(false)
      .implicit_value(true);
  cmd.add_argument("-d", "--delimiter")
      .help("Input field delimiter: single char, 'tab', or '\\t' (default: auto-detect)")
      .default_value(std::string{""});
}

// Register --exact for commands that perform column name lookups.
void AddExactArgument(argparse::ArgumentParser& cmd) {
  cmd.add_argument("--exact")
      .help("Use exact case-sensitive column name matching")
      .default_value(false)
      .implicit_value(true);
}

csvzall::pipeline::LoggerCallbacks BuildCallbacks(const Logger& logger) {
  return {
    [&logger](const std::string& msg) { logger.Error(msg); },
    [&logger](const std::string& msg) { logger.Verbose(msg); }};
}

csvzall::pipeline::RunOptions BuildTransformOptions(
    const argparse::ArgumentParser& cmd, std::istream* input, const std::string& input_path) {
  csvzall::pipeline::RunOptions options;
  options.single_threaded = cmd.get<bool>("--single-threaded");
  options.input_is_stdin = (input == &std::cin);
  options.exact_column_matching = cmd.get<bool>("--exact");
  options.delimiter = ParseDelimiter(cmd.get<std::string>("--delimiter"));
  options.input_path = input_path;
  return options;
}

ThroughputStats FinishStats(const csvzall::pipeline::RunStats& ps,
                             std::chrono::steady_clock::time_point start) {
  return {ps.rows_processed, ps.bytes_processed,
          std::chrono::steady_clock::now() - start};
}

}  // namespace

int main(int argc, char** argv) {
  argparse::ArgumentParser program("csvzall", "0.1.0");
  program.add_description("csvzall: high-performance CSV transformation CLI");

  argparse::ArgumentParser derive_cmd("derive");
  derive_cmd.add_description("Add or overwrite a column using an expression");
  derive_cmd.add_argument("assignment").help("Format: NewCol = expression");
  AddCsvInputArguments(derive_cmd);
  AddExactArgument(derive_cmd);;

  argparse::ArgumentParser filter_cmd("filter");
  filter_cmd.add_description("Keep rows where expression is truthy");
  filter_cmd.add_argument("expression").help("Filter expression evaluated per row");
  AddCsvInputArguments(filter_cmd);
  AddExactArgument(filter_cmd);;

    argparse::ArgumentParser head_cmd("head");
    head_cmd.add_description("Print header and first rows as a neat table");
    head_cmd.add_argument("input")
      .help("Input CSV file path, or '-' for stdin")
      .default_value(std::string{"-"})
      .nargs(argparse::nargs_pattern::optional);
    head_cmd.add_argument("-n", "--rows")
      .help("Number of data rows to display")
      .default_value(50)
      .scan<'i', int>();
    head_cmd.add_argument("--verbose")
      .help("Print throughput and diagnostic logs to stderr")
      .default_value(false)
      .implicit_value(true);
    head_cmd.add_argument("-d", "--delimiter")
      .help("Input field delimiter: single char, 'tab', or '\\t' (default: auto-detect)")
      .default_value(std::string{""});

  argparse::ArgumentParser summarize_cmd("summarize");
  summarize_cmd.add_description("Group rows and compute aggregate statistics");
  summarize_cmd.add_argument("--group-by")
      .help("Column to group by")
      .required();
  summarize_cmd.add_argument("--max")
      .help("Column to compute the maximum of")
      .required();
  summarize_cmd.add_argument("--show")
      .help("Additional columns to include from the winning row (repeatable)")
      .default_value(std::vector<std::string>{})
      .append();
  AddCsvInputArguments(summarize_cmd);
  AddExactArgument(summarize_cmd);;

  argparse::ArgumentParser timeseries_cmd("timeseries");
  timeseries_cmd.add_description("Extract an x/y time series with optional grouping and reduction");
  timeseries_cmd.add_argument("--x")
      .help("Column to use as the x axis")
      .required();
  timeseries_cmd.add_argument("--y")
      .help("Column to use as the y axis (must be numeric)")
      .required();
  timeseries_cmd.add_argument("--series")
      .help("Column to group series by")
      .default_value(std::string{});
  timeseries_cmd.add_argument("--reduce")
      .help("How to reduce duplicate (series, x) pairs: max|min|sum|avg|last")
      .default_value(std::string{"last"});
  timeseries_cmd.add_argument("--format")
      .help("Output format: csv|markdown")
      .default_value(std::string{"csv"});
  AddCsvInputArguments(timeseries_cmd);
  AddExactArgument(timeseries_cmd);;

  program.add_subparser(derive_cmd);
  program.add_subparser(filter_cmd);
    program.add_subparser(head_cmd);
  program.add_subparser(summarize_cmd);
  program.add_subparser(timeseries_cmd);

  argparse::ArgumentParser sql_cmd("sql");
    sql_cmd.add_description("SQLite-backed workflows (load/query)");

    sql_cmd.add_argument("mode")
      .help("SQL mode: load|query")
      .default_value(std::string{"load"})
      .nargs(argparse::nargs_pattern::optional);
    AddCsvInputArguments(sql_cmd);
    sql_cmd.add_argument("--dest")
      .help("Destination .db file path (default: input filename with .db extension)")
      .default_value(std::string{});
    sql_cmd.add_argument("--table")
      .help("Table name to create inside the database")
      .default_value(std::string{"data"});
    sql_cmd.add_argument("--csv")
      .help("Input CSV file path, or '-' for stdin")
      .default_value(std::string{});
      sql_cmd.add_argument("--db")
        .help("Input SQLite database path")
        .default_value(std::string{});
    sql_cmd.add_argument("--sql")
      .help("SQL query text to execute")
      .default_value(std::string{""});
      sql_cmd.add_argument("--no-int-division-warning")
        .help("Suppress warning for likely integer division in SQL expressions")
        .default_value(false)
        .implicit_value(true);

  program.add_subparser(sql_cmd);

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    std::cerr << program;
    return 1;
  }

  if (!program.is_subcommand_used("derive") && !program.is_subcommand_used("filter") &&
      !program.is_subcommand_used("head") && !program.is_subcommand_used("summarize") &&
      !program.is_subcommand_used("timeseries") && !program.is_subcommand_used("sql")) {
    std::cerr << program;
    return 1;
  }

  std::unique_ptr<std::ifstream> input_file;
  ThroughputStats stats;
  const auto start = std::chrono::steady_clock::now();

  if (program.is_subcommand_used("derive")) {
    Logger logger(derive_cmd.get<bool>("--verbose"));
    const std::string input_name = derive_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunDerive(
        derive_cmd.get<std::string>("assignment"),
        *input, std::cout, BuildTransformOptions(derive_cmd, input, input_name),
        BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("filter")) {
    Logger logger(filter_cmd.get<bool>("--verbose"));
    const std::string input_name = filter_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunFilter(
        filter_cmd.get<std::string>("expression"),
        *input, std::cout, BuildTransformOptions(filter_cmd, input, input_name),
        BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("summarize")) {
    Logger logger(summarize_cmd.get<bool>("--verbose"));
    const std::string input_name = summarize_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunSummarize(
        summarize_cmd.get<std::string>("--group-by"),
        summarize_cmd.get<std::string>("--max"),
        summarize_cmd.get<std::vector<std::string>>("--show"),
        *input, std::cout, BuildTransformOptions(summarize_cmd, input, input_name),
        BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("timeseries")) {
    Logger logger(timeseries_cmd.get<bool>("--verbose"));
    const std::string input_name = timeseries_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunTimeseries(
        timeseries_cmd.get<std::string>("--x"),
        timeseries_cmd.get<std::string>("--y"),
        timeseries_cmd.get<std::string>("--series"),
        timeseries_cmd.get<std::string>("--reduce"),
        timeseries_cmd.get<std::string>("--format"),
        *input, std::cout, BuildTransformOptions(timeseries_cmd, input, input_name),
        BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("sql")) {
    Logger logger(sql_cmd.get<bool>("--verbose"));
    const std::string mode = sql_cmd.get<std::string>("mode");

    if (mode == "query") {
      const std::string csv_override = sql_cmd.get<std::string>("--csv");
      const std::string db_override = sql_cmd.get<std::string>("--db");
      const std::string query_text = sql_cmd.get<std::string>("--sql");
      const bool suppress_int_div_warning = sql_cmd.get<bool>("--no-int-division-warning");
      if (!csv_override.empty() && !db_override.empty()) {
        logger.Error("sql query: pass only one of --csv or --db.");
        return 1;
      }
      if (query_text.empty()) {
        logger.Error("sql query: --sql is required.");
        return 1;
      }

      std::string source_path;
      csvzall::pipeline::SqlQueryInputKind input_kind =
          csvzall::pipeline::SqlQueryInputKind::kUnknown;

      if (!csv_override.empty()) {
        source_path = csv_override;
        input_kind = csvzall::pipeline::SqlQueryInputKind::kCsv;
      } else if (!db_override.empty()) {
        source_path = db_override;
        input_kind = csvzall::pipeline::SqlQueryInputKind::kSqlite;
      } else {
        source_path = sql_cmd.get<std::string>("input");
        if (source_path == "-") {
          input_kind = csvzall::pipeline::SqlQueryInputKind::kCsv;
        } else {
          input_kind = csvzall::pipeline::DetectSqlQueryInputKind(source_path);
        }
      }

      if (input_kind == csvzall::pipeline::SqlQueryInputKind::kUnknown) {
        logger.Error("sql query: could not infer input type from path. Use --csv or --db.");
        return 1;
      }

      if (!suppress_int_div_warning && csvzall::pipeline::ShouldWarnIntegerDivision(query_text)) {
        logger.Info("sql query: possible integer division detected. "
                    "SQLite/PostgreSQL may truncate integer division; use 30.0 or x * 1.0 / y "
                    "for floating-point behavior. Pass --no-int-division-warning to suppress.");
      }

      if (input_kind == csvzall::pipeline::SqlQueryInputKind::kSqlite) {
        csvzall::pipeline::RunStats ps;
        const auto rc = csvzall::pipeline::RunSqlQueryDb(
            query_text, source_path, std::cout, BuildCallbacks(logger), ps);
        stats = FinishStats(ps, start);
        MaybePrintVerboseStats(stats, logger);
        return rc;
      }

      auto* input = ResolveInput(source_path, input_file, logger);
      if (!input) {
        logger.Error("Unable to open input file: " + source_path);
        return 1;
      }

      csvzall::pipeline::RunOptions options;
      options.single_threaded = sql_cmd.get<bool>("--single-threaded");
      options.input_is_stdin = (input == &std::cin);
      options.delimiter = ParseDelimiter(sql_cmd.get<std::string>("--delimiter"));
      options.input_path = source_path;

      csvzall::pipeline::RunStats ps;
      const auto rc = csvzall::pipeline::RunSqlQueryCsv(
          query_text,
          sql_cmd.get<std::string>("--table"),
          *input, std::cout, options, BuildCallbacks(logger), ps);
      stats = FinishStats(ps, start);
      MaybePrintVerboseStats(stats, logger);
      return rc;
    }

    if (mode == "load" || (mode != "query" && mode != "load")) {
      std::string input_name = sql_cmd.get<std::string>("input");
      if (mode != "load") {
        // Backward-compatible shorthand: `csvzall sql <input.csv>`.
        input_name = mode;
      }

      auto* input = ResolveInput(input_name, input_file, logger);
      if (!input) {
        logger.Error("Unable to open input file: " + input_name);
        return 1;
      }

      csvzall::pipeline::RunOptions options;
      options.single_threaded = sql_cmd.get<bool>("--single-threaded");
      options.input_is_stdin = (input == &std::cin);
      options.delimiter = ParseDelimiter(sql_cmd.get<std::string>("--delimiter"));
      options.input_path = input_name;

      csvzall::pipeline::RunStats ps;
      const auto rc = csvzall::pipeline::RunSqlExport(
          input_name, sql_cmd.get<std::string>("--dest"),
          sql_cmd.get<std::string>("--table"),
          *input, options, BuildCallbacks(logger), ps);
      stats.rows_processed = ps.rows_processed;
      stats.elapsed = std::chrono::steady_clock::now() - start;
      MaybePrintVerboseStats(stats, logger);
      return rc;
    }

    logger.Error("sql: mode must be 'load' or 'query'.");
    return 1;
  }

  Logger logger(head_cmd.get<bool>("--verbose"));
  const int requested_rows = head_cmd.get<int>("--rows");
  if (requested_rows < 0) {
    logger.Error("--rows must be greater than or equal to 0.");
    return 1;
  }
  const std::string input_name = head_cmd.get<std::string>("input");
  auto* input = ResolveInput(input_name, input_file, logger);
  if (!input) {
    logger.Error("Unable to open input file: " + input_name);
    return 1;
  }
  const csvzall::pipeline::RunOptions head_options{
    false,
    input == &std::cin,
    false,
    ParseDelimiter(head_cmd.get<std::string>("--delimiter"))};
  csvzall::head::Result head_result;
  const auto rc = csvzall::head::Run(
      static_cast<std::size_t>(requested_rows), *input, std::cout,
      head_options, logger, head_result);
  stats.rows_processed = head_result.rows_processed;
  stats.bytes_processed = head_result.bytes_processed;
  stats.elapsed = std::chrono::steady_clock::now() - start;
  MaybePrintVerboseStats(stats, logger);
  return rc;
}

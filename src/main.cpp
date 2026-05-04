#include <argparse/argparse.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/progress_spinner.hpp>
#include "credentials.hpp"
#include "head.hpp"
#include "transform_pipeline.hpp"
#include "util.hpp"

#ifdef CSVZALL_HAVE_POSTGRESQL
#include "pipeline/postgres/postgres_connection.hpp"
#include "pipeline/postgres/row_loader.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

class Logger {
 public:
  explicit Logger(bool verbose, bool quiet = false) : verbose_(verbose), quiet_(quiet) {}

  void Info(const std::string& msg) const {
    if (quiet_) {
      return;
    }
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

  void StartProgress(const std::string& label, std::uint64_t total) const {
    if (quiet_) {
      return;
    }

    progress_total_ = total;
    if (total == 0) {
      progress_spinner_ = std::make_unique<indicators::ProgressSpinner>(
          indicators::option::Stream{std::cerr},
          indicators::option::PrefixText{label + " "},
          indicators::option::ShowPercentage{false},
          indicators::option::ShowElapsedTime{true},
          indicators::option::SpinnerStates{std::vector<std::string>{"|", "/", "-", "\\"}},
          indicators::option::ForegroundColor{indicators::Color::cyan});
      progress_spinner_->tick();
      return;
    }

    progress_bar_ = std::make_unique<indicators::ProgressBar>(
        indicators::option::Stream{std::cerr},
        indicators::option::BarWidth{40},
        indicators::option::Start{"["},
        indicators::option::Fill{"="},
        indicators::option::Lead{">"},
        indicators::option::Remainder{" "},
        indicators::option::End{"]"},
        indicators::option::PrefixText{label + " "},
        indicators::option::ShowPercentage{true},
        indicators::option::ShowElapsedTime{true},
        indicators::option::ShowRemainingTime{true},
        indicators::option::MaxProgress{100},
        indicators::option::ForegroundColor{indicators::Color::cyan});
    progress_bar_->set_progress(0);
  }

  void UpdateProgress(std::uint64_t current) const {
    if (progress_spinner_) {
      progress_spinner_->set_option(
          indicators::option::PostfixText{std::to_string(current) + " rows"});
      progress_spinner_->tick();
      return;
    }

    if (!progress_bar_ || progress_total_ == 0) {
      return;
    }

    const auto percent = static_cast<std::size_t>(
        std::min<std::uint64_t>((current * 100) / progress_total_, 100));
    progress_bar_->set_progress(percent);
  }

  void FinishProgress() const {
    if (progress_spinner_) {
      progress_spinner_->mark_as_completed();
      progress_spinner_.reset();
      progress_total_ = 0;
      return;
    }

    if (progress_bar_) {
      progress_bar_->set_progress(100);
      progress_bar_.reset();
      progress_total_ = 0;
    }
  }

  [[nodiscard]] bool is_verbose() const { return verbose_; }

 private:
  bool verbose_ = false;
  bool quiet_ = false;
  mutable std::unique_ptr<indicators::ProgressBar> progress_bar_;
  mutable std::unique_ptr<indicators::ProgressSpinner> progress_spinner_;
  mutable std::uint64_t progress_total_ = 0;
};

struct ThroughputStats {
  std::uint64_t rows_processed = 0;
  std::uint64_t bytes_processed = 0;
  std::chrono::steady_clock::duration elapsed{};
};

csvzall::CredentialTarget BuildCredentialTarget(
    const std::string& host,
    const int port,
    const std::string& database,
    const std::string& user) {
  return {host, port, database, user};
}

bool AskYesNo(const std::string& question, const bool default_yes = false) {
  std::cerr << question << (default_yes ? " [Y/n] " : " [y/N] ");
  std::string answer;
  std::getline(std::cin, answer);
  if (answer.empty()) {
    return default_yes;
  }
  return answer == "y" || answer == "Y" || answer == "yes" || answer == "YES";
}

bool ValidatePasswordSources(const argparse::ArgumentParser& cmd, Logger& logger) {
  if (cmd.is_used("--password") && cmd.is_used("--password-env")) {
    logger.Error("postgres: --password and --password-env are mutually exclusive. Pick one.");
    return false;
  }
  if (cmd.is_used("--password")) {
    logger.Error("⚠️  WARNING: Passing password via --password is insecure "
                 "(visible in shell history and process list). Use --save instead.");
  }
  return true;
}

std::optional<std::string> ReadEnvironmentVariable(const std::string& name) {
#ifdef _WIN32
  char* value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, name.c_str()) != 0 || value == nullptr) {
    return std::nullopt;
  }
  std::string result(value, length > 0 ? length - 1 : 0);
  free(value);
  return result;
#else
  const char* value = std::getenv(name.c_str());
  if (!value) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

#ifdef CSVZALL_HAVE_POSTGRESQL
bool ApplyPasswordSources(argparse::ArgumentParser& cmd,
                          Logger& logger,
                          csvzall::pipeline::postgres::ConnectionConfig& pg_config,
                          const csvzall::CredentialTarget& credential_target,
                          const bool allow_prompt) {
  if (cmd.is_used("--password")) {
    pg_config.password = cmd.get<std::string>("--password");
    return true;
  }

  const auto password_env = cmd.get<std::string>("--password-env");
  if (!password_env.empty()) {
    auto value = ReadEnvironmentVariable(password_env);
    if (!value) {
      logger.Error("postgres: environment variable '" + password_env + "' is not set");
      return false;
    }
    pg_config.password = *value;
    return true;
  }

  csvzall::CredentialManager credentials;
  std::string credential_error;
  if (auto stored = credentials.get_password(credential_target, &credential_error)) {
    pg_config.password = *stored;
    return true;
  }
  if (!credential_error.empty() && credentials.available()) {
    logger.Info("postgres: could not read stored credentials: " + credential_error);
  }

  if (!allow_prompt) {
    return true;
  }

  if (!csvzall::util::stdin_is_terminal()) {
    logger.Error("postgres: no password source found and stdin is not interactive");
    return false;
  }

  try {
    pg_config.password = csvzall::util::read_password("PostgreSQL password: ");
  } catch (const std::exception& ex) {
    logger.Error(std::string("postgres: ") + ex.what());
    return false;
  }
  return true;
}
#endif

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
  cmd.add_argument("--quiet")
      .help("Suppress informational logs")
      .default_value(false)
      .implicit_value(true);
  cmd.add_argument("-d", "--delimiter")
      .help("Input field delimiter: single char, 'tab', or '\\t' (default: auto-detect)")
      .default_value(std::string{""});
  cmd.add_argument("--zip-entry")
      .help("File entry to read from a .zip input archive")
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
    [&logger](const std::string& msg) { logger.Verbose(msg); },
    [&logger](const std::string& msg) { logger.Info(msg); },
    [&logger](const std::string& label, std::uint64_t total) { logger.StartProgress(label, total); },
    [&logger](std::uint64_t current) { logger.UpdateProgress(current); },
    [&logger]() { logger.FinishProgress(); }};
}

csvzall::pipeline::RunOptions BuildTransformOptions(
    const argparse::ArgumentParser& cmd, std::istream* input, const std::string& input_path) {
  csvzall::pipeline::RunOptions options;
  options.single_threaded = cmd.get<bool>("--single-threaded");
  options.input_is_stdin = (input == &std::cin);
  options.exact_column_matching = cmd.get<bool>("--exact");
  options.delimiter = ParseDelimiter(cmd.get<std::string>("--delimiter"));
  options.input_path = input_path;
  options.zip_entry = cmd.get<std::string>("--zip-entry");
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
    head_cmd.add_argument("--quiet")
      .help("Suppress informational logs")
      .default_value(false)
      .implicit_value(true);
    head_cmd.add_argument("-d", "--delimiter")
      .help("Input field delimiter: single char, 'tab', or '\\t' (default: auto-detect)")
      .default_value(std::string{""});
    head_cmd.add_argument("--zip-entry")
      .help("File entry to read from a .zip input archive")
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

  argparse::ArgumentParser forget_cmd("forget");
  forget_cmd.add_description("Forget stored csvzall credentials");
  forget_cmd.add_argument("connection")
      .help("Credential group to forget (currently: postgres)");
  forget_cmd.add_argument("--host")
      .help("PostgreSQL server host")
      .default_value(std::string{"localhost"});
  forget_cmd.add_argument("--port")
      .help("PostgreSQL server port")
      .default_value(5432)
      .scan<'i', int>();
  forget_cmd.add_argument("--dbname")
      .help("PostgreSQL database name for a specific credential")
      .default_value(std::string{""});
  forget_cmd.add_argument("--user")
      .help("PostgreSQL user name for a specific credential")
      .default_value(std::string{""});

  program.add_subparser(forget_cmd);

#ifdef CSVZALL_HAVE_POSTGRESQL
  argparse::ArgumentParser infer_cmd("infer");
  infer_cmd.add_description("Infer a PostgreSQL schema without creating a table or loading rows");
  AddCsvInputArguments(infer_cmd);
  AddExactArgument(infer_cmd);
  infer_cmd.add_argument("--table")
      .help("Table name to use when printing the inferred schema")
      .default_value(std::string{"inferred_table"});

  argparse::ArgumentParser postgres_cmd("postgres");
  postgres_cmd.add_description("Export CSV to PostgreSQL database with automatic schema inference");
  AddCsvInputArguments(postgres_cmd);
  AddExactArgument(postgres_cmd);
  postgres_cmd.add_argument("--table")
      .help("PostgreSQL table name to create or append to")
      .default_value(std::string{""});
  postgres_cmd.add_argument("--dbname")
      .help("PostgreSQL database name")
      .required();
  postgres_cmd.add_argument("--host")
      .help("PostgreSQL server host")
      .default_value(std::string{"localhost"});
  postgres_cmd.add_argument("--port")
      .help("PostgreSQL server port")
      .default_value(5432)
      .scan<'i', int>();
  postgres_cmd.add_argument("--user")
      .help("PostgreSQL username")
      .required();
  postgres_cmd.add_argument("--password")
      .help("PostgreSQL password (leave empty for prompt or .pgpass)")
      .default_value(std::string{""});
  postgres_cmd.add_argument("--password-env")
      .help("Read PostgreSQL password from an environment variable")
      .default_value(std::string{""});
  postgres_cmd.add_argument("--save")
      .help("Prompt for password, verify connection, and save credentials to the OS keychain")
      .default_value(false)
      .implicit_value(true);
  postgres_cmd.add_argument("--if-exists")
      .help("Action if table exists: error|drop|append")
      .default_value(std::string{"error"});
  postgres_cmd.add_argument("--copy-batch-rows")
      .help("Rows per PostgreSQL COPY producer batch")
      .default_value(10000)
      .scan<'i', int>();
  postgres_cmd.add_argument("--parallel-copy")
      .help("Number of PostgreSQL COPY workers; values above 1 do not preserve insertion order")
      .default_value(1)
      .implicit_value(0)
      .nargs(argparse::nargs_pattern::optional)
      .scan<'i', int>();

  program.add_subparser(infer_cmd);
  program.add_subparser(postgres_cmd);
#endif

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    std::cerr << program;
    return 1;
  }

  if (!program.is_subcommand_used("derive") && !program.is_subcommand_used("filter") &&
      !program.is_subcommand_used("head") && !program.is_subcommand_used("summarize") &&
      !program.is_subcommand_used("timeseries") && !program.is_subcommand_used("sql") &&
      !program.is_subcommand_used("forget")
#ifdef CSVZALL_HAVE_POSTGRESQL
      && !program.is_subcommand_used("infer")
      && !program.is_subcommand_used("postgres")
#endif
  ) {
    std::cerr << program;
    return 1;
  }

  std::unique_ptr<std::ifstream> input_file;
  ThroughputStats stats;
  const auto start = std::chrono::steady_clock::now();

  if (program.is_subcommand_used("forget")) {
    Logger logger(false, false);
    const auto connection = forget_cmd.get<std::string>("connection");
    if (connection != "postgres") {
      logger.Error("forget: unknown credential group '" + connection + "' (expected: postgres)");
      return 1;
    }

    csvzall::CredentialManager credentials;
    if (!credentials.available()) {
      logger.Error("forget postgres: " + credentials.unavailable_reason());
      return 1;
    }

    std::string error;
    const bool specific =
        forget_cmd.is_used("--host") || forget_cmd.is_used("--port") ||
        forget_cmd.is_used("--dbname") || forget_cmd.is_used("--user");
    if (specific) {
      const auto dbname = forget_cmd.get<std::string>("--dbname");
      const auto user = forget_cmd.get<std::string>("--user");
      if (dbname.empty() || user.empty()) {
        logger.Error("forget postgres: --dbname and --user are required when forgetting one credential");
        return 1;
      }
      const auto target = BuildCredentialTarget(
          forget_cmd.get<std::string>("--host"),
          forget_cmd.get<int>("--port"),
          dbname,
          user);
      if (!credentials.forget_password(target, &error)) {
        logger.Error("forget postgres: " + error);
        return 1;
      }
      logger.Info("forget postgres: removed stored credentials for " +
                  credentials.target_name(target));
      return 0;
    }

    const auto removed = credentials.forget_all_postgres(&error);
    if (!error.empty()) {
      logger.Error("forget postgres: " + error);
      return 1;
    }
    logger.Info("forget postgres: removed " + std::to_string(removed) +
                " stored credential(s)");
    return 0;
  }

  if (program.is_subcommand_used("derive")) {
    Logger logger(derive_cmd.get<bool>("--verbose"), derive_cmd.get<bool>("--quiet"));
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
    Logger logger(filter_cmd.get<bool>("--verbose"), filter_cmd.get<bool>("--quiet"));
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
    Logger logger(summarize_cmd.get<bool>("--verbose"), summarize_cmd.get<bool>("--quiet"));
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
    Logger logger(timeseries_cmd.get<bool>("--verbose"), timeseries_cmd.get<bool>("--quiet"));
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
    Logger logger(sql_cmd.get<bool>("--verbose"), sql_cmd.get<bool>("--quiet"));
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
      options.zip_entry = sql_cmd.get<std::string>("--zip-entry");

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
      options.zip_entry = sql_cmd.get<std::string>("--zip-entry");

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

#ifdef CSVZALL_HAVE_POSTGRESQL
  if (program.is_subcommand_used("infer")) {
    Logger logger(infer_cmd.get<bool>("--verbose"), infer_cmd.get<bool>("--quiet"));
    const std::string input_name = infer_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }

    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunPostgresInfer(
        *input, BuildTransformOptions(infer_cmd, input, input_name),
        BuildCallbacks(logger), ps,
        infer_cmd.get<std::string>("--table"));
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("postgres")) {
    Logger logger(postgres_cmd.get<bool>("--verbose"), postgres_cmd.get<bool>("--quiet"));
    if (!ValidatePasswordSources(postgres_cmd, logger)) {
      return 1;
    }

    // Build PostgreSQL configuration
    csvzall::pipeline::postgres::ConnectionConfig pg_config;
    pg_config.host = postgres_cmd.get<std::string>("--host");
    pg_config.port = postgres_cmd.get<int>("--port");
    pg_config.database = postgres_cmd.get<std::string>("--dbname");
    pg_config.user = postgres_cmd.get<std::string>("--user");

    const auto credential_target = BuildCredentialTarget(
        pg_config.host, pg_config.port, pg_config.database, pg_config.user);

    if (postgres_cmd.get<bool>("--save")) {
      if (postgres_cmd.is_used("--password") || postgres_cmd.is_used("--password-env")) {
        logger.Error("postgres --save: do not pass --password or --password-env; "
                     "csvzall will prompt securely");
        return 1;
      }

      csvzall::CredentialManager credentials;
      if (!credentials.available()) {
        logger.Error("postgres --save: " + credentials.unavailable_reason());
        return 1;
      }

      if (!csvzall::util::stdin_is_terminal()) {
        logger.Error("postgres --save: stdin is not interactive; cannot prompt for password");
        return 1;
      }

      try {
        pg_config.password = csvzall::util::read_password("PostgreSQL password: ");
      } catch (const std::exception& ex) {
        logger.Error(std::string("postgres --save: ") + ex.what());
        return 1;
      }

      bool connected = false;
      try {
        csvzall::pipeline::postgres::PostgresConnection test_conn(pg_config);
        connected = true;
      } catch (const std::exception& ex) {
        logger.Error(std::string("postgres --save: connection failed: ") + ex.what());
      }

      if (!connected &&
          !AskYesNo("Save these credentials anyway?", false)) {
        return 1;
      }

      std::string error;
      if (!credentials.save_password(credential_target, pg_config.password, &error)) {
        logger.Error("postgres --save: " + error);
        return 1;
      }

      logger.Info("postgres --save: saved credentials for " +
                  credentials.target_name(credential_target));
      return 0;
    }

    const std::string input_name = postgres_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }

    if (postgres_cmd.get<std::string>("--table").empty()) {
      logger.Error("postgres: --table is required unless --save is used");
      return 1;
    }

    if (!ApplyPasswordSources(postgres_cmd, logger, pg_config, credential_target, true)) {
      return 1;
    }

    auto options = BuildTransformOptions(postgres_cmd, input, input_name);
    const auto copy_batch_rows = postgres_cmd.get<int>("--copy-batch-rows");
    if (copy_batch_rows <= 0) {
      logger.Error("postgres: --copy-batch-rows must be greater than 0");
      return 1;
    }
    options.postgres_copy_batch_rows = static_cast<std::size_t>(copy_batch_rows);
    auto parallel_copy = postgres_cmd.get<int>("--parallel-copy");
    if (parallel_copy == 0 && postgres_cmd.is_used("--parallel-copy")) {
      const auto hardware_threads = std::thread::hardware_concurrency();
      const auto heuristic = hardware_threads > 1 ? hardware_threads / 2 : 1;
      parallel_copy = static_cast<int>(std::min<unsigned int>(heuristic, 8));
    }
    if (parallel_copy <= 0) {
      logger.Error("postgres: --parallel-copy must be greater than 0");
      return 1;
    }
    options.postgres_parallel_copy_workers = static_cast<std::size_t>(parallel_copy);
    const auto hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads > 0 &&
        options.postgres_parallel_copy_workers > static_cast<std::size_t>(hardware_threads)) {
      options.postgres_parallel_copy_workers = static_cast<std::size_t>(hardware_threads);
      logger.Info("postgres: capped --parallel-copy to hardware concurrency (" +
                  std::to_string(hardware_threads) + ")");
    }

    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunPostgresExport(
        *input, options, BuildCallbacks(logger), ps,
        pg_config,
        postgres_cmd.get<std::string>("--table"),
        postgres_cmd.get<std::string>("--if-exists"));
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }
#endif

  Logger logger(head_cmd.get<bool>("--verbose"), head_cmd.get<bool>("--quiet"));
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
  csvzall::pipeline::RunOptions head_options;
  head_options.input_is_stdin = (input == &std::cin);
  head_options.delimiter = ParseDelimiter(head_cmd.get<std::string>("--delimiter"));
  head_options.input_path = input_name;
  head_options.zip_entry = head_cmd.get<std::string>("--zip-entry");
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

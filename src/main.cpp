#include <argparse/argparse.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/progress_spinner.hpp>
#include "credentials.hpp"
#include "head.hpp"
#include "charts/chart_schema.hpp"
#include "pipeline/common/chart_spec.hpp"
#include "pipeline/common/markdown_calendar.hpp"
#include "transform_pipeline.hpp"
#include "util.hpp"

#ifdef CSVZALL_HAVE_POSTGRESQL
#include "postgres/postgres_connection.hpp"
#include "postgres/row_loader.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
                          csvzall::postgres::ConnectionConfig& pg_config,
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

std::optional<csvzall::pipeline::ViewModeSelection> ParseViewMode(const std::string& s) {
  if (s == "auto") return csvzall::pipeline::ViewModeSelection::Auto;
  if (s == "materialized") return csvzall::pipeline::ViewModeSelection::Paged;
  if (s == "paged") return csvzall::pipeline::ViewModeSelection::Paged;
  return std::nullopt;
}

csvzall::pipeline::common::ChartValueSpec ParseChartValueArg(const std::string& raw) {
  const auto pos = raw.find('=');
  if (pos == std::string::npos) {
    return {raw, raw, ""};
  }
  auto column = raw.substr(0, pos);
  auto label = raw.substr(pos + 1);
  if (label.empty()) {
    label = column;
  }
  return {std::move(column), std::move(label), ""};
}

std::string GetEnvString(const char* name) {
#ifdef _WIN32
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
    return {};
  }
  std::string result(value, size > 0 ? size - 1 : 0);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string(value);
#endif
}

std::string CalendarMonthName(unsigned month) {
  static constexpr std::array<std::string_view, 12> names{
      "January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December"};
  if (month < 1 || month > names.size()) {
    return std::to_string(month);
  }
  return std::string(names[month - 1]);
}

void ReplaceAll(std::string& text, const std::string& needle, const std::string& replacement) {
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

std::string FormatCalendarMonthHeader(std::string pattern, int year, unsigned month) {
  ReplaceAll(pattern, "{month-name}", CalendarMonthName(month));
  ReplaceAll(pattern, "{month}", std::to_string(month));
  ReplaceAll(pattern, "{year}", std::to_string(year));
  return pattern;
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
  argparse::ArgumentParser program("csvzall", "0.3.1");
  program.add_description("csvzall: high-performance CSV ETL and reporting CLI");
  program.add_epilog(R"(Intent groups:
  ETL/data: head, filter, derive, summarize, timeseries, sql, json, append, merge
  Inspect/view: view
  Rendering/report: calendar, heatmap, charts

SQL notes:
  sql query supports SQLite plus csvzall functions: REGEXP and regexp_like(value, pattern)

Workflow examples:
  csvzall json extract activities.json --map map.json > incoming.csv
  csvzall merge store.csv incoming.csv --key id --in-place
  csvzall sql query --csv store.csv --format markdown --sql "SELECT substr(date,1,7) AS month, COUNT(*) AS days FROM data GROUP BY month"
  csvzall view store.csv
  csvzall heatmap store.csv --date date --start 2026-01-01 --end 2026-12-31 --title "Activity"
  csvzall charts run --config .csvzall/charts.json
)");

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

  argparse::ArgumentParser max_cmd("max");
  max_cmd.add_description("Stream a CSV and print the maximum value in one column");
  AddCsvInputArguments(max_cmd);
  AddExactArgument(max_cmd);
  max_cmd.add_argument("--column")
      .help("Column to scan")
      .required();

  argparse::ArgumentParser min_cmd("min");
  min_cmd.add_description("Stream a CSV and print the minimum value in one column");
  AddCsvInputArguments(min_cmd);
  AddExactArgument(min_cmd);
  min_cmd.add_argument("--column")
      .help("Column to scan")
      .required();

  argparse::ArgumentParser append_cmd("append");
  append_cmd.add_description(
      "Append one CSV to another after validating exact header compatibility. "
      "Output is the combined CSV to stdout unless --in-place is set.");
  append_cmd.add_epilog(R"(Input shape:
  existing.csv and incoming.csv must have exactly matching headers.

Edge cases:
  append does not inspect keys or deduplicate rows; use merge for rerunnable
  keyed imports where existing rows should win on collisions.
  --in-place writes a temporary sibling file and replaces the original only
  after validation and output writing succeed.

Examples:
  csvzall append existing.csv incoming.csv > combined.csv
  csvzall append existing.csv incoming.csv --in-place
)");
  append_cmd.add_argument("existing")
      .help("Existing CSV file path");
  append_cmd.add_argument("incoming")
      .help("Incoming CSV file path");
  append_cmd.add_argument("--in-place")
      .help("Replace the existing CSV atomically after validation succeeds")
      .default_value(false)
      .implicit_value(true);

  argparse::ArgumentParser merge_cmd("merge");
  merge_cmd.add_description(
      "Merge incoming CSV rows into an existing CSV by key. Existing rows win on key collisions.");
  merge_cmd.add_epilog(R"(Input shape:
  existing.csv and incoming.csv must have exactly matching headers.
  --key names the required primary-key column.

Output shape:
  Without --in-place, merged CSV is written to stdout.
  With --in-place, the existing file is replaced only after validation succeeds.
  Diagnostics report added/skipped counts to stderr unless --quiet is set.

Edge cases:
  Duplicate keys within existing fail.
  Duplicate keys within incoming fail.
  Incoming rows whose key already exists are skipped.

Examples:
  csvzall merge activities.csv incoming.csv --key id > merged.csv
  csvzall merge activities.csv incoming.csv --key id --in-place
)");
  merge_cmd.add_argument("existing")
      .help("Existing CSV file path");
  merge_cmd.add_argument("incoming")
      .help("Incoming CSV file path");
  merge_cmd.add_argument("--key")
      .help("Primary key column used to skip already-imported rows")
      .required();
  merge_cmd.add_argument("--in-place")
      .help("Replace the existing CSV atomically after validation succeeds")
      .default_value(false)
      .implicit_value(true);
  merge_cmd.add_argument("--quiet")
      .help("Suppress added/skipped row diagnostics")
      .default_value(false)
      .implicit_value(true);

  argparse::ArgumentParser view_cmd("view");
  view_cmd.add_description(
      "Start a local browser table viewer for one plain CSV file.");
  view_cmd.add_epilog(R"(Input shape:
  A plain local CSV file path is required. stdin, gzip (.gz), and zip (.zip)
  inputs are intentionally not supported by the viewer in this pass.

Output shape:
  Prints the local viewer URL to stdout, then runs a local-only HTTP server on
  127.0.0.1 until interrupted.

  Behavior:
  API requests require a random session token.
  --startup-json prints {"url":"http://127.0.0.1:..."} for host integrations.
  The viewer is read-only unless --edit is provided.
  Pass --edit to enable explicit editable mode. Editable mode uses the same
  row-offset index as read-only viewing, keeps unsaved edits in an overlay,
  supports cell edits plus row/column insert/delete, exposes row deletion from
  the context menu, and saves by atomically rewriting the source CSV after
  checking that size and mtime did not change externally.
  --viewer-assets <dir> is a developer override that serves index.html,
  viewer.css, viewer.js, and viewer modules from disk on every request;
  embedded assets remain the default. CSVZALL_VIEWER_ASSETS provides the same
  override. AG Grid and Popright vendor files remain embedded.
  The viewer indexes CSV row byte offsets and serves rows through /api/rows for
  all file sizes. Paged mode disables global sort/search/filter until
  server-side implementations exist. The old materialized view mode spelling is
  accepted as a compatibility alias for paged mode.
  --once serves one successful request and exits; useful for scripts and tests.

Examples:
  csvzall view activities.csv
  csvzall view activities.csv --edit
  csvzall view activities.csv --no-open --port 43117
  csvzall view activities.csv --no-open --startup-json
  csvzall view activities.csv --view-mode paged

Related:
  Use head for terminal previews and sql query --format markdown for note-ready summaries.
)");
  view_cmd.add_argument("input")
      .help("Input CSV file path")
      .required();
  view_cmd.add_argument("--verbose")
      .help("Print diagnostic logs to stderr")
      .default_value(false)
      .implicit_value(true);
  view_cmd.add_argument("--quiet")
      .help("Suppress informational logs")
      .default_value(false)
      .implicit_value(true);
  view_cmd.add_argument("-d", "--delimiter")
      .help("Input field delimiter: single char, 'tab', or '\\t' (default: auto-detect)")
      .default_value(std::string{""});
  view_cmd.add_argument("--zip-entry")
      .help("File entry to read from a .zip input archive")
      .default_value(std::string{""});
  view_cmd.add_argument("--port")
      .help("Port to bind on 127.0.0.1. 0 chooses a random available port.")
      .default_value(0)
      .scan<'i', int>();
  view_cmd.add_argument("--view-mode")
      .help("Viewer row loading mode: auto or paged; materialized is a compatibility alias")
      .default_value(std::string{"auto"});
  view_cmd.add_argument("--materialize-threshold-mb")
      .help("Compatibility no-op; the viewer now uses row-offset paging for all files")
      .default_value(200)
      .scan<'i', int>();
  view_cmd.add_argument("--edit")
      .help("Enable explicit editable mode with cell edits, row/column insert/delete, reset, and atomic save")
      .default_value(false)
      .implicit_value(true);
  view_cmd.add_argument("--viewer-assets")
      .help("Developer mode: serve first-party viewer assets from this directory instead of embedded copies")
      .default_value(std::string{""});
  view_cmd.add_argument("--no-open")
      .help("Print the URL without opening a browser automatically")
      .default_value(false)
      .implicit_value(true);
  view_cmd.add_argument("--startup-json")
      .help("Print startup metadata as JSON: {\"url\":\"http://127.0.0.1:...\"}")
      .default_value(false)
      .implicit_value(true);
  view_cmd.add_argument("--once")
      .help("Serve one successful request and exit")
      .default_value(false)
      .implicit_value(true);

  argparse::ArgumentParser calendar_cmd("calendar");
  calendar_cmd.add_description(
      "Render fixed-shape date,content CSV as deterministic Markdown calendars. "
      "Output is Markdown to stdout.");
  calendar_cmd.add_epilog(R"(Input CSV schema:
  date,content
  2026-05-01,Done
  2026-05-02,
  2026-05-03,Skipped

Contract:
  date and content columns are required; additional columns are ignored.
  date must be ISO YYYY-MM-DD.
  Duplicate dates are rejected.
  Cells outside the requested date range are empty.
  Cells outside the current month are empty.
  Default layout is deterministic and Sunday-first.
  --month-header supports {year}, {month}, and {month-name}.

Examples:
  csvzall calendar habit-days.csv --start 2026-05-01 --end 2026-05-31
  csvzall calendar habit-days.csv --start 2026-05-01 --end 2026-06-30 --month-header "{month-name} {year}"
)");
  AddCsvInputArguments(calendar_cmd);
  AddExactArgument(calendar_cmd);
  calendar_cmd.add_argument("--start")
      .help("Inclusive start date, YYYY-MM-DD")
      .required();
  calendar_cmd.add_argument("--end")
      .help("Inclusive end date, YYYY-MM-DD")
      .required();
  calendar_cmd.add_argument("--month-header")
      .help("Month heading template; supports {year}, {month}, and {month-name}")
      .default_value(std::string{});

#ifdef CSVZALL_HAVE_SVGPLOT
  argparse::ArgumentParser heatmap_cmd("heatmap");
  heatmap_cmd.add_description(
      "Render date/value CSV as a self-contained SVG calendar heatmap. "
      "Output is SVG to stdout.");
  heatmap_cmd.add_epilog(R"(Input CSV shape:
  A date column is required. Dates may be YYYY-MM-DD, American M/D/YYYY or
  M-D-YYYY, or European D/M/YYYY or D-M-YYYY. Ambiguous numeric dates use a
  column-wide inference when an unambiguous clue exists, otherwise month-first.
  A numeric value column is optional. If omitted, each row contributes 1.
  Repeat --value to render a multi-value categorical heatmap.
  Multi-value labels can be written as --value column=Label.
  A label column is optional. Labels are included in SVG cell tooltips.
  Duplicate dates are aggregated by summing values or row counts.
  Rows outside --start/--end or --lookback are ignored by the chart renderer.
  --orientation months-vertical flips months to the vertical axis and weekdays
  to the horizontal axis for narrow embeds.

Examples:
  csvzall heatmap gym-attendance.csv --start 2025-05-15 --end 2026-05-15 --date date --title "Gym Attendance" > gym.svg
  csvzall heatmap gym-attendance.csv --lookback 1y --date date --output gym.svg
  csvzall heatmap gym-attendance.csv --lookback 365d --date date --output gym.svg
  csvzall heatmap gym-attendance.csv --start 2025-05-15 --end 2026-05-15 --date date --output gym.svg
  csvzall heatmap daily-counts.csv --start 2026-01-01 --end 2026-12-31 --date day --value count --label note > heatmap.svg
  csvzall heatmap training.csv --lookback 1y --date date --value gym=Gym --value bike=Bike --label note --output training.svg
  csvzall heatmap training.csv --lookback 1y --date date --orientation months-vertical --output training.svg
)");
  AddCsvInputArguments(heatmap_cmd);
  AddExactArgument(heatmap_cmd);
  heatmap_cmd.add_argument("--start")
      .help("Inclusive start date. Accepts YYYY-MM-DD, M/D/YYYY, M-D-YYYY, D/M/YYYY, or D-M-YYYY. Use with --end, or omit when using --lookback.")
      .default_value(std::string{});
  heatmap_cmd.add_argument("--end")
      .help("Inclusive end date. Accepts YYYY-MM-DD, M/D/YYYY, M-D-YYYY, D/M/YYYY, or D-M-YYYY. With --lookback, defaults to today's local date.")
      .default_value(std::string{});
  heatmap_cmd.add_argument("--lookback")
      .help("Rolling range ending at --end or today, such as 365d or 1y. Cannot be combined with --start.")
      .default_value(std::string{});
  heatmap_cmd.add_argument("--date")
      .help("Date column name (default: date)")
      .default_value(std::string{"date"});
  heatmap_cmd.add_argument("--value")
      .help("Optional numeric weight column. Repeat for multi-value heatmaps; use column=Label to name a value.")
      .default_value(std::vector<std::string>{})
      .append();
  heatmap_cmd.add_argument("--label")
      .help("Optional label column to include in SVG tooltips")
      .default_value(std::string{});
  heatmap_cmd.add_argument("--title")
      .help("Optional SVG title")
      .default_value(std::string{});
  heatmap_cmd.add_argument("--orientation")
      .help("Calendar layout: months-horizontal (default) or months-vertical")
      .default_value(std::string{"months-horizontal"});
  heatmap_cmd.add_argument("--output")
      .help("Optional SVG output file path. If omitted, SVG is written to stdout.")
      .default_value(std::string{});

  argparse::ArgumentParser charts_cmd("charts");
  charts_cmd.add_description(
      "Run configured CSV-to-chart rendering from .csvzall/charts.json.");
  charts_cmd.add_epilog(R"(charts quick start:
  Runs chart artifacts configured in .csvzall/charts.json by default.
  Use --config <path> to choose another config file.

Minimal config:
  {"charts":[{"id":"gym","type":"heatmap","input":"gym.csv","output":"charts/gym.svg","options":{"date":"date","lookback":"1y"}}]}

Supported chart types:
  heatmap, bar, line, markdown-table

Use `csvzall charts schema` for the full config reference, including option
value grammar such as heatmap lookback values.

Examples:
  csvzall charts run
  csvzall charts run gym-attendance-heatmap
  csvzall charts run --config .csvzall/charts.json
  csvzall charts run --validate --config .csvzall/charts.json
  csvzall charts schema

Related:
  Use heatmap for direct one-off SVG rendering to stdout or --output.
)");
  charts_cmd.add_argument("mode")
      .help("Charts mode: run or schema")
      .metavar("[run|schema]")
      .default_value(std::string{"run"})
      .nargs(argparse::nargs_pattern::optional);
  charts_cmd.add_argument("id")
      .help("Optional chart id for run mode")
      .metavar("[id]")
      .default_value(std::string{})
      .nargs(argparse::nargs_pattern::optional);
  charts_cmd.add_argument("--config")
      .help("Chart config path (default: .csvzall/charts.json from current directory)")
      .default_value(std::string{});
  charts_cmd.add_argument("--validate")
      .help("Validate matching chart configs without writing output files")
      .default_value(false)
      .implicit_value(true);
  charts_cmd.add_argument("--verbose")
      .help("Print diagnostic logs to stderr")
      .default_value(false)
      .implicit_value(true);
  charts_cmd.add_argument("--quiet")
      .help("Suppress informational logs")
      .default_value(false)
      .implicit_value(true);
#endif

  argparse::ArgumentParser json_cmd("json");
  json_cmd.add_description(
      "JSON workflows. Use `csvzall json extract <input.json> --map <mapping.json>` "
      "to extract mapped JSON rows as CSV to stdout.");
  json_cmd.add_epilog(R"(json extract contract:
  Mapping file is required via --map.
  JSON input path is required.
  Output is CSV to stdout; diagnostics go to stderr.
  rows is required and selects the row objects.
  columns is required and maps output column names to paths.
  Column paths are evaluated relative to each selected row.
  Optional, missing, or null fields become empty cells.
  Nested API payload paths such as $.extra_data.content are supported.
  Scalar values become CSV cells.
  Objects or arrays selected as column values fail by default.

Supported JSONPath subset:
  $
  .field
  ["field name"]
  ['field name']
  [0]
  [*]

Unsupported JSONPath features:
  recursive descent, filters, slices, unions, scripts, regex JSONPath queries

Mapping example:
  {
    "rows": "$.results[*]",
    "columns": {
      "event_id": "$.id",
      "event_type": "$.event_type",
      "completed_at": "$.event_date",
      "content": "$.extra_data.content",
      "due_date": "$.extra_data.due_date",
      "was_overdue": "$.extra_data.was_overdue"
    }
  }

Pipeline example:
  csvzall json extract todoist-activities.json --map todoist-map.json > todoist-activities.csv
  csvzall merge activities.csv todoist-activities.csv --key event_id --in-place
  csvzall sql query --csv activities.csv --format markdown --sql "SELECT content, COUNT(*) FROM data GROUP BY content"
)");
  json_cmd.add_argument("mode")
      .help("JSON mode: extract")
      .default_value(std::string{"extract"})
      .nargs(argparse::nargs_pattern::optional);
  json_cmd.add_argument("input")
      .help("Input JSON file path")
      .default_value(std::string{""})
      .nargs(argparse::nargs_pattern::optional);
  json_cmd.add_argument("--map")
      .help("Mapping JSON file with required rows and columns fields")
      .default_value(std::string{});

  program.add_subparser(derive_cmd);
  program.add_subparser(filter_cmd);
    program.add_subparser(head_cmd);
  program.add_subparser(summarize_cmd);
  program.add_subparser(timeseries_cmd);
  program.add_subparser(max_cmd);
  program.add_subparser(min_cmd);
  program.add_subparser(append_cmd);
  program.add_subparser(merge_cmd);
  program.add_subparser(view_cmd);
  program.add_subparser(calendar_cmd);
#ifdef CSVZALL_HAVE_SVGPLOT
  program.add_subparser(heatmap_cmd);
  program.add_subparser(charts_cmd);
#endif
  program.add_subparser(json_cmd);

  argparse::ArgumentParser sql_cmd("sql");
    sql_cmd.add_description(
        "SQLite-backed workflows. Use `csvzall sql query --csv <path> --sql <query>` "
        "to query CSV directly with SQL aggregation.");
    sql_cmd.add_epilog(R"SQL(sql query contract:
  --csv <path> reads CSV directly.
  --csv - reads CSV from stdin.
  --db <path> queries an existing SQLite database.
  --sql <query> is required for query mode.
  --format csv|markdown controls query output; default is csv.
  CSV input is loaded into a SQLite table named by --table, default data.
  CSV-backed tables infer SQLite affinity before load: numeric-only columns use
  NUMERIC; text-like columns and very large integer IDs use TEXT for exact
  lexical projection.

Useful SQL features:
  WHERE, GROUP BY, COUNT, COUNT(DISTINCT ...), ORDER BY, substr, CASE,
  REGEXP, and regexp_like.

Examples:
  csvzall sql query --csv gym-attendance.csv --sql "SELECT substr(date, 1, 7) AS month, COUNT(*) AS attendance_days FROM data GROUP BY month ORDER BY month"
  csvzall sql query --csv gym-events.csv --sql "SELECT date, COUNT(*) AS events FROM data GROUP BY date HAVING COUNT(*) > 1 ORDER BY date"
  csvzall sql query --csv gym-events.csv --sql "SELECT COUNT(DISTINCT date) AS attendance_days FROM data"
  csvzall sql query --csv gym-attendance.csv --format markdown --sql "SELECT substr(date, 1, 7) AS month, COUNT(*) AS days FROM data GROUP BY month ORDER BY month"
  csvzall sql query --csv gym-events.csv --sql "SELECT content FROM data WHERE regexp_like(content, '(?i)\b(gym|workout)\b')"
)SQL");

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
      sql_cmd.add_argument("--format")
        .help("Output format for query mode: csv|markdown")
        .default_value(std::string{"csv"});
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
      !program.is_subcommand_used("timeseries") && !program.is_subcommand_used("max") &&
      !program.is_subcommand_used("min") && !program.is_subcommand_used("append") &&
      !program.is_subcommand_used("merge") && !program.is_subcommand_used("view") &&
      !program.is_subcommand_used("calendar") &&
#ifdef CSVZALL_HAVE_SVGPLOT
      !program.is_subcommand_used("heatmap") &&
      !program.is_subcommand_used("charts") &&
#endif
      !program.is_subcommand_used("json") &&
      !program.is_subcommand_used("sql") &&
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

  if (program.is_subcommand_used("max")) {
    Logger logger(max_cmd.get<bool>("--verbose"), max_cmd.get<bool>("--quiet"));
    const std::string input_name = max_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunMax(
        max_cmd.get<std::string>("--column"),
        *input, std::cout, BuildTransformOptions(max_cmd, input, input_name),
        BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("min")) {
    Logger logger(min_cmd.get<bool>("--verbose"), min_cmd.get<bool>("--quiet"));
    const std::string input_name = min_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunMin(
        min_cmd.get<std::string>("--column"),
        *input, std::cout, BuildTransformOptions(min_cmd, input, input_name),
        BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("append")) {
    Logger logger(false, false);
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunAppend(
        append_cmd.get<std::string>("existing"),
        append_cmd.get<std::string>("incoming"),
        append_cmd.get<bool>("--in-place"),
        std::cout, BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    return rc;
  }

  if (program.is_subcommand_used("merge")) {
    Logger logger(false, merge_cmd.get<bool>("--quiet"));
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunMerge(
        merge_cmd.get<std::string>("existing"),
        merge_cmd.get<std::string>("incoming"),
        merge_cmd.get<std::string>("--key"),
        merge_cmd.get<bool>("--in-place"),
        std::cout, BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    return rc;
  }

  if (program.is_subcommand_used("view")) {
    Logger logger(view_cmd.get<bool>("--verbose"), view_cmd.get<bool>("--quiet"));
    const std::string input_name = view_cmd.get<std::string>("input");
    const int requested_port = view_cmd.get<int>("--port");
    if (requested_port < 0 || requested_port > 65535) {
      logger.Error("view: --port must be between 0 and 65535.");
      return 1;
    }
    const auto view_mode = ParseViewMode(view_cmd.get<std::string>("--view-mode"));
    if (!view_mode) {
      logger.Error("view: --view-mode must be one of auto, paged, or materialized.");
      return 1;
    }
    const int materialize_threshold_mb = view_cmd.get<int>("--materialize-threshold-mb");
    if (materialize_threshold_mb < 0) {
      logger.Error("view: --materialize-threshold-mb must be non-negative.");
      return 1;
    }

    csvzall::pipeline::RunOptions options;
    options.delimiter = ParseDelimiter(view_cmd.get<std::string>("--delimiter"));
    options.input_path = input_name;
    options.zip_entry = view_cmd.get<std::string>("--zip-entry");
    options.view_mode = *view_mode;
    options.view_materialize_threshold_mb = static_cast<std::size_t>(materialize_threshold_mb);
    options.view_edit = view_cmd.get<bool>("--edit");
    options.view_asset_dir = view_cmd.get<std::string>("--viewer-assets");
    if (options.view_asset_dir.empty()) {
      options.view_asset_dir = GetEnvString("CSVZALL_VIEWER_ASSETS");
    }

    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunView(
        input_name,
        std::cout,
        options,
        BuildCallbacks(logger),
        ps,
        requested_port,
        !view_cmd.get<bool>("--no-open"),
        view_cmd.get<bool>("--once"),
        view_cmd.get<bool>("--startup-json"));
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("calendar")) {
    Logger logger(calendar_cmd.get<bool>("--verbose"), calendar_cmd.get<bool>("--quiet"));
    const std::string input_name = calendar_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }

    csvzall::pipeline::common::MarkdownCalendarOptions calendar_options;
    calendar_options.start_date = calendar_cmd.get<std::string>("--start");
    calendar_options.end_date = calendar_cmd.get<std::string>("--end");
    const auto header_template = calendar_cmd.get<std::string>("--month-header");
    if (!header_template.empty()) {
      calendar_options.month_header =
          [header_template](int year, unsigned month) {
            return FormatCalendarMonthHeader(header_template, year, month);
          };
    }

    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::common::RenderMarkdownCalendarCsv(
        *input, std::cout, BuildTransformOptions(calendar_cmd, input, input_name),
        BuildCallbacks(logger), ps, calendar_options);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

#ifdef CSVZALL_HAVE_SVGPLOT
  if (program.is_subcommand_used("heatmap")) {
    Logger logger(heatmap_cmd.get<bool>("--verbose"), heatmap_cmd.get<bool>("--quiet"));
    const std::string input_name = heatmap_cmd.get<std::string>("input");
    auto* input = ResolveInput(input_name, input_file, logger);
    if (!input) {
      logger.Error("Unable to open input file: " + input_name);
      return 1;
    }

    csvzall::pipeline::common::HeatmapSpec heatmap_spec;
    heatmap_spec.date_column = heatmap_cmd.get<std::string>("--date");
    const auto value_args = heatmap_cmd.get<std::vector<std::string>>("--value");
    if (value_args.size() == 1 && value_args.front().find('=') == std::string::npos) {
      heatmap_spec.value_column = value_args.front();
    } else {
      heatmap_spec.values.reserve(value_args.size());
      for (const auto& value_arg : value_args) {
        if (!value_arg.empty()) {
          heatmap_spec.values.push_back(ParseChartValueArg(value_arg));
        }
      }
    }
    heatmap_spec.label_column = heatmap_cmd.get<std::string>("--label");
    heatmap_spec.start_date = heatmap_cmd.get<std::string>("--start");
    heatmap_spec.end_date = heatmap_cmd.get<std::string>("--end");
    heatmap_spec.lookback = heatmap_cmd.get<std::string>("--lookback");
    heatmap_spec.title = heatmap_cmd.get<std::string>("--title");
    heatmap_spec.orientation = heatmap_cmd.get<std::string>("--orientation");

    std::unique_ptr<std::ofstream> output_file;
    std::ostream* output = &std::cout;
    const auto output_name = heatmap_cmd.get<std::string>("--output");
    if (!output_name.empty()) {
      const std::filesystem::path output_path(output_name);
      const auto parent = output_path.parent_path();
      if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
          logger.Error("heatmap: unable to create output directory: " +
                       parent.string() + ": " + ec.message());
          return 1;
        }
      }
      output_file = std::make_unique<std::ofstream>(output_path, std::ios::binary | std::ios::trunc);
      if (!output_file->is_open()) {
        logger.Error("heatmap: unable to open output file: " + output_name);
        return 1;
      }
      output = output_file.get();
    }

    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunHeatmap(
        heatmap_spec,
        *input, *output, BuildTransformOptions(heatmap_cmd, input, input_name),
        BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    if (rc == 0 && output_file) {
      output_file->flush();
      if (!output_file->good()) {
        logger.Error("heatmap: failed to write output file: " + output_name);
        return 1;
      }
    }
    return rc;
  }

  if (program.is_subcommand_used("charts")) {
    Logger logger(charts_cmd.get<bool>("--verbose"), charts_cmd.get<bool>("--quiet"));
    const auto mode = charts_cmd.get<std::string>("mode");
    const auto chart_id = charts_cmd.get<std::string>("id");
    if (mode == "schema") {
      if (!chart_id.empty()) {
        logger.Error("charts schema: chart id is not accepted.");
        return 1;
      }
      std::cout << csvzall::charts::BuildChartSchemaReference();
      return 0;
    }
    if (mode != "run") {
      logger.Error("charts: mode must be 'run' or 'schema'.");
      return 1;
    }

    csvzall::pipeline::RunOptions options;
    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunCharts(
        charts_cmd.get<std::string>("--config"),
        chart_id,
        charts_cmd.get<bool>("--validate"),
        options,
        BuildCallbacks(logger),
        ps);
    stats = FinishStats(ps, start);
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }
#endif

  if (program.is_subcommand_used("json")) {
    Logger logger(false, false);
    const auto mode = json_cmd.get<std::string>("mode");
    if (mode != "extract") {
      logger.Error("json: mode must be 'extract'.");
      return 1;
    }

    csvzall::pipeline::RunStats ps;
    const auto rc = csvzall::pipeline::RunJsonExtract(
        json_cmd.get<std::string>("input"),
        json_cmd.get<std::string>("--map"),
        std::cout, BuildCallbacks(logger), ps);
    stats = FinishStats(ps, start);
    return rc;
  }

  if (program.is_subcommand_used("sql")) {
    Logger logger(sql_cmd.get<bool>("--verbose"), sql_cmd.get<bool>("--quiet"));
    const std::string mode = sql_cmd.get<std::string>("mode");

    if (mode == "query") {
      const std::string csv_override = sql_cmd.get<std::string>("--csv");
      const std::string db_override = sql_cmd.get<std::string>("--db");
      const std::string query_text = sql_cmd.get<std::string>("--sql");
      const std::string output_format = sql_cmd.get<std::string>("--format");
      const bool suppress_int_div_warning = sql_cmd.get<bool>("--no-int-division-warning");
      if (output_format != "csv" && output_format != "markdown") {
        logger.Error("sql query: --format must be csv or markdown.");
        return 1;
      }
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
            query_text, source_path, output_format, std::cout, BuildCallbacks(logger), ps);
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
          output_format,
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
    csvzall::postgres::ConnectionConfig pg_config;
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
        csvzall::postgres::PostgresConnection test_conn(pg_config);
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

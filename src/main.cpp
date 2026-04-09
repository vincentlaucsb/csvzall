#include <argparse/argparse.hpp>
#include "head.hpp"
#include "transform_pipeline.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
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

struct RunContext {
  std::istream* input = nullptr;
  std::ostream* output = nullptr;
  std::string input_name;
  bool single_threaded = false;
  bool verbose = false;
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

}  // namespace

int main(int argc, char** argv) {
  argparse::ArgumentParser program("csvzall", "0.1.0");
  program.add_description("csvzall: high-performance CSV transformation CLI");

  argparse::ArgumentParser derive_cmd("derive");
  derive_cmd.add_description("Add or overwrite a column using an expression");
  derive_cmd.add_argument("assignment").help("Format: NewCol = expression");
  derive_cmd.add_argument("input")
      .help("Input CSV file path, or '-' for stdin")
      .default_value(std::string{"-"})
      .nargs(argparse::nargs_pattern::optional);
  derive_cmd.add_argument("--single-threaded")
      .help("Disable csv-parser multithreading")
      .default_value(false)
      .implicit_value(true);
  derive_cmd.add_argument("--verbose")
      .help("Print throughput and diagnostic logs to stderr")
      .default_value(false)
      .implicit_value(true);
    derive_cmd.add_argument("--exact")
      .help("Use exact case-sensitive column matching")
      .default_value(false)
      .implicit_value(true);

  argparse::ArgumentParser filter_cmd("filter");
  filter_cmd.add_description("Keep rows where expression is truthy");
  filter_cmd.add_argument("expression").help("Filter expression evaluated per row");
  filter_cmd.add_argument("input")
      .help("Input CSV file path, or '-' for stdin")
      .default_value(std::string{"-"})
      .nargs(argparse::nargs_pattern::optional);
  filter_cmd.add_argument("--single-threaded")
      .help("Disable csv-parser multithreading")
      .default_value(false)
      .implicit_value(true);
  filter_cmd.add_argument("--verbose")
      .help("Print throughput and diagnostic logs to stderr")
      .default_value(false)
      .implicit_value(true);
    filter_cmd.add_argument("--exact")
      .help("Use exact case-sensitive column matching")
      .default_value(false)
      .implicit_value(true);

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
  summarize_cmd.add_argument("input")
      .help("Input CSV file path, or '-' for stdin")
      .default_value(std::string{"-"})
      .nargs(argparse::nargs_pattern::optional);
  summarize_cmd.add_argument("--single-threaded")
      .help("Disable csv-parser multithreading")
      .default_value(false)
      .implicit_value(true);
  summarize_cmd.add_argument("--verbose")
      .help("Print throughput and diagnostic logs to stderr")
      .default_value(false)
      .implicit_value(true);
    summarize_cmd.add_argument("--exact")
      .help("Use exact case-sensitive column matching")
      .default_value(false)
      .implicit_value(true);

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
  timeseries_cmd.add_argument("input")
      .help("Input CSV file path, or '-' for stdin")
      .default_value(std::string{"-"})
      .nargs(argparse::nargs_pattern::optional);
  timeseries_cmd.add_argument("--single-threaded")
      .help("Disable csv-parser multithreading")
      .default_value(false)
      .implicit_value(true);
  timeseries_cmd.add_argument("--verbose")
      .help("Print throughput and diagnostic logs to stderr")
      .default_value(false)
      .implicit_value(true);
  timeseries_cmd.add_argument("--exact")
      .help("Use exact case-sensitive column matching")
      .default_value(false)
      .implicit_value(true);

  program.add_subparser(derive_cmd);
  program.add_subparser(filter_cmd);
    program.add_subparser(head_cmd);
  program.add_subparser(summarize_cmd);
  program.add_subparser(timeseries_cmd);

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    std::cerr << program;
    return 1;
  }

  if (!program.is_subcommand_used("derive") && !program.is_subcommand_used("filter") &&
      !program.is_subcommand_used("head") && !program.is_subcommand_used("summarize") &&
      !program.is_subcommand_used("timeseries")) {
    std::cerr << program;
    return 1;
  }

  std::unique_ptr<std::ifstream> input_file;
  ThroughputStats stats;
  const auto start = std::chrono::steady_clock::now();

  if (program.is_subcommand_used("derive")) {
    Logger logger(derive_cmd.get<bool>("--verbose"));
    RunContext ctx;
    ctx.input_name = derive_cmd.get<std::string>("input");
    ctx.single_threaded = derive_cmd.get<bool>("--single-threaded");
    ctx.verbose = derive_cmd.get<bool>("--verbose");
    ctx.input = ResolveInput(ctx.input_name, input_file, logger);
    ctx.output = &std::cout;

    if (ctx.input == nullptr) {
      logger.Error("Unable to open input file: " + ctx.input_name);
      return 1;
    }

    csvzall::pipeline::RunStats pipeline_stats;
    const csvzall::pipeline::LoggerCallbacks callbacks{
      [&](const std::string& msg) { logger.Error(msg); },
      [&](const std::string& msg) { logger.Verbose(msg); }};
    const csvzall::pipeline::RunOptions options{
      ctx.single_threaded,
      ctx.input == &std::cin,
      derive_cmd.get<bool>("--exact")};

    const auto rc = csvzall::pipeline::RunDerive(derive_cmd.get<std::string>("assignment"),
                           *ctx.input, *ctx.output, options, callbacks,
                           pipeline_stats);
    stats.rows_processed = pipeline_stats.rows_processed;
    stats.bytes_processed = pipeline_stats.bytes_processed;
    stats.elapsed = std::chrono::steady_clock::now() - start;
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("filter")) {
    Logger logger(filter_cmd.get<bool>("--verbose"));
    RunContext ctx;
    ctx.input_name = filter_cmd.get<std::string>("input");
    ctx.single_threaded = filter_cmd.get<bool>("--single-threaded");
    ctx.verbose = filter_cmd.get<bool>("--verbose");
    ctx.input = ResolveInput(ctx.input_name, input_file, logger);
    ctx.output = &std::cout;

    if (ctx.input == nullptr) {
      logger.Error("Unable to open input file: " + ctx.input_name);
      return 1;
    }

    csvzall::pipeline::RunStats pipeline_stats;
    const csvzall::pipeline::LoggerCallbacks callbacks{
      [&](const std::string& msg) { logger.Error(msg); },
      [&](const std::string& msg) { logger.Verbose(msg); }};
    const csvzall::pipeline::RunOptions options{
      ctx.single_threaded,
      ctx.input == &std::cin,
      filter_cmd.get<bool>("--exact")};

    const auto rc = csvzall::pipeline::RunFilter(filter_cmd.get<std::string>("expression"),
                           *ctx.input, *ctx.output, options, callbacks,
                           pipeline_stats);
    stats.rows_processed = pipeline_stats.rows_processed;
    stats.bytes_processed = pipeline_stats.bytes_processed;
    stats.elapsed = std::chrono::steady_clock::now() - start;
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("summarize")) {
    Logger logger(summarize_cmd.get<bool>("--verbose"));
    RunContext ctx;
    ctx.input_name = summarize_cmd.get<std::string>("input");
    ctx.single_threaded = summarize_cmd.get<bool>("--single-threaded");
    ctx.verbose = summarize_cmd.get<bool>("--verbose");
    ctx.input = ResolveInput(ctx.input_name, input_file, logger);
    ctx.output = &std::cout;

    if (ctx.input == nullptr) {
      logger.Error("Unable to open input file: " + ctx.input_name);
      return 1;
    }

    csvzall::pipeline::RunStats pipeline_stats;
    const csvzall::pipeline::LoggerCallbacks callbacks{
      [&](const std::string& msg) { logger.Error(msg); },
      [&](const std::string& msg) { logger.Verbose(msg); }};
    const csvzall::pipeline::RunOptions options{
      ctx.single_threaded,
      ctx.input == &std::cin,
      summarize_cmd.get<bool>("--exact")};

    const auto rc = csvzall::pipeline::RunSummarize(
        summarize_cmd.get<std::string>("--group-by"),
        summarize_cmd.get<std::string>("--max"),
        summarize_cmd.get<std::vector<std::string>>("--show"),
        *ctx.input, *ctx.output, options, callbacks, pipeline_stats);
    stats.rows_processed = pipeline_stats.rows_processed;
    stats.bytes_processed = pipeline_stats.bytes_processed;
    stats.elapsed = std::chrono::steady_clock::now() - start;
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  if (program.is_subcommand_used("timeseries")) {
    Logger logger(timeseries_cmd.get<bool>("--verbose"));
    RunContext ctx;
    ctx.input_name = timeseries_cmd.get<std::string>("input");
    ctx.single_threaded = timeseries_cmd.get<bool>("--single-threaded");
    ctx.verbose = timeseries_cmd.get<bool>("--verbose");
    ctx.input = ResolveInput(ctx.input_name, input_file, logger);
    ctx.output = &std::cout;

    if (ctx.input == nullptr) {
      logger.Error("Unable to open input file: " + ctx.input_name);
      return 1;
    }

    csvzall::pipeline::RunStats pipeline_stats;
    const csvzall::pipeline::LoggerCallbacks callbacks{
      [&](const std::string& msg) { logger.Error(msg); },
      [&](const std::string& msg) { logger.Verbose(msg); }};
    const csvzall::pipeline::RunOptions options{
      ctx.single_threaded,
      ctx.input == &std::cin,
      timeseries_cmd.get<bool>("--exact")};

    const auto rc = csvzall::pipeline::RunTimeseries(
        timeseries_cmd.get<std::string>("--x"),
        timeseries_cmd.get<std::string>("--y"),
        timeseries_cmd.get<std::string>("--series"),
        timeseries_cmd.get<std::string>("--reduce"),
        timeseries_cmd.get<std::string>("--format"),
        *ctx.input, *ctx.output, options, callbacks, pipeline_stats);
    stats.rows_processed = pipeline_stats.rows_processed;
    stats.bytes_processed = pipeline_stats.bytes_processed;
    stats.elapsed = std::chrono::steady_clock::now() - start;
    MaybePrintVerboseStats(stats, logger);
    return rc;
  }

  Logger logger(head_cmd.get<bool>("--verbose"));
  const int requested_rows = head_cmd.get<int>("--rows");
  if (requested_rows < 0) {
    logger.Error("--rows must be greater than or equal to 0.");
    return 1;
  }

  RunContext ctx;
  ctx.input_name = head_cmd.get<std::string>("input");
  ctx.verbose = head_cmd.get<bool>("--verbose");
  ctx.input = ResolveInput(ctx.input_name, input_file, logger);
  ctx.output = &std::cout;

  if (ctx.input == nullptr) {
    logger.Error("Unable to open input file: " + ctx.input_name);
    return 1;
  }

  csvzall::head::Result head_result;
  const auto rc = csvzall::head::Run(static_cast<std::size_t>(requested_rows), *ctx.input,
                                     *ctx.output, ctx.input == &std::cin, logger, head_result);
  stats.rows_processed = head_result.rows_processed;
  stats.bytes_processed = head_result.bytes_processed;
  stats.elapsed = std::chrono::steady_clock::now() - start;
  MaybePrintVerboseStats(stats, logger);
  return rc;
}

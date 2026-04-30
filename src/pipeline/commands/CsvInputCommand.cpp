#include "commands.hpp"

#include "../common/gzip_stream.hpp"

#include <sstream>

namespace csvzall::pipeline::commands {

CsvInputCommand::CsvInputCommand(std::istream& input,
                                 const RunOptions& options,
                                 const LoggerCallbacks& logger, RunStats& stats)
    : input_(input), options_(options), logger_(logger), stats_(stats) {}

csv::CSVFormat CsvInputCommand::make_format() const {
  csv::CSVFormat format;
  if (options_.delimiter) {
    format.delimiter(*options_.delimiter);
  } else {
    format.delimiter({ ',', '|', '\t', ';', '^' });
  }
  format.quote('"').header_row(0);
  if (!options_.exact_column_matching) {
    format.column_names_policy(csv::ColumnNamePolicy::CASE_INSENSITIVE);
  }
  return format;
}

int CsvInputCommand::init_reader() {
  std::istream* parse_input = &input_;
  if (options_.input_is_stdin) {
    std::ostringstream raw;
    raw << input_.rdbuf();
    buffered_input_ = std::make_unique<std::istringstream>(raw.str());
    parse_input = buffered_input_.get();
  }

  auto format = make_format();
  try {
    if (!options_.input_is_stdin && !options_.input_path.empty() && options_.input_path != "-") {
      reader_ = std::make_unique<csv::CSVReader>(
          common::OpenCsvReader(options_.input_path, format));
    } else {
      reader_ = std::make_unique<csv::CSVReader>(*parse_input, format);
    }
  } catch (const std::exception& ex) {
    if (logger_.error) {
      logger_.error(ex.what());
    }
    return 1;
  }
  headers_ = reader_->get_col_names();

  if (headers_.empty()) {
    if (logger_.error) {
      logger_.error("Input appears to have no header row.");
    }
    return 1;
  }

  return 0;
}

int CsvInputCommand::reset_reader() {
  std::istream* parse_input = &input_;
  if (options_.input_is_stdin) {
    if (!buffered_input_) {
      if (logger_.error) {
        logger_.error("Unable to reset buffered stdin input.");
      }
      return 1;
    }
    buffered_input_->clear();
    buffered_input_->seekg(0);
    parse_input = buffered_input_.get();
  } else if (options_.input_path.empty() || options_.input_path == "-") {
    input_.clear();
    input_.seekg(0);
    if (!input_) {
      if (logger_.error) {
        logger_.error("Unable to rewind input stream for second pass.");
      }
      return 1;
    }
  }

  auto format = make_format();
  try {
    if (!options_.input_is_stdin && !options_.input_path.empty() && options_.input_path != "-") {
      reader_ = std::make_unique<csv::CSVReader>(
          common::OpenCsvReader(options_.input_path, format));
    } else {
      reader_ = std::make_unique<csv::CSVReader>(*parse_input, format);
    }
  } catch (const std::exception& ex) {
    if (logger_.error) {
      logger_.error(ex.what());
    }
    return 1;
  }
  const auto reset_headers = reader_->get_col_names();
  if (reset_headers != headers_) {
    if (logger_.error) {
      logger_.error("CSV header changed while resetting input stream.");
    }
    return 1;
  }

  return 0;
}

int CsvInputCommand::execute() {
  if (int rc = init_reader(); rc != 0) {
    return rc;
  }
  return run();
}

}  // namespace csvzall::pipeline::commands

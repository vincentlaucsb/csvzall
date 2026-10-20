#include "indexed_csv.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace csvzall::viewer {

namespace {

std::uint64_t GetFileSize(const std::string& input_path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(input_path, ec);
  if (ec) {
    throw std::runtime_error("unable to stat input file: " + input_path);
  }
  return static_cast<std::uint64_t>(size);
}

std::filesystem::file_time_type GetFileMtime(const std::string& input_path) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(input_path, ec);
  if (ec) {
    throw std::runtime_error("unable to stat input file mtime: " + input_path);
  }
  return mtime;
}

bool FileExists(const std::string& input_path) {
  std::error_code ec;
  return std::filesystem::exists(input_path, ec);
}

bool RecoverRenamedSource(std::string& input_path,
                          std::string& file_name,
                          const std::uint64_t source_size,
                          const std::filesystem::file_time_type source_mtime) {
  if (FileExists(input_path)) {
    return false;
  }

  const auto original = std::filesystem::path(input_path);
  auto parent = original.parent_path();
  if (parent.empty()) {
    parent = ".";
  }

  std::error_code ec;
  if (!std::filesystem::exists(parent, ec)) {
    throw std::runtime_error("source file no longer exists: " + input_path);
  }

  std::vector<std::filesystem::path> matches;
  std::filesystem::directory_iterator entries(parent, ec);
  if (ec) {
    throw std::runtime_error("unable to scan source directory after rename: " + parent.string());
  }

  for (const auto& entry : entries) {
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec) || entry_ec) {
      continue;
    }
    const auto size = entry.file_size(entry_ec);
    if (entry_ec || static_cast<std::uint64_t>(size) != source_size) {
      continue;
    }
    const auto mtime = entry.last_write_time(entry_ec);
    if (entry_ec || mtime != source_mtime) {
      continue;
    }
    matches.push_back(entry.path());
  }

  if (matches.empty()) {
    throw std::runtime_error("source file no longer exists: " + input_path);
  }
  if (matches.size() > 1) {
    throw std::runtime_error(
        "source file was renamed, but the new path is ambiguous; reopen the viewer");
  }

  input_path = matches.front().string();
  file_name = matches.front().filename().string();
  return true;
}

void RequireNonEmptyCsvFile(const std::uint64_t file_size) {
  if (file_size == 0) {
    throw std::runtime_error(
        "CSV file is empty. Add a header row first, for example: column");
  }
}

std::vector<std::string> ColumnNames(const std::vector<CsvColumnDescriptor>& columns) {
  std::vector<std::string> names;
  names.reserve(columns.size());
  for (const auto& column : columns) {
    names.push_back(column.name);
  }
  return names;
}

std::vector<std::string> SyntheticHeaders(const std::size_t count) {
  std::vector<std::string> names;
  for (std::size_t i = 0; i < count; ++i) {
    std::string name;
    for (auto n = i + 1; n != 0; n = (n - 1) / 26) {
      name.insert(name.begin(), static_cast<char>('A' + (n - 1) % 26));
    }
    names.push_back(std::move(name));
  }
  return names;
}

// csv-parser's DelimWriter fixes delimiter and quote at compile time. Viewer
// conversion needs runtime characters, while still escaping fields losslessly.
void WriteRow(std::ostream& output, const std::vector<std::string>& values,
              const csv::CSVFormat& format, const std::string& newline) {
  const auto delimiter = format.get_delim();
  const auto quote = format.get_quote_char();
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) output.put(delimiter);
    const auto& value = values[i];
    const bool needs_quote = value.find(delimiter) != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos ||
        (format.is_quoting_enabled() && value.find(quote) != std::string::npos) ||
        (values.size() == 1 && value.empty());
    if (needs_quote && !format.is_quoting_enabled()) {
      throw std::runtime_error("This value requires quoting; choose a quoting character in Options.");
    }
    if (needs_quote) output.put(quote);
    for (const auto c : value) {
      if (needs_quote && c == quote) output.put(quote);
      output.put(c);
    }
    if (needs_quote) output.put(quote);
  }
  output << newline;
}

std::string RecordNewline(const std::string& path, const csv::CSVFormat& format) {
  std::ifstream input(path, std::ios::binary);
  char prefix[3]{};
  input.read(prefix, 3);
  if (std::string(prefix, 3) != "\xEF\xBB\xBF") { input.clear(); input.seekg(0); }
  bool quoted = false, field_start = true;
  char c;
  while (input.get(c)) {
    if (format.is_quoting_enabled() && c == format.get_quote_char()) {
      if (quoted && input.peek() == c) { input.get(c); continue; }
      if (quoted || field_start) quoted = !quoted;
    } else if (!quoted) {
      if (c == '\r') return input.peek() == '\n' ? "\r\n" : "\r";
      if (c == '\n') return "\n";
    }
    field_start = !quoted && c == format.get_delim();
  }
  return "\n";
}

csv::CSVFormat WithDialect(csv::CSVFormat format, const std::string& delimiter, const std::string& quote) {
  const auto valid_char = [](const std::string& value) {
    return value.size() == 1 && (value[0] == '\t' || (value[0] >= ' ' && value[0] <= '~'));
  };
  if (!valid_char(delimiter) || (!quote.empty() && !valid_char(quote))) {
    throw std::runtime_error("Delimiter and quote must be single ASCII characters (or no quote).");
  }
  if (!quote.empty() && delimiter == quote) {
    throw std::runtime_error("Delimiter and quoting character must differ.");
  }
  // CSVFormat validates its stored quote even when quoting is disabled. A
  // temporary unused delimiter allows swapping delimiter and quote safely.
  format.delimiter('\x1f').quote(quote.empty() ? '\0' : quote[0]);
  format.delimiter(delimiter[0]).quote(!quote.empty());
  return format;
}

// Ragged samples can make the generic consistency heuristic pick an absent
// delimiter and collapse a delimited file into one column. Only reconsider a
// one-field result when the first record and another record provide evidence.
csv::CSVFormat InferViewerDelimiter(const std::string& path, csv::CSVFormat format) {
  const auto candidates = format.get_possible_delims();
  if (candidates.size() <= 1) return format;
  std::ifstream input(path, std::ios::binary);
  std::string sample(256 * 1024, '\0');
  input.read(sample.data(), static_cast<std::streamsize>(sample.size()));
  sample.resize(static_cast<std::size_t>(input.gcount()));
  try {
    std::stringstream guessed_stream(sample);
    auto probe_format = format;
    probe_format.no_header();
    csv::CSVReader guessed(guessed_stream, probe_format);
    bool has_records = false;
    for (const auto& row : guessed) {
      has_records = true;
      if (row.size() > 1) return format;
    }
    if (!has_records) return format;
    std::size_t best_width = 1;
    char best_delimiter = guessed.get_format().get_delim();
    for (const auto candidate : candidates) {
      auto candidate_format = probe_format;
      candidate_format.delimiter(candidate);
      std::stringstream stream(sample);
      csv::CSVReader reader(stream, candidate_format);
      auto row = reader.begin();
      if (row == reader.end() || row->size() <= best_width) continue;
      const auto first_width = row->size();
      for (int count = 0; ++row != reader.end() && count < 16; ++count) {
        if (row->size() > 1) { best_width = first_width; best_delimiter = candidate; break; }
      }
    }
    if (best_width > 1) format.delimiter(best_delimiter);
  } catch (const std::exception&) {
    // Leave ambiguous/truncated samples to the existing parser inference.
  }
  return format;
}

csv::CSVFormat InferViewerQuote(const std::string& path, csv::CSVFormat format) {
  if (!format.is_quoting_enabled() || format.get_quote_char() != '"') return format;
  // csv-parser accepts configurable quote characters. Resolve the common single
  // quote dialect from complete quoted fields in a bounded sample, retaining
  // double quotes when there is no evidence or the sample is ambiguous.
  std::ifstream input(path, std::ios::binary);
  std::string sample(256 * 1024, '\0');
  input.read(sample.data(), static_cast<std::streamsize>(sample.size()));
  sample.resize(static_cast<std::size_t>(input.gcount()));
  char delimiter;
  try {
    std::stringstream stream(sample);
    csv::CSVReader probe(stream, format);
    delimiter = probe.get_format().get_delim();
  } catch (const std::exception&) {
    return format;
  }
  const auto boundary = [delimiter](char c) {
    return c == '\r' || c == '\n' || c == delimiter;
  };
  const auto evidence = [&](char quote, bool require_structure) {
    std::size_t count = 0;
    bool start = true;
    for (std::size_t i = sample.starts_with("\xEF\xBB\xBF") ? 3 : 0; i < sample.size(); ++i) {
      const auto c = sample[i];
      if (start && c == quote) {
        bool structural = false;
        for (++i; i < sample.size(); ++i) {
          if (sample[i] != quote) { structural = structural || boundary(sample[i]); continue; }
          if (i + 1 < sample.size() && sample[i + 1] == quote) { ++i; continue; }
          if ((i + 1 < sample.size() ? boundary(sample[i + 1]) : input.eof()) && (!require_structure || structural)) ++count;
          break;
        }
      }
      start = boundary(c);
    }
    return count;
  };
  if (evidence('"', false) == 0 && evidence('\'', true) > 0 && delimiter != '\'') format.quote('\'');
  return format;
}

std::string GenerateSaveToken() {
  std::random_device rd;
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  static constexpr char hex[] = "0123456789abcdef";

  std::string token;
  token.reserve(32);
  for (int i = 0; i < 16; ++i) {
    const auto byte = dist(rd);
    token.push_back(hex[(byte >> 4) & 0x0f]);
    token.push_back(hex[byte & 0x0f]);
  }
  return token;
}

}  // namespace

CsvIndexedFile CsvIndexedFile::Open(
    const std::string& input_path,
    csv::CSVFormat format,
    std::function<void(const std::string&)> verbose) {
  if (input_path.empty() || input_path == "-") {
    throw std::runtime_error("stdin is not supported; pass a plain local CSV file path");
  }
  CsvIndexedFile indexed;
  indexed.input_path_ = input_path;
  indexed.file_name_ = std::filesystem::path(input_path).filename().string();

  RequireNonEmptyCsvFile(GetFileSize(input_path));
  indexed.format_ = InferViewerQuote(input_path, InferViewerDelimiter(input_path, std::move(format)));
  indexed.reload();

  if (verbose) {
    verbose("view: indexed " + std::to_string(indexed.index_.size()) +
            " row offset(s) from " + input_path);
  }
  return indexed;
}

const std::string& CsvIndexedFile::input_path() const {
  return input_path_;
}

const std::string& CsvIndexedFile::file_name() const {
  return file_name_;
}

const std::vector<std::string>& CsvIndexedFile::headers() const {
  return headers_;
}

std::uint64_t CsvIndexedFile::row_count() const {
  return static_cast<std::uint64_t>(rows_.size());
}

std::uint64_t CsvIndexedFile::file_size() const {
  return file_size_;
}

std::string CsvIndexedFile::delimiter() const { return std::string(1, output_format_.get_delim()); }
std::string CsvIndexedFile::quote() const {
  return output_format_.is_quoting_enabled() ? std::string(1, output_format_.get_quote_char()) : "";
}
bool CsvIndexedFile::has_headers() const { return has_headers_; }
const std::string& CsvIndexedFile::headers_disabled_reason() const { return headers_disabled_reason_; }
bool CsvIndexedFile::has_bom() const { return has_bom_; }

void CsvIndexedFile::relabel_columns(const std::vector<std::string>& names) {
  for (auto& row : rows_) {
    std::unordered_map<std::string, std::string> cells;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
      if (const auto it = row.cells.find(columns_[i].name); it != row.cells.end()) {
        cells[names[i]] = it->second;
      }
    }
    row.cells = std::move(cells);
  }
  for (std::size_t i = 0; i < columns_.size(); ++i) columns_[i].name = names[i];
  refresh_headers();
}

void CsvIndexedFile::set_csv_options(const std::string& delimiter,
                                    const std::string& quote, const bool has_headers) {
  if (has_headers && !headers_disabled_reason_.empty()) throw std::runtime_error(headers_disabled_reason_);
  auto next_format = WithDialect(output_format_, delimiter, quote);
  if (delimiter == this->delimiter() && quote == this->quote() && has_headers == has_headers_) return;

  if (has_headers != has_headers_) {
    if (!has_headers) {
      CsvLogicalRow first;
      for (const auto& column : columns_) first.cells[column.name] = column.name;
      rows_.insert(rows_.begin(), std::move(first));
      relabel_columns(SyntheticHeaders(columns_.size()));
    } else {
      if (rows_.empty()) throw std::runtime_error("There is no first row to use as headers.");
      const auto names = row_values_for_columns(rows_.front(), columns_);
      std::unordered_map<std::string, bool> seen;
      for (const auto& name : names) {
        if (name.empty() || !seen.emplace(name, true).second) {
          throw std::runtime_error("The first row must have non-empty, unique column names to enable headers.");
        }
      }
      rows_.erase(rows_.begin());
      relabel_columns(names);
    }
    has_headers_ = has_headers;
  }
  output_format_ = std::move(next_format);
  modified_ = true;
}

void CsvIndexedFile::read_csv_options(const std::string& delimiter,
                                     const std::string& quote, bool has_headers) {
  if (modified_) throw std::runtime_error("Save or reset changes before changing the input dialect.");
  auto next_format = WithDialect(format_, delimiter, quote);
  next_format.header_row(has_headers ? 0 : -1);
  recover_renamed_source();
  CsvIndexedFile next;
  next.input_path_ = input_path_;
  next.file_name_ = file_name_;
  next.format_ = std::move(next_format);
  next.columns_ = columns_;  // Retain the known width of an empty headerless file.
  next.reload();  // Explicit input characters must bypass automatic inference.
  *this = std::move(next);
}

std::vector<std::vector<std::string>> CsvIndexedFile::read_source_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  if (offset >= index_.size() || limit == 0) {
    return {};
  }

  const auto count = std::min<std::uint64_t>(limit, index_.size() - offset);
  const auto start = index_[static_cast<std::size_t>(offset)].byte_offset;
  const auto after_last_row = offset + count;
  const auto end = after_last_row < index_.size()
      ? index_[static_cast<std::size_t>(after_last_row)].byte_offset
      : file_size_;
  if (end < start) {
    throw std::runtime_error("row index is not monotonic");
  }

  const auto length = end - start;
  if (length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("requested row page is too large to materialize");
  }

  std::ifstream input(input_path_, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open input file: " + input_path_);
  }
  input.seekg(static_cast<std::streamoff>(start), std::ios::beg);

  std::string buffer(static_cast<std::size_t>(length), '\0');
  input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  buffer.resize(static_cast<std::size_t>(input.gcount()));

  auto page_format = format_;
  page_format.no_header().variable_columns(csv::VariableColumnPolicy::KEEP);
  std::stringstream page_stream(buffer);
  csv::CSVReader page_reader(page_stream, page_format);

  std::vector<std::vector<std::string>> rows;
  rows.reserve(static_cast<std::size_t>(count));
  for (auto& row : page_reader) {
    rows.emplace_back(std::vector<std::string>(row));
    if (rows.size() == count) {
      break;
    }
  }
  return rows;
}

std::vector<std::string> CsvIndexedFile::read_source_row(const std::uint64_t source_row) const {
  auto rows = read_source_rows(source_row, 1);
  if (rows.empty()) {
    throw std::out_of_range("row index out of bounds");
  }
  return std::move(rows.front());
}

std::vector<std::string> CsvIndexedFile::row_values_for_columns(
    const CsvLogicalRow& row,
    const std::vector<CsvColumnDescriptor>& columns) const {
  std::vector<std::string> source_values;
  if (row.source_row) {
    source_values = read_source_row(*row.source_row);
  }

  std::vector<std::string> values;
  values.reserve(columns.size());
  for (const auto& column : columns) {
    std::string value = column.default_value;
    if (column.source_index && *column.source_index < source_values.size()) {
      value = source_values[*column.source_index];
    }
    if (const auto edit = row.cells.find(column.name); edit != row.cells.end()) {
      value = edit->second;
    }
    values.push_back(std::move(value));
  }
  return values;
}

std::vector<std::vector<std::string>> CsvIndexedFile::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  if (offset >= row_count() || limit == 0) {
    return {};
  }

  const auto count = std::min<std::uint64_t>(limit, row_count() - offset);
  bool direct_source_page = columns_.size() == headers_.size();
  for (std::size_t i = 0; direct_source_page && i < columns_.size(); ++i) {
    direct_source_page = columns_[i].source_index == i;
  }
  const auto first_source_row = rows_[static_cast<std::size_t>(offset)].source_row;
  direct_source_page = direct_source_page && first_source_row.has_value();
  for (std::uint64_t i = 0; direct_source_page && i < count; ++i) {
    const auto& row = rows_[static_cast<std::size_t>(offset + i)];
    direct_source_page = row.source_row && row.cells.empty() &&
        *row.source_row == *first_source_row + i;
  }
  if (direct_source_page) {
    auto page = read_source_rows(*first_source_row, count);
    for (auto& row : page) if (row.size() < columns_.size()) row.resize(columns_.size());
    return page;
  }

  std::vector<std::vector<std::string>> rows;
  rows.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t i = 0; i < count; ++i) {
    rows.push_back(row_values_for_columns(
        rows_[static_cast<std::size_t>(offset + i)], columns_));
  }
  return rows;
}

std::size_t CsvIndexedFile::column_index(const std::string& column) const {
  const auto iter = std::ranges::find(headers_, column);
  if (iter == headers_.end()) {
    throw std::runtime_error("unknown column: " + column);
  }
  return static_cast<std::size_t>(std::distance(headers_.begin(), iter));
}

bool CsvIndexedFile::has_column(const std::string& column) const {
  return std::ranges::find(headers_, column) != headers_.end();
}

void CsvIndexedFile::refresh_headers() {
  headers_ = ColumnNames(columns_);
}

void CsvIndexedFile::validate_column_order(const std::vector<std::string>& columns) const {
  if (columns.empty()) {
    return;
  }
  if (columns.size() != headers_.size()) {
    throw std::runtime_error("column order must include every column exactly once");
  }

  std::unordered_map<std::string, std::size_t> counts;
  for (const auto& column : headers_) {
    ++counts[column];
  }
  for (const auto& column : columns) {
    const auto iter = counts.find(column);
    if (iter == counts.end() || iter->second == 0) {
      throw std::runtime_error("unknown column: " + column);
    }
    --iter->second;
  }
  for (const auto& [column, count] : counts) {
    if (count != 0) {
      throw std::runtime_error("missing column: " + column);
    }
  }
}

std::vector<CsvColumnDescriptor> CsvIndexedFile::ordered_columns(
    const std::vector<std::string>& columns) const {
  validate_column_order(columns);
  if (columns.empty()) {
    return columns_;
  }

  std::vector<CsvColumnDescriptor> ordered;
  ordered.reserve(columns.size());
  for (const auto& column : columns) {
    ordered.push_back(columns_[column_index(column)]);
  }
  return ordered;
}

void CsvIndexedFile::edit_cell(const std::uint64_t row,
                               const std::string& column,
                               const std::string& value) {
  if (row >= row_count()) {
    throw std::out_of_range("row index out of bounds");
  }
  if (!has_column(column)) {
    throw std::runtime_error("unknown column: " + column);
  }
  rows_[static_cast<std::size_t>(row)].cells[column] = value;
  modified_ = true;
}

void CsvIndexedFile::delete_row(const std::uint64_t row) {
  if (row >= row_count()) {
    throw std::out_of_range("row index out of bounds");
  }
  rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(row));
  modified_ = true;
}

void CsvIndexedFile::insert_row(const std::uint64_t row, const std::vector<std::string>& values) {
  if (values.size() != headers_.size()) {
    throw std::runtime_error("inserted row must match header shape");
  }
  if (row > row_count()) {
    throw std::out_of_range("row index out of bounds");
  }

  CsvLogicalRow inserted;
  for (std::size_t i = 0; i < headers_.size(); ++i) {
    inserted.cells[headers_[i]] = values[i];
  }
  rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(row), std::move(inserted));
  modified_ = true;
}

void CsvIndexedFile::swap_rows(const std::uint64_t first, const std::uint64_t second) {
  if (first >= row_count() || second >= row_count()) {
    throw std::out_of_range("row index out of bounds");
  }
  if (first == second) {
    return;
  }
  std::swap(rows_[static_cast<std::size_t>(first)], rows_[static_cast<std::size_t>(second)]);
  modified_ = true;
}

void CsvIndexedFile::insert_column(const std::uint64_t column,
                                   const std::string& name,
                                   const std::string& value) {
  if (has_headers_ && name.empty()) {
    throw std::runtime_error("column name is required");
  }
  if (has_headers_ && has_column(name)) {
    throw std::runtime_error("column already exists: " + name);
  }
  if (column > columns_.size()) {
    throw std::out_of_range("column index out of bounds");
  }
  // A temporary unique key avoids colliding with existing synthetic labels.
  auto key = name;
  if (!has_headers_) { key = "_new"; while (has_column(key)) key += '_'; }
  columns_.insert(columns_.begin() + static_cast<std::ptrdiff_t>(column),
                  CsvColumnDescriptor{key, std::nullopt, value});
  if (!has_headers_) relabel_columns(SyntheticHeaders(columns_.size()));
  refresh_headers();
  modified_ = true;
}

void CsvIndexedFile::rename_column(const std::string& column, const std::string& name) {
  if (!has_headers_) throw std::runtime_error("Synthetic column labels cannot be renamed.");
  if (name.empty()) {
    throw std::runtime_error("column name is required");
  }
  const auto index = column_index(column);
  if (column == name) {
    return;
  }
  if (has_column(name)) {
    throw std::runtime_error("column already exists: " + name);
  }

  columns_[index].name = name;
  for (auto& row : rows_) {
    if (const auto iter = row.cells.find(column); iter != row.cells.end()) {
      row.cells[name] = std::move(iter->second);
      row.cells.erase(iter);
    }
  }
  modified_ = true;
  refresh_headers();
}

void CsvIndexedFile::delete_column(const std::string& column) {
  if (columns_.size() <= 1) {
    throw std::runtime_error("cannot delete the last column");
  }
  const auto index = column_index(column);
  columns_.erase(columns_.begin() + static_cast<std::ptrdiff_t>(index));
  for (auto& row : rows_) {
    row.cells.erase(column);
  }
  if (!has_headers_) relabel_columns(SyntheticHeaders(columns_.size()));
  refresh_headers();
  modified_ = true;
}

bool CsvIndexedFile::recover_renamed_source() {
  return RecoverRenamedSource(input_path_, file_name_, file_size_, source_mtime_);
}

void CsvIndexedFile::reload() {
  const auto input_path = input_path_;
  const auto options_format = format_;

  file_size_ = GetFileSize(input_path);
  source_mtime_ = GetFileMtime(input_path);

  // An empty headerless file can result from deleting its last data row.
  std::ifstream prefix(input_path, std::ios::binary);
  char bom[3]{};
  prefix.read(bom, 3);
  const bool bom_only = file_size_ == 3 && std::string(bom, 3) == "\xEF\xBB\xBF";
  if ((file_size_ == 0 || bom_only) && options_format.get_header() < 0 && !columns_.empty()) {
    index_.clear(); rows_.clear();
    has_headers_ = false; headers_disabled_reason_.clear(); has_bom_ = bom_only; output_format_ = format_;
    headers_ = SyntheticHeaders(columns_.size());
    for (std::size_t i = 0; i < columns_.size(); ++i) columns_[i] = {headers_[i], i, ""};
    modified_ = false;
    return;
  }

  // Index every record using the same policy as page reads. Skipping malformed
  // rows here would make logical row offsets disagree with later page reads.
  auto read_format = options_format;
  read_format.variable_columns(csv::VariableColumnPolicy::KEEP);
  csv::CSVReader reader(input_path, read_format);
  format_ = reader.get_format();
  has_headers_ = options_format.get_header() >= 0;
  // get_format includes inferred column names. Clear them without forcing a
  // header on a headerless source, otherwise reload would skip its first row.
  format_.header_row(has_headers_ ? 0 : -1);
  output_format_ = format_;
  has_bom_ = reader.utf8_bom();
  newline_ = RecordNewline(input_path, format_);
  headers_ = reader.get_col_names();

  index_.clear();
  rows_.clear();
  std::size_t width = headers_.size();
  std::size_t first_width = has_headers_ ? headers_.size() : 0;
  bool first_record = true;
  for (auto& row : reader) {
    if (!has_headers_ && first_record) first_width = row.size();
    first_record = false;
    width = std::max(width, row.size());
    const auto source_row = static_cast<std::uint64_t>(index_.size());
    index_.push_back({static_cast<std::uint64_t>(row.byte_offset())});
    rows_.push_back({source_row, {}});
  }
  if (has_headers_ && width > first_width) {
    // Re-read the original first record as data, retaining its byte offset and
    // every extra field. Do not convert only the already-indexed rows.
    format_.header_row(-1);
    reload();
    return;
  }
  headers_disabled_reason_ = width > first_width
      ? "Headers are disabled because some rows have more fields than the first row (" +
            std::to_string(width) + " versus " + std::to_string(first_width) +
            "). The first row is shown as data to preserve every value. Add missing header names in the first row, or remove surplus columns, then save and reopen. If the delimiter or quoting is wrong, correct the input dialect and read the file again."
      : "";
  if (!has_headers_) headers_ = SyntheticHeaders(width);
  if (headers_.empty()) throw std::runtime_error("input appears to have no header row");
  columns_.clear();
  for (std::size_t i = 0; i < headers_.size(); ++i) columns_.push_back({headers_[i], i, ""});
  modified_ = false;
}

void CsvIndexedFile::reset() {
  recover_renamed_source();
  reload();
}

void CsvIndexedFile::save(const std::vector<std::string>& columns) {
  const auto save_columns = ordered_columns(columns);

  recover_renamed_source();
  if (GetFileSize(input_path_) != file_size_ ||
      GetFileMtime(input_path_) != source_mtime_) {
    throw std::runtime_error("source file changed externally; reload before saving");
  }

  const auto target = std::filesystem::path(input_path_);
  const auto temp = target.parent_path() /
      (target.filename().string() + ".csvzall-save-" + GenerateSaveToken() + ".tmp");
  try {
    {
      std::ofstream output(temp, std::ios::binary);
      if (!output) {
        throw std::runtime_error("unable to open temporary save file: " + temp.string());
      }
      if (has_bom_) output << "\xEF\xBB\xBF";
      if (has_headers_) WriteRow(output, ColumnNames(save_columns), output_format_, newline_);
      for (const auto& row : rows_) {
        WriteRow(output, row_values_for_columns(row, save_columns), output_format_, newline_);
      }
      output.close();
      if (!output) {
        throw std::runtime_error("failed to write temporary save file: " + temp.string());
      }
    }

#ifdef _WIN32
    if (!MoveFileExW(temp.wstring().c_str(),
                     target.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      throw std::system_error(
          static_cast<int>(GetLastError()), std::system_category(), "failed to replace CSV file");
    }
#else
    std::filesystem::rename(temp, target);
#endif
    format_ = output_format_;
    format_.header_row(has_headers_ ? 0 : -1);
    reload();
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    throw;
  }
}

CsvViewData CsvViewData::Open(
    const std::string& input_path,
    csv::CSVFormat format,
    std::function<void(const std::string&)> verbose) {
  return CsvViewData(CsvIndexedFile::Open(input_path, std::move(format), std::move(verbose)));
}

CsvViewData::CsvViewData(CsvIndexedFile indexed)
    : data_(std::move(indexed)) {}

CsvViewDataMode CsvViewData::mode() const {
  return CsvViewDataMode::Paged;
}

std::string_view CsvViewData::mode_name() const {
  return "paged";
}

const std::string& CsvViewData::input_path() const {
  return data_.input_path();
}

const std::string& CsvViewData::file_name() const {
  return data_.file_name();
}

const std::vector<std::string>& CsvViewData::headers() const {
  return data_.headers();
}

std::uint64_t CsvViewData::row_count() const {
  return data_.row_count();
}

std::uint64_t CsvViewData::file_size() const {
  return data_.file_size();
}

std::string CsvViewData::delimiter() const { return data_.delimiter(); }
std::string CsvViewData::quote() const { return data_.quote(); }
bool CsvViewData::has_headers() const { return data_.has_headers(); }
const std::string& CsvViewData::headers_disabled_reason() const { return data_.headers_disabled_reason(); }
bool CsvViewData::has_bom() const { return data_.has_bom(); }
void CsvViewData::set_csv_options(const std::string& delimiter, const std::string& quote, bool has_headers) {
  data_.set_csv_options(delimiter, quote, has_headers);
}
void CsvViewData::read_csv_options(const std::string& delimiter, const std::string& quote, bool has_headers) {
  data_.read_csv_options(delimiter, quote, has_headers);
}

std::vector<std::vector<std::string>> CsvViewData::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  return data_.read_rows(offset, limit);
}

void CsvViewData::edit_cell(const std::uint64_t row,
                            const std::string& column,
                            const std::string& value) {
  data_.edit_cell(row, column, value);
}

void CsvViewData::delete_row(const std::uint64_t row) {
  data_.delete_row(row);
}

void CsvViewData::insert_row(const std::uint64_t row, const std::vector<std::string>& values) {
  data_.insert_row(row, values);
}

void CsvViewData::swap_rows(const std::uint64_t first, const std::uint64_t second) {
  data_.swap_rows(first, second);
}

void CsvViewData::insert_column(const std::uint64_t column,
                                const std::string& name,
                                const std::string& value) {
  data_.insert_column(column, name, value);
}

void CsvViewData::rename_column(const std::string& column, const std::string& name) {
  data_.rename_column(column, name);
}

void CsvViewData::delete_column(const std::string& column) {
  data_.delete_column(column);
}

bool CsvViewData::recover_renamed_source() {
  return data_.recover_renamed_source();
}

void CsvViewData::reset() {
  data_.reset();
}

void CsvViewData::save(const std::vector<std::string>& columns) {
  data_.save(columns);
}

}  // namespace csvzall::viewer

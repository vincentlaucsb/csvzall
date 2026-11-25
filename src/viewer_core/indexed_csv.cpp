#include "indexed_csv.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
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
#elif !defined(__EMSCRIPTEN__)
#include <sys/stat.h>
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

// A Sync client can preserve the previous modification time when replacing CSV bytes.
std::array<std::uint64_t, 4> GetSourceIdentity(const std::string& path) {
#ifdef _WIN32
  const HANDLE handle = CreateFileW(std::filesystem::path(path).c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("unable to identify CSV source: " + path);
  BY_HANDLE_FILE_INFORMATION identity{};
  FILE_BASIC_INFO times{};
  const bool read = GetFileInformationByHandle(handle, &identity) &&
      GetFileInformationByHandleEx(handle, FileBasicInfo, &times, sizeof(times));
  CloseHandle(handle);
  if (!read) throw std::runtime_error("unable to identify CSV source: " + path);
  return {identity.dwVolumeSerialNumber, identity.nFileIndexHigh, identity.nFileIndexLow,
          static_cast<std::uint64_t>(times.ChangeTime.QuadPart)};
#elif defined(__EMSCRIPTEN__)
  (void)path;
  return {};
#else
  struct stat identity{};
  if (stat(path.c_str(), &identity) != 0) throw std::runtime_error("unable to identify CSV source: " + path);
#ifdef __APPLE__
  const auto changed = identity.st_ctimespec;
#else
  const auto changed = identity.st_ctim;
#endif
  return {static_cast<std::uint64_t>(identity.st_dev), static_cast<std::uint64_t>(identity.st_ino),
          static_cast<std::uint64_t>(changed.tv_sec), static_cast<std::uint64_t>(changed.tv_nsec)};
#endif
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
    decltype(row.cells) cells;
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
      for (const auto& column : columns_) first.cells[column.name] = {column.name, {}};
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
  mark_modified();
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
  next.allow_rename_recovery_ = allow_rename_recovery_;
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
  return logical_row_values(row, columns, source_values);
}

std::vector<std::string> CsvIndexedFile::logical_row_values(
    const CsvLogicalRow& row, const std::vector<CsvColumnDescriptor>& columns,
    const std::vector<std::string>& source_values, bool original_values) {
  std::vector<std::string> values;
  values.reserve(columns.size());
  for (const auto& column : columns) {
    std::string value = column.default_value;
    if (column.source_index && *column.source_index < source_values.size()) {
      value = source_values[*column.source_index];
    }
    if (const auto edit = row.cells.find(column.name); edit != row.cells.end()) {
      const auto& cell = edit->second;
      value = original_values && cell.original ? *cell.original : cell.value;
    }
    values.push_back(std::move(value));
  }
  return values;
}

std::vector<std::vector<std::string>> CsvIndexedFile::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit, bool original_values) const {
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
  for (std::uint64_t i = 0; i < count;) {
    const auto& row = rows_[offset + i];
    if (!row.source_row) {
      rows.push_back(logical_row_values(row, columns_, {}, original_values));
      ++i;
      continue;
    }
    std::uint64_t run = 1;
    while (i + run < count && rows_[offset + i + run].source_row == *row.source_row + run) ++run;
    auto source_rows = read_source_rows(*row.source_row, run);
    if (source_rows.size() != run) throw std::out_of_range("row index out of bounds");
    for (std::uint64_t j = 0; j < run; ++j) {
      rows.push_back(logical_row_values(rows_[offset + i + j], columns_, source_rows[j], original_values));
    }
    i += run;
  }
  return rows;
}

CsvIndexedFile::SortIndex CsvIndexedFile::build_sort_index(const std::string& column,
                                                          bool descending) const {
  const auto column_number = column_index(column);
  SortIndex index{column, descending, {}};
  index.row_ids.reserve(rows_.size());
  std::vector<std::uint64_t> inserted;
  std::vector<std::string> keys(rows_.size());
  constexpr std::uint64_t batch_size = 500;
  for (std::uint64_t offset = 0; offset < row_count(); offset += batch_size) {
    auto rows = read_rows(offset, batch_size, true);
    if (rows.size() != std::min(batch_size, row_count() - offset)) {
      throw std::runtime_error("CSV changed while sorting");
    }
    for (std::size_t slot = 0; slot < rows.size(); ++slot) {
      const auto row_id = offset + slot;
      if (rows_[row_id].inserted) {
        inserted.push_back(row_id);
      } else {
        index.row_ids.push_back(row_id);
        keys[row_id] = std::move(rows[slot][column_number]);
      }
    }
  }

  // A column with any non-numeric value sorts as text. Mixing numeric and
  // lexical comparisons within the same column would break transitivity. Draft
  // rows are excluded so their values cannot change the comparison before save.
  std::vector<long double> numbers(keys.size());
  const bool numeric = std::ranges::all_of(index.row_ids, [&](auto row_id) {
    const auto& key = keys[row_id];
    char* end = nullptr;
    const auto value = std::strtold(key.c_str(), &end);
    numbers[row_id] = value;
    return key.empty() || (end == key.c_str() + key.size() && std::isfinite(value));
  });

  std::stable_sort(index.row_ids.begin(), index.row_ids.end(), [&](auto first, auto second) {
    if (descending) std::swap(first, second);
    if (keys[first].empty() != keys[second].empty()) return keys[first].empty();
    return numeric ? numbers[first] < numbers[second] : keys[first] < keys[second];
  });
  index.row_ids.insert(index.row_ids.end(), inserted.begin(), inserted.end());
  return index;
}

CsvSortedPage CsvIndexedFile::read_sorted_rows(std::uint64_t offset, std::uint64_t limit,
                                             const std::string& column, bool descending) const {
  if (offset >= row_count() || limit == 0) return {};
  if (!sort_index_ || sort_index_->column != column || sort_index_->descending != descending) {
    sort_index_ = build_sort_index(column, descending);
  }
  const auto count = std::min(limit, row_count() - offset);
  const auto first = sort_index_->row_ids.begin() + static_cast<std::ptrdiff_t>(offset);
  CsvSortedPage page{{first, first + static_cast<std::ptrdiff_t>(count)}, {}};
  page.rows.resize(page.row_ids.size());

  // Read contiguous source ranges together and place rows back in view order.
  std::vector<std::size_t> slots(page.row_ids.size());
  std::iota(slots.begin(), slots.end(), std::size_t{0});
  std::ranges::sort(slots, {}, [&](auto slot) { return page.row_ids[slot]; });
  for (std::size_t start = 0; start < slots.size();) {
    const auto row_id = page.row_ids[slots[start]];
    auto end = start + 1;
    while (end < slots.size() && page.row_ids[slots[end]] == row_id + end - start) ++end;
    auto rows = read_rows(row_id, end - start);
    if (rows.size() != end - start) throw std::runtime_error("CSV changed while reading sorted rows");
    for (auto slot = start; slot < end; ++slot) {
      page.rows[slots[slot]] = std::move(rows[slot - start]);
    }
    start = end;
  }
  return page;
}

void CsvIndexedFile::mark_modified() {
  sort_index_.reset();
  modified_ = true;
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

CsvRowChanges CsvIndexedFile::row_changes(std::uint64_t row) const {
  const auto& logical = rows_.at(row);
  CsvRowChanges changes{logical.inserted, {}};
  for (const auto& [column, cell] : logical.cells) {
    if (cell.original && cell.value != *cell.original) changes.columns.push_back(column);
  }
  return changes;
}

bool CsvIndexedFile::edit_cell(const std::uint64_t row,
                               const std::string& column,
                               const std::string& value) {
  if (row >= row_count()) {
    throw std::out_of_range("row index out of bounds");
  }
  const auto& descriptor = columns_[column_index(column)];
  auto& logical = rows_[row];
  auto iter = logical.cells.find(column);
  if (iter == logical.cells.end()) {
    std::string original = descriptor.default_value;
    if (logical.source_row && descriptor.source_index) {
      auto source = read_source_row(*logical.source_row);
      if (*descriptor.source_index < source.size()) original = std::move(source[*descriptor.source_index]);
    }
    iter = logical.cells.emplace(column, CsvLogicalRow::Cell{value, std::move(original)}).first;
  } else {
    auto& cell = iter->second;
    if (!cell.original) cell.original = cell.value;
    cell.value = value;
  }
  modified_ = true;
  return iter->second.value != *iter->second.original;
}

void CsvIndexedFile::delete_row(const std::uint64_t row) {
  if (row >= row_count()) {
    throw std::out_of_range("row index out of bounds");
  }
  rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(row));
  mark_modified();
}

void CsvIndexedFile::insert_row(const std::uint64_t row, const std::vector<std::string>& values) {
  if (values.size() != headers_.size()) {
    throw std::runtime_error("inserted row must match header shape");
  }
  if (row > row_count()) {
    throw std::out_of_range("row index out of bounds");
  }

  CsvLogicalRow inserted;
  inserted.inserted = true;
  for (std::size_t i = 0; i < headers_.size(); ++i) {
    inserted.cells[headers_[i]] = {values[i], {}};
  }
  const bool appended = row == row_count();
  rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(row), std::move(inserted));
  if (appended && sort_index_) {
    sort_index_->row_ids.push_back(row);
    modified_ = true;
  } else {
    mark_modified();
  }
}

void CsvIndexedFile::swap_rows(const std::uint64_t first, const std::uint64_t second) {
  if (first >= row_count() || second >= row_count()) {
    throw std::out_of_range("row index out of bounds");
  }
  if (first == second) {
    return;
  }
  std::swap(rows_[static_cast<std::size_t>(first)], rows_[static_cast<std::size_t>(second)]);
  mark_modified();
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
  mark_modified();
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
    if (auto cell = row.cells.extract(column); !cell.empty()) {
      cell.key() = name;
      row.cells.insert(std::move(cell));
    }
  }
  mark_modified();
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
  mark_modified();
}

void CsvIndexedFile::keep_original_path() {
  allow_rename_recovery_ = false;
}

bool CsvIndexedFile::recover_renamed_source() {
  return allow_rename_recovery_ && RecoverRenamedSource(input_path_, file_name_, file_size_, source_mtime_);
}

void CsvIndexedFile::reload() {
  sort_index_.reset();
  const auto input_path = input_path_;
  const auto options_format = format_;

  file_size_ = GetFileSize(input_path);
  source_mtime_ = GetFileMtime(input_path);
  source_identity_ = GetSourceIdentity(input_path);

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

bool CsvIndexedFile::source_changed() const {
  return !FileExists(input_path_) ||
         GetFileSize(input_path_) != file_size_ ||
         GetFileMtime(input_path_) != source_mtime_ ||
         (!allow_rename_recovery_ && GetSourceIdentity(input_path_) != source_identity_);
}

bool CsvIndexedFile::reload_if_changed() {
  if (!source_changed()) return false;
  CsvIndexedFile updated;
  updated.input_path_ = input_path_;
  updated.file_name_ = file_name_;
  updated.allow_rename_recovery_ = allow_rename_recovery_;
  updated.format_ = format_;
  if (!has_headers_) updated.columns_ = columns_;
  updated.reload();
  if (updated.source_changed()) {
    throw std::runtime_error("source file changed during reload; try again");
  }
  *this = std::move(updated);
  return true;
}

void CsvIndexedFile::reset() {
  recover_renamed_source();
  reload();
}

void CsvIndexedFile::save(const std::vector<std::string>& columns) {
  recover_renamed_source();
  if (source_changed()) {
    throw std::runtime_error("source file changed externally; reload before saving");
  }

  const auto target = std::filesystem::path(input_path_);
  const auto temp = target.parent_path() /
      (target.filename().string() + ".csvzall-save-" + GenerateSaveToken() + ".tmp");
  try {
    write_save_file(temp, columns);
    if (source_changed()) {
      throw std::runtime_error("source file changed externally; reload before saving");
    }
    commit_save_file(temp);
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    throw;
  }
}

void CsvIndexedFile::write_save_file(const std::filesystem::path& path,
                                    const std::vector<std::string>& columns) const {
  const auto save_columns = ordered_columns(columns);
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("unable to open temporary save file: " + path.string());
  if (has_bom_) output << "\xEF\xBB\xBF";
  if (has_headers_) WriteRow(output, ColumnNames(save_columns), output_format_, newline_);
  for (const auto& row : rows_) {
    WriteRow(output, row_values_for_columns(row, save_columns), output_format_, newline_);
  }
  output.close();
  if (!output) throw std::runtime_error("failed to write temporary save file: " + path.string());
}

void CsvIndexedFile::commit_save_file(const std::filesystem::path& path) {
#ifdef _WIN32
  if (!MoveFileExW(path.c_str(), std::filesystem::path(input_path_).c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::system_error(
        static_cast<int>(GetLastError()), std::system_category(), "failed to replace CSV file");
  }
#else
  std::filesystem::rename(path, input_path_);
#endif
  format_ = output_format_;
  format_.header_row(has_headers_ ? 0 : -1);
  reload();
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

CsvSortedPage CsvViewData::read_sorted_rows(std::uint64_t offset, std::uint64_t limit,
                                         const std::string& column, bool descending) const {
  return data_.read_sorted_rows(offset, limit, column, descending);
}

CsvRowChanges CsvViewData::row_changes(std::uint64_t row) const {
  return data_.row_changes(row);
}

bool CsvViewData::edit_cell(const std::uint64_t row,
                            const std::string& column,
                            const std::string& value) {
  return data_.edit_cell(row, column, value);
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

void CsvViewData::keep_original_path() {
  data_.keep_original_path();
}

bool CsvViewData::recover_renamed_source() {
  return data_.recover_renamed_source();
}

bool CsvViewData::source_changed() const {
  return data_.source_changed();
}

bool CsvViewData::reload_if_changed() {
  return data_.reload_if_changed();
}

void CsvViewData::reset() {
  data_.reset();
}

void CsvViewData::save(const std::vector<std::string>& columns) {
  data_.save(columns);
}

void CsvViewData::write_save_file(const std::string& path) const {
  data_.write_save_file(path);
}

void CsvViewData::commit_save_file(const std::string& path) {
  data_.commit_save_file(path);
}

}  // namespace csvzall::viewer

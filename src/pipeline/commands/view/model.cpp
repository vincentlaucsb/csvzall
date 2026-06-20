#include "../view.hpp"

#include "support.hpp"

#include "../../common/gzip_stream.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
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

namespace csvzall::pipeline::commands {

namespace {

using view_internal::GenerateSessionToken;
using view_internal::MakeViewFormat;

void ValidatePlainLocalViewInput(const std::string& input_path, const RunOptions& options) {
  if (input_path.empty() || input_path == "-") {
    throw std::runtime_error("stdin is not supported; pass a plain local CSV file path");
  }
  if (!options.zip_entry.empty() || common::IsZipPath(input_path) || common::IsGzipPath(input_path)) {
    throw std::runtime_error("view currently supports plain local CSV files only");
  }
}

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

}  // namespace

CsvIndexedFile CsvIndexedFile::Open(const std::string& input_path,
                                    const RunOptions& options,
                                    const LoggerCallbacks& logger,
                                    RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);

  CsvIndexedFile indexed;
  indexed.input_path_ = input_path;
  indexed.file_name_ = std::filesystem::path(input_path).filename().string();

  indexed.file_size_ = GetFileSize(input_path);
  RequireNonEmptyCsvFile(indexed.file_size_);
  indexed.source_mtime_ = GetFileMtime(input_path);

  auto format = MakeViewFormat(options);
  csv::CSVReader reader(input_path, format);
  indexed.format_ = reader.get_format();
  indexed.format_.header_row(0);
  indexed.headers_ = reader.get_col_names();
  if (indexed.headers_.empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  indexed.columns_.reserve(indexed.headers_.size());
  for (std::size_t i = 0; i < indexed.headers_.size(); ++i) {
    indexed.columns_.push_back({indexed.headers_[i], i, ""});
  }

  for (auto& row : reader) {
    const auto source_row = static_cast<std::uint64_t>(indexed.index_.size());
    indexed.index_.push_back({static_cast<std::uint64_t>(row.byte_offset())});
    indexed.rows_.push_back({source_row, {}});
  }

  stats.rows_processed = static_cast<std::uint64_t>(indexed.index_.size());
  stats.bytes_processed = indexed.file_size_;
  if (logger.verbose) {
    logger.verbose("view: indexed " + std::to_string(indexed.index_.size()) +
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
    return read_source_rows(*first_source_row, count);
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
}

void CsvIndexedFile::delete_row(const std::uint64_t row) {
  if (row >= row_count()) {
    throw std::out_of_range("row index out of bounds");
  }
  rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(row));
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
}

void CsvIndexedFile::swap_rows(const std::uint64_t first, const std::uint64_t second) {
  if (first >= row_count() || second >= row_count()) {
    throw std::out_of_range("row index out of bounds");
  }
  if (first == second) {
    return;
  }
  std::swap(rows_[static_cast<std::size_t>(first)], rows_[static_cast<std::size_t>(second)]);
}

void CsvIndexedFile::insert_column(const std::uint64_t column,
                                   const std::string& name,
                                   const std::string& value) {
  if (name.empty()) {
    throw std::runtime_error("column name is required");
  }
  if (has_column(name)) {
    throw std::runtime_error("column already exists: " + name);
  }
  if (column > columns_.size()) {
    throw std::out_of_range("column index out of bounds");
  }
  columns_.insert(columns_.begin() + static_cast<std::ptrdiff_t>(column),
                  CsvColumnDescriptor{name, std::nullopt, value});
  refresh_headers();
}

void CsvIndexedFile::rename_column(const std::string& column, const std::string& name) {
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
  refresh_headers();
}

bool CsvIndexedFile::recover_renamed_source() {
  return RecoverRenamedSource(input_path_, file_name_, file_size_, source_mtime_);
}

void CsvIndexedFile::reload() {
  const auto input_path = input_path_;
  const auto options_format = format_;

  file_size_ = GetFileSize(input_path);
  RequireNonEmptyCsvFile(file_size_);
  source_mtime_ = GetFileMtime(input_path);

  csv::CSVReader reader(input_path, options_format);
  format_ = reader.get_format();
  format_.header_row(0);
  headers_ = reader.get_col_names();
  if (headers_.empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  columns_.clear();
  columns_.reserve(headers_.size());
  for (std::size_t i = 0; i < headers_.size(); ++i) {
    columns_.push_back({headers_[i], i, ""});
  }

  index_.clear();
  rows_.clear();
  for (auto& row : reader) {
    const auto source_row = static_cast<std::uint64_t>(index_.size());
    index_.push_back({static_cast<std::uint64_t>(row.byte_offset())});
    rows_.push_back({source_row, {}});
  }
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
      (target.filename().string() + ".csvzall-save-" + GenerateSessionToken() + ".tmp");
  try {
    {
      std::ofstream output(temp, std::ios::binary);
      if (!output) {
        throw std::runtime_error("unable to open temporary save file: " + temp.string());
      }
      auto writer = csv::make_csv_writer(output).set_auto_flush(false);
      writer << ColumnNames(save_columns);
      for (const auto& row : rows_) {
        writer << row_values_for_columns(row, save_columns);
      }
      writer.flush();
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
    reload();
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    throw;
  }
}

CsvViewData CsvViewData::Open(const std::string& input_path,
                              const RunOptions& options,
                              const LoggerCallbacks& logger,
                              RunStats& stats) {
  return CsvViewData(CsvIndexedFile::Open(input_path, options, logger, stats));
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

}  // namespace csvzall::pipeline::commands

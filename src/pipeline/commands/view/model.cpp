#include "../view.hpp"

#include "support.hpp"

#include "../../common/gzip_stream.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
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
using view_internal::kBytesPerMiB;
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

bool RecoverRenamedMaterializedSource(CsvMaterializedFile& materialized) {
  if (FileExists(materialized.input_path)) {
    return false;
  }

  const auto original = std::filesystem::path(materialized.input_path);
  auto parent = original.parent_path();
  if (parent.empty()) {
    parent = ".";
  }

  std::error_code ec;
  if (!std::filesystem::exists(parent, ec)) {
    throw std::runtime_error("source file no longer exists: " + materialized.input_path);
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
    if (entry_ec || static_cast<std::uint64_t>(size) != materialized.source_size) {
      continue;
    }
    const auto mtime = entry.last_write_time(entry_ec);
    if (entry_ec || mtime != materialized.source_mtime) {
      continue;
    }
    matches.push_back(entry.path());
  }

  if (matches.empty()) {
    throw std::runtime_error("source file no longer exists: " + materialized.input_path);
  }
  if (matches.size() > 1) {
    throw std::runtime_error(
        "source file was renamed, but the new path is ambiguous; reopen the viewer");
  }

  materialized.input_path = matches.front().string();
  materialized.file_name = matches.front().filename().string();
  return true;
}

void RequireNonEmptyCsvFile(const std::uint64_t file_size) {
  if (file_size == 0) {
    throw std::runtime_error(
        "CSV file is empty. Add a header row first, for example: column");
  }
}

std::uint64_t ThresholdBytes(std::size_t threshold_mb) {
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  if (threshold_mb > max / kBytesPerMiB) {
    return max;
  }
  return static_cast<std::uint64_t>(threshold_mb) * kBytesPerMiB;
}

void ValidateColumnOrder(const csv::DataFrame<>& frame,
                         const std::vector<std::string>& columns) {
  if (columns.empty()) {
    return;
  }
  if (columns.size() != frame.n_cols()) {
    throw std::runtime_error("column order must include every column exactly once");
  }

  std::unordered_map<std::string, std::size_t> counts;
  for (const auto& column : frame.columns()) {
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

void ReloadMaterializedFile(CsvMaterializedFile& materialized) {
  const auto file_size = GetFileSize(materialized.input_path);
  csv::CSVReader reader(materialized.input_path, materialized.format);
  auto frame = std::make_shared<csv::DataFrame<>>(reader);
  if (frame->columns().empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  materialized.frame = std::move(frame);
  materialized.source_size = file_size;
  materialized.source_mtime = GetFileMtime(materialized.input_path);
}

CsvMaterializedFile OpenMaterializedFile(const std::string& input_path,
                                         const RunOptions& options,
                                         const LoggerCallbacks& logger,
                                         RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);

  CsvMaterializedFile materialized;
  materialized.input_path = input_path;
  materialized.file_name = std::filesystem::path(input_path).filename().string();
  const auto file_size = GetFileSize(input_path);
  RequireNonEmptyCsvFile(file_size);
  materialized.source_size = file_size;
  materialized.source_mtime = GetFileMtime(input_path);

  auto format = MakeViewFormat(options);
  materialized.format = format;
  csv::CSVReader reader(input_path, format);
  materialized.frame = std::make_shared<csv::DataFrame<>>(reader);
  if (materialized.frame->columns().empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  stats.rows_processed = static_cast<std::uint64_t>(materialized.frame->n_rows());
  stats.bytes_processed = file_size;
  if (logger.verbose) {
    logger.verbose("view: materialized " + std::to_string(materialized.frame->n_rows()) +
                   " row(s) from " + input_path);
  }
  return materialized;
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

  auto format = MakeViewFormat(options);
  csv::CSVReader reader(input_path, format);
  indexed.format_ = reader.get_format();
  indexed.headers_ = reader.get_col_names();
  if (indexed.headers_.empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  for (auto& row : reader) {
    indexed.index_.push_back({static_cast<std::uint64_t>(row.byte_offset())});
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
  return static_cast<std::uint64_t>(index_.size());
}

std::vector<std::vector<std::string>> CsvIndexedFile::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  if (offset >= row_count() || limit == 0) {
    return {};
  }

  const auto count = std::min<std::uint64_t>(limit, row_count() - offset);
  const auto start = index_[static_cast<std::size_t>(offset)].byte_offset;
  const auto after_last_row = offset + count;
  const auto end = after_last_row < row_count()
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

CsvViewData CsvViewData::Open(const std::string& input_path,
                              const RunOptions& options,
                              const LoggerCallbacks& logger,
                              RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);
  const auto file_size = GetFileSize(input_path);
  const auto materialize = [&]() {
    if (options.view_edit) {
      return true;
    }
    switch (options.view_mode) {
      case ViewModeSelection::Materialized:
        return true;
      case ViewModeSelection::Paged:
        return false;
      case ViewModeSelection::Auto:
        return file_size <= ThresholdBytes(options.view_materialize_threshold_mb);
    }
    return false;
  }();

  if (materialize) {
    return CsvViewData(OpenMaterializedFile(input_path, options, logger, stats));
  }
  return CsvViewData(CsvIndexedFile::Open(input_path, options, logger, stats));
}

CsvViewData::CsvViewData(CsvMaterializedFile materialized)
    : data_(std::move(materialized)) {}

CsvViewData::CsvViewData(CsvIndexedFile indexed)
    : data_(std::move(indexed)) {}

CsvViewDataMode CsvViewData::mode() const {
  return std::holds_alternative<CsvMaterializedFile>(data_)
      ? CsvViewDataMode::Materialized
      : CsvViewDataMode::Paged;
}

std::string_view CsvViewData::mode_name() const {
  return mode() == CsvViewDataMode::Materialized ? "materialized" : "paged";
}

const std::string& CsvViewData::input_path() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->input_path;
  }
  return std::get<CsvIndexedFile>(data_).input_path();
}

const std::string& CsvViewData::file_name() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->file_name;
  }
  return std::get<CsvIndexedFile>(data_).file_name();
}

const std::vector<std::string>& CsvViewData::headers() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->frame->columns();
  }
  return std::get<CsvIndexedFile>(data_).headers();
}

std::uint64_t CsvViewData::row_count() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return static_cast<std::uint64_t>(materialized->frame->n_rows());
  }
  return std::get<CsvIndexedFile>(data_).row_count();
}

std::vector<std::vector<std::string>> CsvViewData::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    if (offset >= row_count() || limit == 0) {
      return {};
    }
    const auto count = std::min<std::uint64_t>(limit, row_count() - offset);
    std::vector<std::vector<std::string>> rows;
    rows.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
      rows.emplace_back(std::vector<std::string>(
          materialized->frame->at(static_cast<std::size_t>(offset + i))));
    }
    return rows;
  }
  return std::get<CsvIndexedFile>(data_).read_rows(offset, limit);
}

void CsvViewData::edit_cell(const std::uint64_t row,
                            const std::string& column,
                            const std::string& value) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (!materialized->frame->has_column(column)) {
    throw std::runtime_error("unknown column: " + column);
  }
  if (row >= materialized->frame->n_rows()) {
    throw std::out_of_range("row index out of bounds");
  }
  materialized->frame->at(static_cast<std::size_t>(row))[column] = value;
}

void CsvViewData::delete_row(const std::uint64_t row) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (row >= materialized->frame->n_rows()) {
    throw std::out_of_range("row index out of bounds");
  }
  if (!materialized->frame->at(static_cast<std::size_t>(row)).erase()) {
    throw std::runtime_error("failed to delete row");
  }
}

void CsvViewData::insert_row(const std::uint64_t row, const std::vector<std::string>& values) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (values.size() != materialized->frame->n_cols()) {
    throw std::runtime_error("inserted row must match header shape");
  }
  if (row > materialized->frame->n_rows()) {
    throw std::out_of_range("row index out of bounds");
  }
  materialized->frame->insert_row(static_cast<std::size_t>(row), values);
}

void CsvViewData::swap_rows(const std::uint64_t first, const std::uint64_t second) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  const auto row_count = materialized->frame->n_rows();
  if (first >= row_count || second >= row_count) {
    throw std::out_of_range("row index out of bounds");
  }
  if (first == second) {
    return;
  }

  const auto first_index = static_cast<std::size_t>(first);
  const auto second_index = static_cast<std::size_t>(second);
  const auto first_values = std::vector<std::string>(materialized->frame->at(first_index));
  const auto second_values = std::vector<std::string>(materialized->frame->at(second_index));
  for (std::size_t column = 0; column < materialized->frame->n_cols(); ++column) {
    materialized->frame->at(first_index)[column] = second_values[column];
    materialized->frame->at(second_index)[column] = first_values[column];
  }
}

void CsvViewData::insert_column(const std::uint64_t column,
                                const std::string& name,
                                const std::string& value) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (column > materialized->frame->n_cols()) {
    throw std::out_of_range("column index out of bounds");
  }
  materialized->frame->insert_column(static_cast<std::size_t>(column), name, value);
}

void CsvViewData::rename_column(const std::string& column, const std::string& name) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (name.empty()) {
    throw std::runtime_error("column name is required");
  }
  if (!materialized->frame->has_column(column)) {
    throw std::runtime_error("unknown column: " + column);
  }
  if (column == name) {
    return;
  }
  if (materialized->frame->has_column(name)) {
    throw std::runtime_error("column already exists: " + name);
  }

  const auto column_index = static_cast<std::size_t>(materialized->frame->index_of(column));
  std::vector<std::string> values;
  values.reserve(materialized->frame->n_rows());
  for (std::size_t row = 0; row < materialized->frame->n_rows(); ++row) {
    values.emplace_back(materialized->frame->at(row)[column]);
  }

  materialized->frame->insert_column(column_index, name, "");
  for (std::size_t row = 0; row < materialized->frame->n_rows(); ++row) {
    materialized->frame->at(row)[name] = values[row];
  }
  if (!materialized->frame->column_view(column).erase()) {
    throw std::runtime_error("failed to rename column: " + column);
  }
}

void CsvViewData::delete_column(const std::string& column) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (materialized->frame->n_cols() <= 1) {
    throw std::runtime_error("cannot delete the last column");
  }
  if (!materialized->frame->has_column(column)) {
    throw std::runtime_error("unknown column: " + column);
  }
  if (!materialized->frame->column_view(column).erase()) {
    throw std::runtime_error("failed to delete column: " + column);
  }
}

bool CsvViewData::recover_renamed_source() {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    return false;
  }
  return RecoverRenamedMaterializedSource(*materialized);
}

void CsvViewData::reset() {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("reset requires materialized view mode");
  }
  RecoverRenamedMaterializedSource(*materialized);
  ReloadMaterializedFile(*materialized);
}

void CsvViewData::save(const std::vector<std::string>& columns) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("saving requires materialized view mode");
  }
  ValidateColumnOrder(*materialized->frame, columns);

  RecoverRenamedMaterializedSource(*materialized);
  if (GetFileSize(materialized->input_path) != materialized->source_size ||
      GetFileMtime(materialized->input_path) != materialized->source_mtime) {
    throw std::runtime_error("source file changed externally; reload before saving");
  }

  const auto target = std::filesystem::path(materialized->input_path);
  const auto temp = target.parent_path() /
      (target.filename().string() + ".csvzall-save-" + GenerateSessionToken() + ".tmp");
  try {
    {
      std::ofstream output(temp, std::ios::binary);
      if (!output) {
        throw std::runtime_error("unable to open temporary save file: " + temp.string());
      }
      auto writer = csv::make_csv_writer(output).set_auto_flush(false);
      const auto& save_columns = columns.empty() ? materialized->frame->columns() : columns;
      writer << save_columns;
      for (const auto& row : *materialized->frame) {
        if (columns.empty()) {
          writer << row;
        } else {
          auto reordered = save_columns | std::views::transform([&row](const std::string& column) {
            return row[column];
          });
          writer << reordered;
        }
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
    if (columns.empty()) {
      materialized->source_size = GetFileSize(materialized->input_path);
      materialized->source_mtime = GetFileMtime(materialized->input_path);
    } else {
      ReloadMaterializedFile(*materialized);
    }
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    throw;
  }
}
}  // namespace csvzall::pipeline::commands

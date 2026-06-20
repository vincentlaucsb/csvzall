#include "../view.hpp"

#include "support.hpp"

#include "../../common/gzip_stream.hpp"

#include <stdexcept>
#include <utility>

namespace csvzall::pipeline::commands {

namespace {

void ValidatePlainLocalViewInput(const std::string& input_path, const RunOptions& options) {
  if (input_path.empty() || input_path == "-") {
    throw std::runtime_error("stdin is not supported; pass a plain local CSV file path");
  }
  if (!options.zip_entry.empty() || common::IsZipPath(input_path) || common::IsGzipPath(input_path)) {
    throw std::runtime_error("view currently supports plain local CSV files only");
  }
}

}  // namespace

CsvIndexedFile CsvIndexedFile::Open(const std::string& input_path,
                                    const RunOptions& options,
                                    const LoggerCallbacks& logger,
                                    RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);
  auto indexed = ::csvzall::viewer::CsvIndexedFile::Open(
      input_path, view_internal::MakeViewFormat(options), logger.verbose);
  stats.rows_processed = indexed.row_count();
  stats.bytes_processed = indexed.file_size();
  return CsvIndexedFile(std::move(indexed));
}

CsvIndexedFile::CsvIndexedFile(::csvzall::viewer::CsvIndexedFile indexed)
    : data_(std::move(indexed)) {}

const std::string& CsvIndexedFile::input_path() const {
  return data_.input_path();
}

const std::string& CsvIndexedFile::file_name() const {
  return data_.file_name();
}

const std::vector<std::string>& CsvIndexedFile::headers() const {
  return data_.headers();
}

std::uint64_t CsvIndexedFile::row_count() const {
  return data_.row_count();
}

std::uint64_t CsvIndexedFile::file_size() const {
  return data_.file_size();
}

std::vector<std::vector<std::string>> CsvIndexedFile::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  return data_.read_rows(offset, limit);
}

void CsvIndexedFile::edit_cell(const std::uint64_t row,
                               const std::string& column,
                               const std::string& value) {
  data_.edit_cell(row, column, value);
}

void CsvIndexedFile::delete_row(const std::uint64_t row) {
  data_.delete_row(row);
}

void CsvIndexedFile::insert_row(const std::uint64_t row, const std::vector<std::string>& values) {
  data_.insert_row(row, values);
}

void CsvIndexedFile::swap_rows(const std::uint64_t first, const std::uint64_t second) {
  data_.swap_rows(first, second);
}

void CsvIndexedFile::insert_column(const std::uint64_t column,
                                   const std::string& name,
                                   const std::string& value) {
  data_.insert_column(column, name, value);
}

void CsvIndexedFile::rename_column(const std::string& column, const std::string& name) {
  data_.rename_column(column, name);
}

void CsvIndexedFile::delete_column(const std::string& column) {
  data_.delete_column(column);
}

bool CsvIndexedFile::recover_renamed_source() {
  return data_.recover_renamed_source();
}

void CsvIndexedFile::reset() {
  data_.reset();
}

void CsvIndexedFile::save(const std::vector<std::string>& columns) {
  data_.save(columns);
}

CsvViewData CsvViewData::Open(const std::string& input_path,
                              const RunOptions& options,
                              const LoggerCallbacks& logger,
                              RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);
  auto data = ::csvzall::viewer::CsvViewData::Open(
      input_path, view_internal::MakeViewFormat(options), logger.verbose);
  stats.rows_processed = data.row_count();
  stats.bytes_processed = data.file_size();
  return CsvViewData(std::move(data));
}

CsvViewData::CsvViewData(::csvzall::viewer::CsvViewData data)
    : data_(std::move(data)) {}

CsvViewDataMode CsvViewData::mode() const {
  return data_.mode();
}

std::string_view CsvViewData::mode_name() const {
  return data_.mode_name();
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

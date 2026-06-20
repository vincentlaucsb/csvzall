#include "../viewer_core/indexed_csv.hpp"

#include <csv.hpp>
#include <emscripten/bind.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using CsvRow = std::vector<std::string>;
using CsvRows = std::vector<CsvRow>;
using CsvViewData = csvzall::viewer::CsvViewData;

csv::CSVFormat DefaultViewerFormat() {
  csv::CSVFormat format;
  format.delimiter({',', '|', '\t', ';', '^'}).quote('"').header_row(0);
  return format;
}

std::uint64_t ToRowIndex(const double value) {
  if (!std::isfinite(value) || value < 0 ||
      value > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    throw std::runtime_error("row index out of bounds");
  }
  return static_cast<std::uint64_t>(value);
}

CsvViewData OpenCsvViewData(const std::string& path) {
  return CsvViewData::Open(path, DefaultViewerFormat());
}

double RowCount(const CsvViewData& data) {
  return static_cast<double>(data.row_count());
}

double FileSize(const CsvViewData& data) {
  return static_cast<double>(data.file_size());
}

std::string ModeName(const CsvViewData& data) {
  return std::string(data.mode_name());
}

std::string InputPath(const CsvViewData& data) {
  return data.input_path();
}

std::string FileName(const CsvViewData& data) {
  return data.file_name();
}

std::vector<std::string> Headers(const CsvViewData& data) {
  return data.headers();
}

CsvRows ReadRows(const CsvViewData& data, const double offset, const double limit) {
  return data.read_rows(ToRowIndex(offset), ToRowIndex(limit));
}

void EditCell(CsvViewData& data,
              const double row,
              const std::string& column,
              const std::string& value) {
  data.edit_cell(ToRowIndex(row), column, value);
}

void DeleteRow(CsvViewData& data, const double row) {
  data.delete_row(ToRowIndex(row));
}

void InsertRow(CsvViewData& data, const double row, const CsvRow& values) {
  data.insert_row(ToRowIndex(row), values);
}

void SwapRows(CsvViewData& data, const double first, const double second) {
  data.swap_rows(ToRowIndex(first), ToRowIndex(second));
}

void InsertColumn(CsvViewData& data,
                  const double column,
                  const std::string& name,
                  const std::string& value) {
  data.insert_column(ToRowIndex(column), name, value);
}

void Save(CsvViewData& data) {
  data.save();
}

void SaveWithColumns(CsvViewData& data, const std::vector<std::string>& columns) {
  data.save(columns);
}

}  // namespace

EMSCRIPTEN_BINDINGS(csvzall_viewer_wasm) {
  emscripten::register_vector<std::string>("StringList");
  emscripten::register_vector<CsvRow>("CsvRow");
  emscripten::register_vector<CsvRows>("CsvRows");

  emscripten::class_<CsvViewData>("CsvViewData")
      .class_function("open", &OpenCsvViewData)
      .function("modeName", &ModeName)
      .function("inputPath", &InputPath)
      .function("fileName", &FileName)
      .function("headers", &Headers)
      .function("rowCount", &RowCount)
      .function("fileSize", &FileSize)
      .function("readRows", &ReadRows)
      .function("editCell", &EditCell)
      .function("deleteRow", &DeleteRow)
      .function("insertRow", &InsertRow)
      .function("swapRows", &SwapRows)
      .function("insertColumn", &InsertColumn)
      .function("renameColumn", &CsvViewData::rename_column)
      .function("deleteColumn", &CsvViewData::delete_column)
      .function("reset", &CsvViewData::reset)
      .function("save", &Save)
      .function("saveWithColumns", &SaveWithColumns);
}

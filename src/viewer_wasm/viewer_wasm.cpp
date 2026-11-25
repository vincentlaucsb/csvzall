#include "../viewer_core/indexed_csv.hpp"

#include <csv.hpp>
#include <emscripten/bind.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
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

std::string Delimiter(const CsvViewData& data) { return data.delimiter(); }
std::string Quote(const CsvViewData& data) { return data.quote(); }
bool HasHeaders(const CsvViewData& data) { return data.has_headers(); }
std::string HeadersDisabledReason(const CsvViewData& data) { return data.headers_disabled_reason(); }
bool HasBom(const CsvViewData& data) { return data.has_bom(); }
void SetCsvOptions(CsvViewData& data, const std::string& delimiter,
                   const std::string& quote, bool has_headers) {
  data.set_csv_options(delimiter, quote, has_headers);
}
void ReadCsvOptions(CsvViewData& data, const std::string& delimiter,
                    const std::string& quote, bool has_headers) {
  data.read_csv_options(delimiter, quote, has_headers);
}

std::vector<std::string> Headers(const CsvViewData& data) {
  return data.headers();
}

CsvRows ReadRows(const CsvViewData& data, const double offset, const double limit) {
  return data.read_rows(ToRowIndex(offset), ToRowIndex(limit));
}

template <typename RowId>
emscripten::val ReadChanges(const CsvViewData& data, std::uint64_t count, RowId row_id_at) {
  auto changes = emscripten::val::array();
  unsigned slot = 0;
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto row_id = row_id_at(i);
    const auto row = data.row_changes(row_id);
    if (!row.inserted && row.columns.empty()) continue;
    auto change = emscripten::val::object();
    change.set("rowId", static_cast<double>(row_id));
    change.set("inserted", row.inserted);
    change.set("columns", emscripten::val::array(row.columns));
    changes.set(slot++, change);
  }
  return changes;
}

emscripten::val ReadRowChanges(const CsvViewData& data, double offset, double count) {
  const auto start = ToRowIndex(offset);
  return ReadChanges(data, ToRowIndex(count), [start](auto i) { return start + i; });
}

struct SortedRows {
  std::vector<double> row_ids;
  CsvRows rows;
  emscripten::val changes;
};

SortedRows ReadSortedRows(const CsvViewData& data, const double offset, const double limit,
                          const std::string& column, bool descending) {
  auto page = data.read_sorted_rows(ToRowIndex(offset), ToRowIndex(limit), column, descending);
  auto changes = ReadChanges(data, page.row_ids.size(), [&](auto i) { return page.row_ids[i]; });
  return {{page.row_ids.begin(), page.row_ids.end()}, std::move(page.rows), std::move(changes)};
}

bool EditCell(CsvViewData& data,
              const double row,
              const std::string& column,
              const std::string& value) {
  return data.edit_cell(ToRowIndex(row), column, value);
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
  emscripten::register_vector<double>("RowIds");
  emscripten::value_object<SortedRows>("SortedRows")
      .field("rowIds", &SortedRows::row_ids)
      .field("rows", &SortedRows::rows)
      .field("changes", &SortedRows::changes);

  emscripten::class_<CsvViewData>("CsvViewData")
      .class_function("open", &OpenCsvViewData)
      .function("modeName", &ModeName)
      .function("inputPath", &InputPath)
      .function("fileName", &FileName)
      .function("headers", &Headers)
      .function("delimiter", &Delimiter)
      .function("quote", &Quote)
      .function("hasHeaders", &HasHeaders)
      .function("headersDisabledReason", &HeadersDisabledReason)
      .function("hasBom", &HasBom)
      .function("setCsvOptions", &SetCsvOptions)
      .function("readCsvOptions", &ReadCsvOptions)
      .function("rowCount", &RowCount)
      .function("fileSize", &FileSize)
      .function("readRows", &ReadRows)
      .function("readRowChanges", &ReadRowChanges)
      .function("readSortedRows", &ReadSortedRows)
      .function("editCell", &EditCell)
      .function("deleteRow", &DeleteRow)
      .function("insertRow", &InsertRow)
      .function("swapRows", &SwapRows)
      .function("insertColumn", &InsertColumn)
      .function("renameColumn", &CsvViewData::rename_column)
      .function("deleteColumn", &CsvViewData::delete_column)
      .function("reset", &CsvViewData::reset)
      .function("writeSaveFile", &CsvViewData::write_save_file)
      .function("commitSaveFile", &CsvViewData::commit_save_file)
      .function("save", &Save)
      .function("saveWithColumns", &SaveWithColumns);
}

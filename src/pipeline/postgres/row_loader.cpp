#include "row_loader.hpp"

#ifdef CSVZALL_HAVE_POSTGRESQL

#include "postgres_connection.hpp"

#include <csv.hpp>

#include <stdexcept>

namespace csvzall::pipeline::postgres {

namespace {

bool IsCsvNull(std::string_view value) {
  for (const char ch : value) {
    if (ch != ' ') {
      return false;
    }
  }
  return true;
}

}  // namespace

RowLoader::RowLoader(pqxx::connection& connection,
                     pqxx::work& transaction,
                     const std::string& table_name,
                     const std::vector<InferredColumn>& columns)
    : connection_(connection),
      transaction_(transaction),
      table_name_(table_name),
      columns_(columns) {
  column_names_.reserve(columns_.size());
  for (const auto& column : columns_) {
    column_names_.push_back(column.name);
  }

  open_stream();
}

void RowLoader::add_row(const csv::CSVRow& row) {
  if (completed_) {
    throw std::logic_error("cannot add rows after RowLoader::flush()");
  }

  if (row.size() != columns_.size()) {
    rows_skipped_type_mismatch_++;
    return;
  }

  std::vector<PgValue> values;
  values.reserve(columns_.size());
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    append_value(row, i, values);
  }

  write_values(values);
}

void RowLoader::add_row(const std::vector<std::string>& row) {
  if (completed_) {
    throw std::logic_error("cannot add rows after RowLoader::flush()");
  }

  if (row.size() != columns_.size()) {
    rows_skipped_type_mismatch_++;
    return;
  }

  std::vector<PgValue> values;
  values.reserve(columns_.size());
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    append_value(row[i], values);
  }

  write_values(values);
}

void RowLoader::add_rows(const std::vector<csv::CSVRow>& rows) {
  if (completed_) {
    throw std::logic_error("cannot add rows after RowLoader::flush()");
  }

  if (rows.empty()) {
    return;
  }

  std::vector<PgValue> row_values;
  row_values.reserve(columns_.size());

  for (const auto& row : rows) {
    row_values.clear();
    for (std::size_t column_index = 0; column_index < columns_.size(); ++column_index) {
      append_value(row, column_index, row_values);
    }

    write_values(row_values);
  }
}

void RowLoader::flush() {
  if (completed_) {
    return;
  }

  stream_->complete();
  stream_.reset();
  completed_ = true;
}

void RowLoader::open_stream() {
  stream_ = std::make_unique<pqxx::stream_to>(
      pqxx::stream_to::raw_table(
          transaction_,
          QuoteIdentifier(table_name_),
          connection_.quote_columns(column_names_)));
}

void RowLoader::append_value(const csv::CSVRow& row,
                             std::size_t index,
                             std::vector<PgValue>& values) const {
  auto field = row[index];
  const std::string_view value{field.get_sv()};
  if (IsCsvNull(value)) {
    values.emplace_back(std::nullopt);
    return;
  }

  values.emplace_back(value);
}

void RowLoader::append_value(std::string_view value,
                             std::vector<PgValue>& values) const {
  if (IsCsvNull(value)) {
    values.emplace_back(std::nullopt);
    return;
  }

  values.emplace_back(value);
}

void RowLoader::write_values(std::vector<PgValue>& values) {
  stream_->write_row(values);
  rows_loaded_++;
}

RowLoader::~RowLoader() {
  stream_.reset();
}

}  // namespace csvzall::pipeline::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

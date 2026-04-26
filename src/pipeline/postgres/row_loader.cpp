#include "row_loader.hpp"

#ifdef CSVZALL_HAVE_POSTGRESQL

#include "postgres_connection.hpp"

#include <csv.hpp>

#include <charconv>
#include <stdexcept>

namespace csvzall::pipeline::postgres {

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
    if (!append_cast_value(row, i, values)) {
      rows_skipped_type_mismatch_++;
      return;
    }
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
    if (!append_cast_value(row[i], i, values)) {
      rows_skipped_type_mismatch_++;
      return;
    }
  }

  write_values(values);
}

void RowLoader::flush() {
  if (completed_) {
    return;
  }

  stream_->complete();
  stream_.reset();
  completed_ = true;
}

bool RowLoader::try_parse_int64(std::string_view s, std::int64_t& out) {
  if (s.empty()) {
    return false;
  }
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  const auto result = std::from_chars(begin, end, out);
  return result.ec == std::errc{} && result.ptr == end;
}

void RowLoader::open_stream() {
  stream_ = std::make_unique<pqxx::stream_to>(
      pqxx::stream_to::raw_table(
          transaction_,
          QuoteIdentifier(table_name_),
          connection_.quote_columns(column_names_)));
}

bool RowLoader::append_cast_value(const csv::CSVRow& row,
                                  std::size_t index,
                                  std::vector<PgValue>& values) const {
  auto field = row[index];
  if (field.is_null()) {
    values.emplace_back(std::optional<std::string_view>{});
    return true;
  }

  switch (columns_[index].type) {
    case ColumnType::BIGINT:
    case ColumnType::INTEGER: {
      std::int64_t value = 0;
      if (!field.try_get(value)) {
        return false;
      }
      values.emplace_back(std::optional<std::int64_t>{value});
      return true;
    }

    case ColumnType::NUMERIC: {
      long double value = 0;
      if (!field.try_get(value)) {
        return false;
      }
      values.emplace_back(std::optional<long double>{value});
      return true;
    }

    case ColumnType::TIMESTAMP:
    case ColumnType::TEXT:
      values.emplace_back(std::optional<std::string_view>{std::string_view{field.get_sv()}});
      return true;
  }

  return false;
}

bool RowLoader::append_cast_value(std::string_view value,
                                  std::size_t index,
                                  std::vector<PgValue>& values) const {
  if (csv::internals::data_type(value) == csv::DataType::CSV_NULL) {
    values.emplace_back(std::optional<std::string_view>{});
    return true;
  }

  switch (columns_[index].type) {
    case ColumnType::BIGINT:
    case ColumnType::INTEGER: {
      std::int64_t parsed = 0;
      if (!try_parse_int64(value, parsed)) {
        return false;
      }
      values.emplace_back(std::optional<std::int64_t>{parsed});
      return true;
    }

    case ColumnType::NUMERIC: {
      long double parsed = 0;
      if (csv::internals::data_type(value, &parsed) <= csv::DataType::CSV_STRING) {
        return false;
      }
      values.emplace_back(std::optional<long double>{parsed});
      return true;
    }

    case ColumnType::TIMESTAMP:
    case ColumnType::TEXT:
      values.emplace_back(std::optional<std::string_view>{value});
      return true;
  }

  return false;
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

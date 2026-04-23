#include "row_loader.hpp"

#ifdef CSVZALL_HAVE_POSTGRESQL

#include "postgres_connection.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace csvzall::pipeline::postgres {

namespace {

// Case-insensitive string match
bool contains_ci(const std::string& haystack, const std::string& needle) {
  std::string lower_haystack = haystack;
  std::string lower_needle = needle;

  std::transform(lower_haystack.begin(), lower_haystack.end(),
                 lower_haystack.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::transform(lower_needle.begin(), lower_needle.end(),
                 lower_needle.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  return lower_haystack.find(lower_needle) != std::string::npos;
}

}  // namespace

RowLoader::RowLoader(pqxx::connection& connection,
                     const std::string& table_name,
                     const std::vector<InferredColumn>& columns,
                     const RowLoaderConfig& config)
    : connection_(connection),
      table_name_(table_name),
      columns_(columns),
      config_(config),
      prepared_name_("csvzall_pg_insert") {
  prepare_insert_statement();
}

void RowLoader::add_row(const std::vector<std::string>& row) {
  if (row.size() != columns_.size()) {
    rows_skipped_type_mismatch_++;
    return;
  }

  if (!should_load_row(row)) {
    rows_skipped_filtered_++;
    return;
  }

  pending_rows_.push_back(row);

  if (pending_rows_.size() >= batch_size_) {
    flush();
  }
}

void RowLoader::flush() {
  if (pending_rows_.empty()) {
    return;
  }

  flush_pending_lenient();
  pending_rows_.clear();
}

int RowLoader::find_column_index(const std::string& pattern) const {
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (contains_ci(columns_[i].name, pattern)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool RowLoader::try_parse_int64(const std::string& s, std::int64_t& out) {
  if (s.empty()) {
    return false;
  }
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  const auto result = std::from_chars(begin, end, out);
  return result.ec == std::errc{} && result.ptr == end;
}

bool RowLoader::should_load_row(const std::vector<std::string>& row) const {
  const int price_idx = find_column_index(config_.price_column);
  const int odometer_idx = find_column_index(config_.odometer_column);

  if (config_.price_min >= 0 || config_.price_max >= 0) {
    if (price_idx >= 0 && price_idx < static_cast<int>(row.size())) {
      const auto& price_str = row[price_idx];
      if (!price_str.empty()) {
        std::int64_t price = 0;
        if (try_parse_int64(price_str, price)) {
          if (config_.price_min >= 0 && price < config_.price_min) {
            return false;
          }
          if (config_.price_max >= 0 && price > config_.price_max) {
            return false;
          }
        }
      }
    }
  }

  if (odometer_idx >= 0 && odometer_idx < static_cast<int>(row.size())) {
    const auto& odometer_str = row[odometer_idx];

    // Requirement: skip rows where odometer is null.
    if (odometer_str.empty()) {
      return false;
    }

    if (config_.odometer_max >= 0) {
      std::int64_t odometer = 0;
      if (try_parse_int64(odometer_str, odometer) && odometer > config_.odometer_max) {
        return false;
      }
    }
  }

  return true;
}

void RowLoader::prepare_insert_statement() {
  std::ostringstream col_list;
  std::ostringstream val_list;

  for (size_t i = 0; i < columns_.size(); ++i) {
    if (i > 0) {
      col_list << ", ";
      val_list << ", ";
    }
    col_list << QuoteIdentifier(columns_[i].name);
    // NULLIF allows empty CSV fields to map to SQL NULL without special-case binding.
    val_list << "NULLIF($" << (i + 1) << ", '')";
  }

  const std::string sql = "INSERT INTO " + QuoteIdentifier(table_name_) +
                          " (" + col_list.str() + ") VALUES (" + val_list.str() + ")";

  connection_.prepare(prepared_name_, sql);
}

void RowLoader::insert_one_lenient(const std::vector<std::string>& row) {
  try {
    pqxx::work tx(connection_);
    auto invocation = tx.prepared(prepared_name_);
    for (const auto& value : row) {
      invocation(value);
    }
    invocation.exec();
    tx.commit();
    rows_loaded_++;
  } catch (const pqxx::sql_error&) {
    rows_skipped_type_mismatch_++;
  } catch (const std::exception&) {
    rows_skipped_type_mismatch_++;
  }
}

void RowLoader::flush_pending_lenient() {
  for (const auto& row : pending_rows_) {
    insert_one_lenient(row);
  }
}

RowLoader::~RowLoader() {
  try {
    flush();
  } catch (...) {
    // Suppress exceptions in destructor
  }
}

}  // namespace csvzall::pipeline::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

#pragma once

#ifdef CSVZALL_HAVE_POSTGRESQL

#include <pqxx/pqxx>

#include <string>
#include <vector>
#include <cstdint>

#include "schema_inference.hpp"

namespace csvzall::pipeline::postgres {

// Data cleaning and row loading configuration
struct RowLoaderConfig {
  // Price range filter: drop rows outside this range
  // Set to -1 to disable
  int64_t price_min = 500;
  int64_t price_max = 500000;

  // Odometer limit: drop rows where odometer > this value
  // Set to -1 to disable
  int64_t odometer_max = 500000;

  // Column names to check (case-insensitive approximate match)
  std::string price_column = "price";
  std::string odometer_column = "odometer";
};

// Batch loader state
class RowLoader {
 public:
  RowLoader(pqxx::connection& connection,
            const std::string& table_name,
            const std::vector<InferredColumn>& columns,
            const RowLoaderConfig& config = RowLoaderConfig());

  // Adds a row. Rows filtered by cleaning rules are skipped, not inserted.
  void add_row(const std::vector<std::string>& row);

  void flush();

  [[nodiscard]] std::uint64_t rows_loaded() const { return rows_loaded_; }
  [[nodiscard]] std::uint64_t rows_skipped_filtered() const { return rows_skipped_filtered_; }
  [[nodiscard]] std::uint64_t rows_skipped_type_mismatch() const { return rows_skipped_type_mismatch_; }

  ~RowLoader();

 private:
  pqxx::connection& connection_;
  std::string table_name_;
  std::vector<InferredColumn> columns_;
  RowLoaderConfig config_;
  std::string prepared_name_;

  std::vector<std::vector<std::string>> pending_rows_;
  static constexpr size_t batch_size_ = 1000;

  std::uint64_t rows_loaded_ = 0;
  std::uint64_t rows_skipped_filtered_ = 0;
  std::uint64_t rows_skipped_type_mismatch_ = 0;

  int find_column_index(const std::string& pattern) const;
  void prepare_insert_statement();
  void flush_pending_lenient();
  void insert_one_lenient(const std::vector<std::string>& row);

  bool should_load_row(const std::vector<std::string>& row) const;
  static bool try_parse_int64(const std::string& s, std::int64_t& out);
};

}  // namespace csvzall::pipeline::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

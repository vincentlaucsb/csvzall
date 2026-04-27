#pragma once

#ifdef CSVZALL_HAVE_POSTGRESQL

#include <pqxx/pqxx>
#include <csv.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "schema_inference.hpp"

namespace csvzall::pipeline::postgres {

// Batch loader state
class RowLoader {
 public:
  using PgValue = std::optional<std::string_view>;

  RowLoader(pqxx::connection& connection,
            pqxx::work& transaction,
            const std::string& table_name,
            const std::vector<InferredColumn>& columns);

  // Adds rows to the COPY stream.
  void add_row(const csv::CSVRow& row);
  void add_row(const std::vector<std::string>& row);
  void add_rows(const std::vector<csv::CSVRow>& rows);

  void flush();

  [[nodiscard]] std::uint64_t rows_loaded() const { return rows_loaded_; }
  [[nodiscard]] std::uint64_t rows_skipped_type_mismatch() const { return rows_skipped_type_mismatch_; }

  ~RowLoader();

 private:
  pqxx::connection& connection_;
  pqxx::work& transaction_;
  std::string table_name_;
  std::vector<InferredColumn> columns_;
  std::vector<std::string> column_names_;
  std::unique_ptr<pqxx::stream_to> stream_;
  bool completed_ = false;

  std::uint64_t rows_loaded_ = 0;
  std::uint64_t rows_skipped_type_mismatch_ = 0;

  void open_stream();

  void append_value(const csv::CSVRow& row, std::size_t index, std::vector<PgValue>& values) const;
  void append_value(std::string_view value, std::vector<PgValue>& values) const;
  void write_values(std::vector<PgValue>& values);
};

}  // namespace csvzall::pipeline::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

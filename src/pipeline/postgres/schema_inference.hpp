#pragma once

#ifdef CSVZALL_HAVE_POSTGRESQL

#include <string>
#include <vector>

namespace csv {
class CSVRow;
enum class DataType;
}

namespace csvzall::pipeline::postgres {

// PostgreSQL column type
enum class ColumnType {
  INTEGER,
  BIGINT,
  NUMERIC,
  TIMESTAMP,
  TEXT
};

// Get PostgreSQL type name
std::string ColumnTypeToString(ColumnType type);

// Represents a single inferred column
struct InferredColumn {
  std::string name;
  ColumnType type;
  bool nullable = true;
};

// Schema inference state
class SchemaInference {
 public:
  struct ColumnStats {
    std::string name;
    int integer_count = 0;
    int bigint_count = 0;
    int numeric_count = 0;
    int text_count = 0;
    int total_non_null = 0;
    int null_count = 0;
  };

  explicit SchemaInference(const std::vector<std::string>& headers);

  // Add a row and update type statistics
  void observe_row(const csv::CSVRow& row);
  void observe_row(const std::vector<std::string>& row);

  // Finalize inference and return inferred columns
  std::vector<InferredColumn> finalize();
  static std::vector<InferredColumn> finalize_stats(const std::vector<ColumnStats>& stats);
  static void observe_type(ColumnStats& stats, csv::DataType type);

  // Check if we've observed enough rows to stop inference
  bool has_enough_samples() const { return samples_ >= max_samples_; }

 private:
  static constexpr int max_samples_ = 1000;
  int samples_ = 0;

  std::vector<ColumnStats> column_stats_;

  // Decide final type for a column
  static ColumnType infer_type(const ColumnStats& stats);
};

}  // namespace csvzall::pipeline::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

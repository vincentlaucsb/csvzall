#pragma once

#ifdef CSVZALL_HAVE_POSTGRESQL

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace csvzall::pipeline::postgres {

// PostgreSQL column type
enum class ColumnType {
  INTEGER,
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
  explicit SchemaInference(const std::vector<std::string>& headers);

  // Add a row and update type statistics
  void observe_row(const std::vector<std::string>& row);

  // Finalize inference and return inferred columns
  std::vector<InferredColumn> finalize();

  // Check if we've observed enough rows to stop inference
  bool has_enough_samples() const { return samples_ >= max_samples_; }

 private:
  static constexpr int max_samples_ = 1000;
  int samples_ = 0;

  struct ColumnStats {
    std::string name;
    int integer_count = 0;
    int float_count = 0;
    int timestamp_count = 0;
    int text_count = 0;
    int total_non_null = 0;
    int null_count = 0;
  };

  std::vector<ColumnStats> column_stats_;

  // Type detection helpers
  bool is_integer(const std::string& value);
  bool is_float(const std::string& value);
  bool is_timestamp(const std::string& value);

  // Classify common column names
  bool looks_like_integer_column(const std::string& name);
  bool looks_like_numeric_column(const std::string& name);
  bool looks_like_timestamp_column(const std::string& name);

  // Decide final type for a column
  ColumnType infer_type(const ColumnStats& stats);
};

}  // namespace csvzall::pipeline::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

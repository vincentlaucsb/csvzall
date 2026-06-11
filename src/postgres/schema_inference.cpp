#include "schema_inference.hpp"

#ifdef CSVZALL_HAVE_POSTGRESQL

#include <csv.hpp>

namespace csvzall::postgres {

std::string ColumnTypeToString(ColumnType type) {
  switch (type) {
    case ColumnType::INTEGER:
      return "INTEGER";
    case ColumnType::BIGINT:
      return "BIGINT";
    case ColumnType::NUMERIC:
      return "NUMERIC";
    case ColumnType::TIMESTAMP:
      return "TIMESTAMP";
    case ColumnType::TEXT:
    default:
      return "TEXT";
  }
}

SchemaInference::SchemaInference(const std::vector<std::string>& headers) {
  for (const auto& header : headers) {
    ColumnStats stats;
    stats.name = header;
    column_stats_.push_back(stats);
  }
}

void SchemaInference::observe_row(const std::vector<std::string>& row) {
  if (samples_ >= max_samples_) return;
  samples_++;

  for (size_t i = 0; i < row.size() && i < column_stats_.size(); ++i) {
    observe_type(column_stats_[i], csv::internals::data_type(row[i]));
  }
}

void SchemaInference::observe_row(const csv::CSVRow& row) {
  if (samples_ >= max_samples_) return;
  samples_++;

  for (size_t i = 0; i < row.size() && i < column_stats_.size(); ++i) {
    auto field = row[i];
    observe_type(column_stats_[i], field.type());
  }
}

std::vector<InferredColumn> SchemaInference::finalize() {
  return finalize_stats(column_stats_);
}

std::vector<InferredColumn> SchemaInference::finalize_stats(const std::vector<ColumnStats>& stats_list) {
  std::vector<InferredColumn> result;

  for (const auto& stats : stats_list) {
    InferredColumn col;
    col.name = stats.name;
    col.type = infer_type(stats);
    col.nullable = (stats.null_count > 0);
    result.push_back(col);
  }

  return result;
}

void SchemaInference::observe_type(ColumnStats& stats, const csv::DataType type) {
  if (type == csv::DataType::CSV_NULL) {
    stats.null_count++;
    return;
  }

  stats.total_non_null++;

  if (type >= csv::DataType::CSV_INT8 && type <= csv::DataType::CSV_INT32) {
    stats.integer_count++;
  } else if (type == csv::DataType::CSV_INT64) {
    stats.bigint_count++;
  } else if (type == csv::DataType::CSV_BIGINT || type == csv::DataType::CSV_DOUBLE) {
    stats.numeric_count++;
  } else {
    stats.text_count++;
  }
}

ColumnType SchemaInference::infer_type(const ColumnStats& stats) {
  // If no data observed, default to TEXT
  if (stats.total_non_null == 0) {
    return ColumnType::TEXT;
  }

  if (stats.text_count > 0) {
    return ColumnType::TEXT;
  }

  if (stats.integer_count > 0 && stats.bigint_count == 0 && stats.numeric_count == 0) {
    return ColumnType::INTEGER;
  }

  if (stats.bigint_count > 0 && stats.numeric_count == 0) {
    return ColumnType::BIGINT;
  }

  if (stats.integer_count > 0 || stats.bigint_count > 0 || stats.numeric_count > 0) {
    return ColumnType::NUMERIC;
  }

  // Fallback
  return ColumnType::TEXT;
}

}  // namespace csvzall::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

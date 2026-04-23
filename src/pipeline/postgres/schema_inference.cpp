#include "schema_inference.hpp"

#ifdef CSVZALL_HAVE_POSTGRESQL

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace csvzall::pipeline::postgres {

std::string ColumnTypeToString(ColumnType type) {
  switch (type) {
    case ColumnType::INTEGER:
      return "INTEGER";
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
    const auto& value = row[i];
    auto& stats = column_stats_[i];

    if (value.empty()) {
      stats.null_count++;
      continue;
    }

    stats.total_non_null++;

    // Try to classify the value in order of specificity
    if (is_integer(value)) {
      stats.integer_count++;
    } else if (is_float(value)) {
      stats.float_count++;
    } else if (is_timestamp(value)) {
      stats.timestamp_count++;
    } else {
      stats.text_count++;
    }
  }
}

std::vector<InferredColumn> SchemaInference::finalize() {
  std::vector<InferredColumn> result;

  for (const auto& stats : column_stats_) {
    InferredColumn col;
    col.name = stats.name;
    col.type = infer_type(stats);
    col.nullable = (stats.null_count > 0);
    result.push_back(col);
  }

  return result;
}

bool SchemaInference::is_integer(const std::string& value) {
  if (value.empty()) return false;

  size_t start = 0;
  if (value[0] == '-' || value[0] == '+') {
    start = 1;
  }

  if (start >= value.size()) return false;

  for (size_t i = start; i < value.size(); ++i) {
    if (!std::isdigit(value[i])) {
      return false;
    }
  }

  return true;
}

bool SchemaInference::is_float(const std::string& value) {
  if (value.empty()) return false;

  // Simple heuristic: has a decimal point and digits on both sides
  static const std::regex float_pattern(R"(^[+-]?(\d+\.?\d*|\d*\.\d+)([eE][+-]?\d+)?$)");
  return std::regex_match(value, float_pattern);
}

bool SchemaInference::is_timestamp(const std::string& value) {
  if (value.empty()) return false;

  // Detect ISO 8601 patterns
  // YYYY-MM-DD, YYYY-MM-DD HH:MM:SS, YYYY-MM-DDTHH:MM:SS, etc.
  static const std::regex timestamp_pattern(
    R"(^\d{4}-\d{2}-\d{2}([ T]\d{2}:\d{2}:\d{2}(\.\d+)?)?([+-]\d{2}:\d{2}|Z)?$)");
  return std::regex_match(value, timestamp_pattern);
}

bool SchemaInference::looks_like_integer_column(const std::string& name) {
  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  const std::vector<std::string> patterns = {
    "id", "year", "count", "quantity", "odometer", "age", "number", "_num"};

  for (const auto& pattern : patterns) {
    if (lower_name.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool SchemaInference::looks_like_numeric_column(const std::string& name) {
  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  const std::vector<std::string> patterns = {
    "price", "amount", "cost", "revenue", "salary", "rate", "percentage", "_pct"};

  for (const auto& pattern : patterns) {
    if (lower_name.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool SchemaInference::looks_like_timestamp_column(const std::string& name) {
  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  const std::vector<std::string> patterns = {
    "date", "time", "created", "updated", "timestamp", "when", "_at"};

  for (const auto& pattern : patterns) {
    if (lower_name.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

ColumnType SchemaInference::infer_type(const ColumnStats& stats) {
  // If >20% nulls or total_non_null is 0, default to TEXT
  int total_observed = stats.null_count + stats.total_non_null;
  if (total_observed > 0) {
    int null_pct = (stats.null_count * 100) / total_observed;
    if (null_pct > 20) {
      return ColumnType::TEXT;
    }
  }

  // If no data observed, default to TEXT
  if (stats.total_non_null == 0) {
    return ColumnType::TEXT;
  }

  // Check for mixed types — if we see multiple types, default to TEXT
  int type_count = 0;
  if (stats.integer_count > 0) type_count++;
  if (stats.float_count > 0) type_count++;
  if (stats.timestamp_count > 0) type_count++;
  if (stats.text_count > 0) type_count++;

  if (type_count > 1) {
    // Mixed types detected — check column name heuristics
    if (looks_like_timestamp_column(stats.name)) {
      return ColumnType::TIMESTAMP;
    }
    if (looks_like_numeric_column(stats.name)) {
      return ColumnType::NUMERIC;
    }
    if (looks_like_integer_column(stats.name)) {
      return ColumnType::INTEGER;
    }
    return ColumnType::TEXT;
  }

  // Single dominant type
  if (stats.timestamp_count > 0) {
    return ColumnType::TIMESTAMP;
  }
  if (stats.integer_count > 0 && stats.float_count == 0) {
    return ColumnType::INTEGER;
  }
  if (stats.float_count > 0) {
    return ColumnType::NUMERIC;
  }

  // Fallback
  return ColumnType::TEXT;
}

}  // namespace csvzall::pipeline::postgres

#endif  // CSVZALL_HAVE_POSTGRESQL

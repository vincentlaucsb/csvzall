#include "json.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace csvzall::pipeline::commands::view_internal {
namespace {

using Json = nlohmann::ordered_json;

Json ParseJsonObject(std::string_view body) {
  auto parsed = Json::parse(body.begin(), body.end());
  if (!parsed.is_object()) {
    throw std::runtime_error("expected JSON object");
  }
  return parsed;
}

const Json& RequiredField(const Json& object, std::string_view field) {
  const auto iter = object.find(std::string(field));
  if (iter == object.end()) {
    throw std::runtime_error("missing JSON field: " + std::string(field));
  }
  return *iter;
}

bool IsMissingFieldError(const std::exception& ex, std::string_view field) {
  return ex.what() == "missing JSON field: " + std::string(field);
}

std::vector<std::string> StringsFromJsonArray(const Json& value) {
  if (!value.is_array()) {
    throw std::runtime_error("expected JSON array");
  }

  std::vector<std::string> values;
  values.reserve(value.size());
  for (const auto& item : value) {
    if (!item.is_string()) {
      throw std::runtime_error("expected JSON string");
    }
    values.push_back(item.get<std::string>());
  }
  return values;
}

}  // namespace

std::string BuildSchemaJson(const CsvViewData& data, bool editable) {
  Json json;
  json["file"] = data.file_name();
  json["columns"] = data.headers();
  json["readOnly"] = !editable;
  json["editable"] = editable;
  json["mode"] = std::string(data.mode_name());
  json["totalRows"] = data.row_count();
  return json.dump();
}

std::string BuildRowsJson(const CsvViewData& data,
                          std::uint64_t offset,
                          std::uint64_t limit,
                          const std::vector<std::vector<std::string>>& rows) {
  Json row_values = Json::array();
  for (const auto& row : rows) {
    Json cells = Json::array();
    for (std::size_t col_index = 0; col_index < data.headers().size(); ++col_index) {
      cells.push_back(col_index < row.size() ? row[col_index] : "");
    }
    row_values.push_back(std::move(cells));
  }

  Json json;
  json["offset"] = offset;
  json["limit"] = limit;
  json["totalRows"] = data.row_count();
  json["rows"] = std::move(row_values);
  return json.dump();
}

std::string BuildHealthJson() {
  Json json;
  json["status"] = "ok";
  json["readOnly"] = true;
  return json.dump();
}

std::string BuildStartupJson(const std::string& url) {
  Json json;
  json["url"] = url;
  return json.dump();
}

std::uint64_t JsonUintField(std::string_view body, std::string_view field) {
  const auto object = ParseJsonObject(body);
  const auto& value = RequiredField(object, field);
  if (value.is_number_unsigned()) {
    return value.get<std::uint64_t>();
  }
  if (value.is_number_integer()) {
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value >= 0) {
      return static_cast<std::uint64_t>(signed_value);
    }
  }
  throw std::runtime_error("expected JSON integer");
}

std::string JsonStringField(std::string_view body, std::string_view field) {
  const auto object = ParseJsonObject(body);
  const auto& value = RequiredField(object, field);
  if (!value.is_string()) {
    throw std::runtime_error("expected JSON string");
  }
  return value.get<std::string>();
}

std::vector<std::string> JsonStringArrayField(std::string_view body, std::string_view field) {
  const auto object = ParseJsonObject(body);
  return StringsFromJsonArray(RequiredField(object, field));
}

bool JsonBoolField(std::string_view body, std::string_view field) {
  const auto object = ParseJsonObject(body);
  const auto& value = RequiredField(object, field);
  if (!value.is_boolean()) {
    throw std::runtime_error("expected JSON boolean");
  }
  return value.get<bool>();
}

std::string JsonStringFieldOr(std::string_view body,
                              std::string_view field,
                              std::string fallback) {
  try {
    return JsonStringField(body, field);
  } catch (const std::exception& ex) {
    if (IsMissingFieldError(ex, field)) {
      return fallback;
    }
    throw;
  }
}

bool JsonBoolFieldOr(std::string_view body, std::string_view field, bool fallback) {
  try {
    return JsonBoolField(body, field);
  } catch (const std::exception& ex) {
    if (IsMissingFieldError(ex, field)) {
      return fallback;
    }
    throw;
  }
}

std::vector<std::string> JsonStringArrayFieldOr(std::string_view body,
                                                std::string_view field,
                                                std::vector<std::string> fallback) {
  try {
    return JsonStringArrayField(body, field);
  } catch (const std::exception& ex) {
    if (IsMissingFieldError(ex, field)) {
      return fallback;
    }
    throw;
  }
}

std::string SaveResultJson(std::size_t charts_generated,
                           std::string_view chart_error) {
  Json json;
  json["ok"] = true;
  json["chartsGenerated"] = charts_generated;
  if (!chart_error.empty()) {
    json["chartError"] = chart_error;
  }
  return json.dump();
}

}  // namespace csvzall::pipeline::commands::view_internal

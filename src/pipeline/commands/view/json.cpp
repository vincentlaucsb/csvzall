#include "json.hpp"

#include <cctype>
#include <stdexcept>

namespace csvzall::pipeline::commands::view_internal {
void AppendJsonString(std::string& output, std::string_view value) {
  output.push_back('"');
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (ch < 0x20) {
          static constexpr char hex[] = "0123456789abcdef";
          output += "\\u00";
          output.push_back(hex[(ch >> 4) & 0x0f]);
          output.push_back(hex[ch & 0x0f]);
        } else {
          output.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  output.push_back('"');
}

std::string BuildSchemaJson(const CsvViewData& data, bool editable) {
  std::string json;
  json.reserve(256 + data.headers().size() * 32);
  json += "{\"file\":";
  AppendJsonString(json, data.file_name());
  json += ",\"columns\":[";
  for (std::size_t i = 0; i < data.headers().size(); ++i) {
    if (i > 0) {
      json.push_back(',');
    }
    AppendJsonString(json, data.headers()[i]);
  }
  json += "],\"readOnly\":";
  json += editable ? "false" : "true";
  json += ",\"editable\":";
  json += editable ? "true" : "false";
  json += ",\"mode\":";
  AppendJsonString(json, data.mode_name());
  json += ",\"totalRows\":";
  json += std::to_string(data.row_count());
  json += "}";
  return json;
}

std::string BuildRowsJson(const CsvViewData& data,
                          std::uint64_t offset,
                          std::uint64_t limit,
                          const std::vector<std::vector<std::string>>& rows) {
  std::string json;
  json += "{\"offset\":";
  json += std::to_string(offset);
  json += ",\"limit\":";
  json += std::to_string(limit);
  json += ",\"totalRows\":";
  json += std::to_string(data.row_count());
  json += ",\"rows\":[";

  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    if (row_index > 0) {
      json.push_back(',');
    }
    json.push_back('[');
    const auto& row = rows[row_index];
    for (std::size_t col_index = 0; col_index < data.headers().size(); ++col_index) {
      if (col_index > 0) {
        json.push_back(',');
      }
      if (col_index < row.size()) {
        AppendJsonString(json, row[col_index]);
      } else {
        AppendJsonString(json, "");
      }
    }
    json.push_back(']');
  }

  json += "]}";
  return json;
}

std::string BuildHealthJson() {
  return "{\"status\":\"ok\",\"readOnly\":true}";
}

std::string BuildStartupJson(const std::string& url) {
  std::string json = "{\"url\":";
  AppendJsonString(json, url);
  json += "}";
  return json;
}

void SkipJsonWs(std::string_view text, std::size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
    ++pos;
  }
}

std::string ParseJsonString(std::string_view text, std::size_t& pos) {
  SkipJsonWs(text, pos);
  if (pos >= text.size() || text[pos] != '"') {
    throw std::runtime_error("expected JSON string");
  }
  ++pos;
  std::string value;
  while (pos < text.size()) {
    const char ch = text[pos++];
    if (ch == '"') {
      return value;
    }
    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }
    if (pos >= text.size()) {
      throw std::runtime_error("unterminated JSON escape");
    }
    const char escaped = text[pos++];
    switch (escaped) {
      case '"': value.push_back('"'); break;
      case '\\': value.push_back('\\'); break;
      case '/': value.push_back('/'); break;
      case 'b': value.push_back('\b'); break;
      case 'f': value.push_back('\f'); break;
      case 'n': value.push_back('\n'); break;
      case 'r': value.push_back('\r'); break;
      case 't': value.push_back('\t'); break;
      default:
        throw std::runtime_error("unsupported JSON escape");
    }
  }
  throw std::runtime_error("unterminated JSON string");
}

void SkipJsonValue(std::string_view text, std::size_t& pos) {
  SkipJsonWs(text, pos);
  if (pos >= text.size()) {
    throw std::runtime_error("expected JSON value");
  }
  if (text[pos] == '"') {
    (void)ParseJsonString(text, pos);
    return;
  }
  if (text[pos] == '{' || text[pos] == '[') {
    const char open = text[pos++];
    const char close = open == '{' ? '}' : ']';
    int depth = 1;
    while (pos < text.size() && depth > 0) {
      if (text[pos] == '"') {
        (void)ParseJsonString(text, pos);
      } else if (text[pos] == open) {
        ++depth;
        ++pos;
      } else if (text[pos] == close) {
        --depth;
        ++pos;
      } else {
        ++pos;
      }
    }
    if (depth != 0) {
      throw std::runtime_error("unterminated JSON value");
    }
    return;
  }
  while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']') {
    ++pos;
  }
}

template <typename Parser>
auto ParseJsonField(std::string_view body, std::string_view field, Parser parser) {
  std::size_t pos = 0;
  SkipJsonWs(body, pos);
  if (pos >= body.size() || body[pos++] != '{') {
    throw std::runtime_error("expected JSON object");
  }
  while (true) {
    SkipJsonWs(body, pos);
    if (pos < body.size() && body[pos] == '}') {
      break;
    }
    const auto key = ParseJsonString(body, pos);
    SkipJsonWs(body, pos);
    if (pos >= body.size() || body[pos++] != ':') {
      throw std::runtime_error("expected JSON object separator");
    }
    if (key == field) {
      return parser(body, pos);
    }
    SkipJsonValue(body, pos);
    SkipJsonWs(body, pos);
    if (pos < body.size() && body[pos] == ',') {
      ++pos;
      continue;
    }
    if (pos < body.size() && body[pos] == '}') {
      break;
    }
  }
  throw std::runtime_error("missing JSON field: " + std::string(field));
}

std::uint64_t JsonUintField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    SkipJsonWs(text, pos);
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
      throw std::runtime_error("expected JSON integer");
    }
    std::uint64_t value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      value = value * 10 + static_cast<std::uint64_t>(text[pos++] - '0');
    }
    return value;
  });
}

std::string JsonStringField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    return ParseJsonString(text, pos);
  });
}

std::vector<std::string> JsonStringArrayField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    SkipJsonWs(text, pos);
    if (pos >= text.size() || text[pos++] != '[') {
      throw std::runtime_error("expected JSON array");
    }
    std::vector<std::string> values;
    while (true) {
      SkipJsonWs(text, pos);
      if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return values;
      }
      values.push_back(ParseJsonString(text, pos));
      SkipJsonWs(text, pos);
      if (pos < text.size() && text[pos] == ',') {
        ++pos;
        continue;
      }
      if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return values;
      }
      throw std::runtime_error("expected JSON array separator");
    }
  });
}

bool JsonBoolField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    SkipJsonWs(text, pos);
    if (text.substr(pos, 4) == "true") {
      pos += 4;
      return true;
    }
    if (text.substr(pos, 5) == "false") {
      pos += 5;
      return false;
    }
    throw std::runtime_error("expected JSON boolean");
  });
}

std::string JsonStringFieldOr(std::string_view body,
                              std::string_view field,
                              std::string fallback) {
  try {
    return JsonStringField(body, field);
  } catch (const std::exception& ex) {
    const std::string missing = "missing JSON field: " + std::string(field);
    if (ex.what() == missing) {
      return fallback;
    }
    throw;
  }
}

bool JsonBoolFieldOr(std::string_view body, std::string_view field, bool fallback) {
  try {
    return JsonBoolField(body, field);
  } catch (const std::exception& ex) {
    const std::string missing = "missing JSON field: " + std::string(field);
    if (ex.what() == missing) {
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
    const std::string missing = "missing JSON field: " + std::string(field);
    if (ex.what() == missing) {
      return fallback;
    }
    throw;
  }
}

std::string SaveResultJson(std::size_t charts_generated,
                           std::string_view chart_error) {
  std::string result = "{\"ok\":true,\"chartsGenerated\":";
  result += std::to_string(charts_generated);
  if (!chart_error.empty()) {
    result += ",\"chartError\":";
    AppendJsonString(result, chart_error);
  }
  result += "}";
  return result;
}
}  // namespace csvzall::pipeline::commands::view_internal

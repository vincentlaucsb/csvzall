#include "commands.hpp"

#include "../common/json_path.hpp"

#include <csv.hpp>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace csvzall::pipeline::commands {
namespace {

struct JsonValue {
  enum class Kind {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
  };

  Kind kind = Kind::Null;
  std::string text;
  std::vector<JsonValue> array;
  std::vector<std::pair<std::string, JsonValue>> object;
};

class JsonParser {
public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  JsonValue parse() {
    skip_ws();
    auto value = parse_value();
    skip_ws();
    if (pos_ != text_.size()) {
      fail("Unexpected trailing JSON content");
    }
    return value;
  }

private:
  [[noreturn]] void fail(const std::string& message) const {
    throw std::runtime_error(message + " at byte " + std::to_string(pos_));
  }

  void skip_ws() {
    while (pos_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
      ++pos_;
    }
  }

  char peek() const {
    if (pos_ >= text_.size()) {
      fail("Unexpected end of JSON");
    }
    return text_[pos_];
  }

  char consume() {
    const char ch = peek();
    ++pos_;
    return ch;
  }

  void expect(char wanted) {
    if (consume() != wanted) {
      fail(std::string("Expected '") + wanted + "'");
    }
  }

  JsonValue parse_value() {
    skip_ws();
    switch (peek()) {
      case 'n':
        return parse_literal("null", JsonValue::Kind::Null);
      case 't':
        return parse_literal("true", JsonValue::Kind::Bool);
      case 'f':
        return parse_literal("false", JsonValue::Kind::Bool);
      case '"': {
        JsonValue value;
        value.kind = JsonValue::Kind::String;
        value.text = parse_string();
        return value;
      }
      case '[':
        return parse_array();
      case '{':
        return parse_object();
      default:
        if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())) != 0) {
          return parse_number();
        }
        fail("Unexpected JSON token");
    }
  }

  JsonValue parse_literal(std::string_view literal, JsonValue::Kind kind) {
    if (text_.substr(pos_, literal.size()) != literal) {
      fail("Invalid JSON literal");
    }
    pos_ += literal.size();
    JsonValue value;
    value.kind = kind;
    value.text = std::string(literal);
    return value;
  }

  static void append_utf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
      out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
  }

  std::uint32_t parse_hex4() {
    if (pos_ + 4 > text_.size()) {
      fail("Incomplete unicode escape");
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = text_[pos_++];
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value += static_cast<std::uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        value += static_cast<std::uint32_t>(10 + ch - 'a');
      } else if (ch >= 'A' && ch <= 'F') {
        value += static_cast<std::uint32_t>(10 + ch - 'A');
      } else {
        fail("Invalid unicode escape");
      }
    }
    return value;
  }

  std::string parse_string() {
    expect('"');
    std::string value;
    while (pos_ < text_.size()) {
      const char ch = consume();
      if (ch == '"') {
        return value;
      }
      if (static_cast<unsigned char>(ch) < 0x20) {
        fail("Unescaped control character in string");
      }
      if (ch != '\\') {
        value.push_back(ch);
        continue;
      }

      const char escaped = consume();
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          value.push_back(escaped);
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        case 'u': {
          std::uint32_t codepoint = parse_hex4();
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (pos_ + 6 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
              fail("High unicode surrogate without low surrogate");
            }
            pos_ += 2;
            const std::uint32_t low = parse_hex4();
            if (low < 0xDC00 || low > 0xDFFF) {
              fail("Invalid low unicode surrogate");
            }
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
          }
          append_utf8(value, codepoint);
          break;
        }
        default:
          fail("Invalid string escape");
      }
    }
    fail("Unterminated string");
  }

  JsonValue parse_number() {
    const std::size_t start = pos_;
    if (peek() == '-') {
      ++pos_;
    }
    if (pos_ >= text_.size()) {
      fail("Invalid number");
    }
    if (text_[pos_] == '0') {
      ++pos_;
    } else if (std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
      while (pos_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
        ++pos_;
      }
    } else {
      fail("Invalid number");
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if (pos_ >= text_.size() ||
          std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0) {
        fail("Invalid number fraction");
      }
      while (pos_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= text_.size() ||
          std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0) {
        fail("Invalid number exponent");
      }
      while (pos_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
        ++pos_;
      }
    }
    JsonValue value;
    value.kind = JsonValue::Kind::Number;
    value.text = std::string(text_.substr(start, pos_ - start));
    return value;
  }

  JsonValue parse_array() {
    expect('[');
    JsonValue value;
    value.kind = JsonValue::Kind::Array;
    skip_ws();
    if (peek() == ']') {
      ++pos_;
      return value;
    }
    while (true) {
      value.array.push_back(parse_value());
      skip_ws();
      const char ch = consume();
      if (ch == ']') {
        return value;
      }
      if (ch != ',') {
        fail("Expected ',' or ']' in array");
      }
    }
  }

  JsonValue parse_object() {
    expect('{');
    JsonValue value;
    value.kind = JsonValue::Kind::Object;
    skip_ws();
    if (peek() == '}') {
      ++pos_;
      return value;
    }
    while (true) {
      skip_ws();
      if (peek() != '"') {
        fail("Expected object field name");
      }
      auto key = parse_string();
      skip_ws();
      expect(':');
      value.object.emplace_back(std::move(key), parse_value());
      skip_ws();
      const char ch = consume();
      if (ch == '}') {
        return value;
      }
      if (ch != ',') {
        fail("Expected ',' or '}' in object");
      }
    }
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

struct ColumnMapping {
  std::string name;
  common::JsonPath path;
};

struct JsonMapping {
  common::JsonPath rows_path;
  std::vector<ColumnMapping> columns;
};

std::string ReadFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("Unable to open file: " + path);
  }
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

const JsonValue* FindField(const JsonValue& object, std::string_view name) {
  if (object.kind != JsonValue::Kind::Object) {
    return nullptr;
  }
  for (const auto& [key, value] : object.object) {
    if (key == name) {
      return &value;
    }
  }
  return nullptr;
}

const JsonValue& RequiredField(const JsonValue& object, std::string_view name) {
  const auto* value = FindField(object, name);
  if (!value) {
    throw std::runtime_error("Mapping is missing required field '" + std::string(name) + "'");
  }
  return *value;
}

std::string RequiredString(const JsonValue& value, std::string_view name) {
  if (value.kind != JsonValue::Kind::String) {
    throw std::runtime_error("Mapping field '" + std::string(name) + "' must be a string");
  }
  return value.text;
}

JsonMapping LoadMapping(const std::string& mapping_path) {
  auto root = JsonParser(ReadFile(mapping_path)).parse();
  if (root.kind != JsonValue::Kind::Object) {
    throw std::runtime_error("Mapping JSON must be an object");
  }

  JsonMapping mapping;
  mapping.rows_path = common::ParseJsonPath(RequiredString(RequiredField(root, "rows"), "rows"));

  const auto& columns = RequiredField(root, "columns");
  if (columns.kind != JsonValue::Kind::Object) {
    throw std::runtime_error("Mapping field 'columns' must be an object");
  }

  std::unordered_set<std::string> seen_columns;
  for (const auto& [name, value] : columns.object) {
    if (!seen_columns.insert(name).second) {
      throw std::runtime_error("Duplicate output column in mapping: " + name);
    }
    if (value.kind != JsonValue::Kind::String) {
      throw std::runtime_error("Column mapping for '" + name + "' must be a string path");
    }
    mapping.columns.push_back({name, common::ParseJsonPath(value.text)});
  }

  if (mapping.columns.empty()) {
    throw std::runtime_error("Mapping field 'columns' must contain at least one column");
  }

  return mapping;
}

std::vector<const JsonValue*> EvaluateJsonPath(const JsonValue& root,
                                               const common::JsonPath& path) {
  std::vector<const JsonValue*> current{&root};
  for (const auto& step : path.steps) {
    std::vector<const JsonValue*> next;
    for (const auto* value : current) {
      switch (step.kind) {
        case common::JsonPathStep::Kind::Field:
          if (const auto* child = FindField(*value, step.field)) {
            next.push_back(child);
          }
          break;
        case common::JsonPathStep::Kind::Index:
          if (value->kind == JsonValue::Kind::Array && step.index < value->array.size()) {
            next.push_back(&value->array[step.index]);
          }
          break;
        case common::JsonPathStep::Kind::Wildcard:
          if (value->kind == JsonValue::Kind::Array) {
            for (const auto& child : value->array) {
              next.push_back(&child);
            }
          }
          break;
      }
    }
    current = std::move(next);
    if (current.empty()) {
      break;
    }
  }
  return current;
}

std::vector<const JsonValue*> EvaluateRowsPath(const JsonValue& root,
                                               const common::JsonPath& path) {
  auto matches = EvaluateJsonPath(root, path);
  if (matches.empty()) {
    return {};
  }
  if (!path.steps.empty() &&
      path.steps.back().kind == common::JsonPathStep::Kind::Wildcard) {
    return matches;
  }
  if (matches.size() != 1 || matches[0]->kind != JsonValue::Kind::Array) {
    throw std::runtime_error("rows path must select an array or wildcard over an array");
  }
  std::vector<const JsonValue*> rows;
  rows.reserve(matches[0]->array.size());
  for (const auto& row : matches[0]->array) {
    rows.push_back(&row);
  }
  return rows;
}

std::string ScalarToCsvCell(const JsonValue& value) {
  switch (value.kind) {
    case JsonValue::Kind::Null:
      return {};
    case JsonValue::Kind::Bool:
    case JsonValue::Kind::Number:
    case JsonValue::Kind::String:
      return value.text;
    case JsonValue::Kind::Array:
    case JsonValue::Kind::Object:
      throw std::runtime_error("JSON column path selected an object or array");
  }
  throw std::runtime_error("Unsupported JSON value type");
}

std::vector<std::string> HeaderRow(const JsonMapping& mapping) {
  std::vector<std::string> headers;
  headers.reserve(mapping.columns.size());
  for (const auto& column : mapping.columns) {
    headers.push_back(column.name);
  }
  return headers;
}

}  // namespace

int RunJsonExtract(const std::string& input_path,
                   const std::string& mapping_path,
                   std::ostream& output,
                   const LoggerCallbacks& logger,
                   RunStats& stats) {
  try {
    if (input_path.empty() || input_path == "-") {
      throw std::runtime_error("json extract requires an input JSON file path");
    }
    if (mapping_path.empty()) {
      throw std::runtime_error("json extract requires --map <mapping.json>");
    }

    const auto mapping = LoadMapping(mapping_path);
    const auto document = JsonParser(ReadFile(input_path)).parse();
    const auto rows = EvaluateRowsPath(document, mapping.rows_path);

    auto writer = csv::make_csv_writer(output).set_auto_flush(false);
    writer << HeaderRow(mapping);

    for (const auto* row : rows) {
      std::vector<std::string> out_row;
      out_row.reserve(mapping.columns.size());
      for (const auto& column : mapping.columns) {
        const auto values = EvaluateJsonPath(*row, column.path);
        if (values.empty()) {
          out_row.emplace_back();
        } else if (values.size() == 1) {
          out_row.push_back(ScalarToCsvCell(*values[0]));
        } else {
          throw std::runtime_error(
              "Column path selected multiple values for column '" + column.name + "'");
        }
      }
      writer << out_row;
      ++stats.rows_processed;
    }

    writer.flush();
    return 0;
  } catch (const std::exception& ex) {
    if (logger.error) {
      logger.error(std::string("json extract: ") + ex.what());
    }
    return 1;
  }
}

}  // namespace csvzall::pipeline::commands

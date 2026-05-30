#include "json_path.hpp"

#include <cctype>
#include <stdexcept>

namespace csvzall::pipeline::common {
namespace {

bool IsIdentifierStart(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool IsIdentifierChar(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string ParseQuotedField(std::string_view text, std::size_t& pos) {
  const char quote = text[pos];
  ++pos;
  std::string field;

  while (pos < text.size()) {
    const char ch = text[pos++];
    if (ch == quote) {
      return field;
    }
    if (ch == '\\') {
      if (pos >= text.size()) {
        throw std::runtime_error("Invalid JSONPath quoted field escape");
      }
      const char escaped = text[pos++];
      switch (escaped) {
        case '\\':
        case '"':
        case '\'':
        case '/':
          field.push_back(escaped);
          break;
        case 'n':
          field.push_back('\n');
          break;
        case 'r':
          field.push_back('\r');
          break;
        case 't':
          field.push_back('\t');
          break;
        default:
          throw std::runtime_error("Unsupported JSONPath quoted field escape");
      }
    } else {
      field.push_back(ch);
    }
  }

  throw std::runtime_error("Unterminated JSONPath quoted field");
}

std::vector<simdjson::dom::element> EvaluateStep(
    const std::vector<simdjson::dom::element>& current,
    const JsonPathStep& step) {
  std::vector<simdjson::dom::element> next;

  for (auto element : current) {
    switch (step.kind) {
      case JsonPathStep::Kind::Field: {
        simdjson::dom::object object;
        const auto object_error = element.get_object().get(object);
        if (object_error == simdjson::INCORRECT_TYPE) {
          continue;
        }
        if (object_error) {
          throw std::runtime_error(
              std::string("JSON object access failed: ") +
              simdjson::error_message(object_error));
        }

        simdjson::dom::element child;
        const auto child_error = object.at_key(step.field).get(child);
        if (child_error == simdjson::NO_SUCH_FIELD) {
          continue;
        }
        if (child_error) {
          throw std::runtime_error(
              std::string("JSON field access failed: ") +
              simdjson::error_message(child_error));
        }
        next.push_back(child);
        break;
      }

      case JsonPathStep::Kind::Index: {
        simdjson::dom::array array;
        const auto array_error = element.get_array().get(array);
        if (array_error == simdjson::INCORRECT_TYPE) {
          continue;
        }
        if (array_error) {
          throw std::runtime_error(
              std::string("JSON array access failed: ") +
              simdjson::error_message(array_error));
        }

        std::size_t i = 0;
        for (auto child_result : array) {
          simdjson::dom::element child;
          const auto child_error = child_result.get(child);
          if (child_error) {
            throw std::runtime_error(
                std::string("JSON array element access failed: ") +
                simdjson::error_message(child_error));
          }
          if (i == step.index) {
            next.push_back(child);
            break;
          }
          ++i;
        }
        break;
      }

      case JsonPathStep::Kind::Wildcard: {
        simdjson::dom::array array;
        const auto array_error = element.get_array().get(array);
        if (array_error == simdjson::INCORRECT_TYPE) {
          continue;
        }
        if (array_error) {
          throw std::runtime_error(
              std::string("JSON wildcard access failed: ") +
              simdjson::error_message(array_error));
        }

        for (auto child_result : array) {
          simdjson::dom::element child;
          const auto child_error = child_result.get(child);
          if (child_error) {
            throw std::runtime_error(
                std::string("JSON wildcard element access failed: ") +
                simdjson::error_message(child_error));
          }
          next.push_back(child);
        }
        break;
      }
    }
  }

  return next;
}

}  // namespace

JsonPath ParseJsonPath(std::string_view text) {
  if (text.empty() || text[0] != '$') {
    throw std::runtime_error("JSONPath must start with '$'");
  }

  JsonPath path;
  std::size_t pos = 1;
  while (pos < text.size()) {
    if (text[pos] == '.') {
      ++pos;
      if (pos >= text.size() || !IsIdentifierStart(text[pos])) {
        throw std::runtime_error("Invalid JSONPath field after '.'");
      }
      const std::size_t start = pos;
      ++pos;
      while (pos < text.size() && IsIdentifierChar(text[pos])) {
        ++pos;
      }
      JsonPathStep step;
      step.kind = JsonPathStep::Kind::Field;
      step.field = std::string(text.substr(start, pos - start));
      path.steps.push_back(std::move(step));
      continue;
    }

    if (text[pos] == '[') {
      ++pos;
      if (pos >= text.size()) {
        throw std::runtime_error("Unterminated JSONPath bracket");
      }

      if (text[pos] == '*') {
        ++pos;
        if (pos >= text.size() || text[pos] != ']') {
          throw std::runtime_error("Invalid JSONPath wildcard");
        }
        ++pos;
        JsonPathStep step;
        step.kind = JsonPathStep::Kind::Wildcard;
        path.steps.push_back(std::move(step));
        continue;
      }

      if (text[pos] == '"' || text[pos] == '\'') {
        JsonPathStep step;
        step.kind = JsonPathStep::Kind::Field;
        step.field = ParseQuotedField(text, pos);
        if (pos >= text.size() || text[pos] != ']') {
          throw std::runtime_error("Expected ']' after JSONPath quoted field");
        }
        ++pos;
        path.steps.push_back(std::move(step));
        continue;
      }

      if (std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
        std::size_t index = 0;
        while (pos < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
          index = index * 10 + static_cast<std::size_t>(text[pos] - '0');
          ++pos;
        }
        if (pos >= text.size() || text[pos] != ']') {
          throw std::runtime_error("Expected ']' after JSONPath array index");
        }
        ++pos;
        JsonPathStep step;
        step.kind = JsonPathStep::Kind::Index;
        step.index = index;
        path.steps.push_back(std::move(step));
        continue;
      }

      throw std::runtime_error("Unsupported JSONPath bracket expression");
    }

    throw std::runtime_error("Unsupported JSONPath syntax");
  }

  return path;
}

std::vector<simdjson::dom::element> EvaluateJsonPath(
    simdjson::dom::element root,
    const JsonPath& path) {
  std::vector<simdjson::dom::element> current{root};
  for (const auto& step : path.steps) {
    current = EvaluateStep(current, step);
    if (current.empty()) {
      break;
    }
  }
  return current;
}

std::vector<simdjson::dom::element> EvaluateJsonRowsPath(
    simdjson::dom::element root,
    const JsonPath& path) {
  auto matches = EvaluateJsonPath(root, path);
  if (matches.empty()) {
    return {};
  }

  if (!path.steps.empty() &&
      path.steps.back().kind == JsonPathStep::Kind::Wildcard) {
    return matches;
  }

  if (matches.size() != 1) {
    throw std::runtime_error("rows path must select one array or use an array wildcard");
  }

  simdjson::dom::array array;
  const auto error = matches[0].get_array().get(array);
  if (error) {
    throw std::runtime_error("rows path must select an array or wildcard over an array");
  }

  std::vector<simdjson::dom::element> rows;
  for (auto row_result : array) {
    simdjson::dom::element row;
    const auto row_error = row_result.get(row);
    if (row_error) {
      throw std::runtime_error(
          std::string("JSON row access failed: ") +
          simdjson::error_message(row_error));
    }
    rows.push_back(row);
  }
  return rows;
}

}  // namespace csvzall::pipeline::common

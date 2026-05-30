#pragma once

#include <simdjson.h>

#include <string>
#include <string_view>
#include <vector>

namespace csvzall::pipeline::common {

struct JsonPathStep {
  enum class Kind {
    Field,
    Index,
    Wildcard,
  };

  Kind kind = Kind::Field;
  std::string field;
  std::size_t index = 0;
};

struct JsonPath {
  std::vector<JsonPathStep> steps;
};

JsonPath ParseJsonPath(std::string_view text);

std::vector<simdjson::dom::element> EvaluateJsonPath(
    simdjson::dom::element root,
    const JsonPath& path);

std::vector<simdjson::dom::element> EvaluateJsonRowsPath(
    simdjson::dom::element root,
    const JsonPath& path);

}  // namespace csvzall::pipeline::common

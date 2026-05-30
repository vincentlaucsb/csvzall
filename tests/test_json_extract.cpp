#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/transform_pipeline.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

namespace {

std::filesystem::path TempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() /
         ("csvzall_" + name);
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

int RunJsonExtractForText(const std::string& json,
                          const std::string& mapping,
                          std::ostringstream& output) {
  const auto json_path = TempPath("input.json");
  const auto map_path = TempPath("map.json");
  WriteText(json_path, json);
  WriteText(map_path, mapping);

  auto logger = tests::MakeNullLogger();
  pipeline::RunStats stats;
  return pipeline::RunJsonExtract(json_path.string(), map_path.string(), output, logger, stats);
}

}  // namespace

TEST_CASE("JSON extract: basic and nested fields") {
  std::ostringstream output;
  const int rc = RunJsonExtractForText(
      R"({"results":[{"id":1,"extra":{"content":"Gym","due":"2026-05-01"}},{"id":2,"extra":{"content":"Run","due":null}}]})",
      R"({"rows":"$.results[*]","columns":{"id":"$.id","content":"$.extra.content","due":"$.extra.due"}})",
      output);

  REQUIRE(rc == 0);
  const auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows == std::vector<std::vector<std::string>>{
                      {"id", "content", "due"},
                      {"1", "Gym", "2026-05-01"},
                      {"2", "Run", ""}});
}

TEST_CASE("JSON extract: quoted field names and array indexes") {
  std::ostringstream output;
  const int rc = RunJsonExtractForText(
      R"({"items":[{"field name":{"a.b":["zero","one"]}}]})",
      R"({"rows":"$.items","columns":{"picked":"$[\"field name\"]['a.b'][1]"}})",
      output);

  REQUIRE(rc == 0);
  const auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows == std::vector<std::vector<std::string>>{{"picked"}, {"one"}});
}

TEST_CASE("JSON extract: missing null boolean numeric and timestamp scalars") {
  std::ostringstream output;
  const int rc = RunJsonExtractForText(
      R"({"results":[{"ok":true,"count":3,"when":"2026-04-29T17:40:10.391658Z","missing_is_absent":1}]})",
      R"({"rows":"$.results[*]","columns":{"ok":"$.ok","count":"$.count","when":"$.when","missing":"$.missing","nullish":"$.nothing"}})",
      output);

  REQUIRE(rc == 0);
  const auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows == std::vector<std::vector<std::string>>{
                      {"ok", "count", "when", "missing", "nullish"},
                      {"true", "3", "2026-04-29T17:40:10.391658Z", "", ""}});
}

TEST_CASE("JSON extract: rejects object and array column values") {
  std::ostringstream output;
  const int rc = RunJsonExtractForText(
      R"({"results":[{"nested":{"x":1}}]})",
      R"({"rows":"$.results[*]","columns":{"nested":"$.nested"}})",
      output);

  REQUIRE(rc == 1);
}

TEST_CASE("JSON extract: rejects invalid input mapping and unsupported paths") {
  {
    std::ostringstream output;
    const int rc = RunJsonExtractForText(
        R"({"results":[{}]})",
        R"({"columns":{"x":"$.x"}})",
        output);
    REQUIRE(rc == 1);
  }
  {
    std::ostringstream output;
    const int rc = RunJsonExtractForText(
        R"({"results":[{}]})",
        R"({"rows":"$.results[?(@.x)]","columns":{"x":"$.x"}})",
        output);
    REQUIRE(rc == 1);
  }
  {
    std::ostringstream output;
    const int rc = RunJsonExtractForText(
        R"({"results":[{}]})",
        R"({"rows":"$.results[*]"})",
        output);
    REQUIRE(rc == 1);
  }
  {
    std::ostringstream output;
    const int rc = RunJsonExtractForText(
        "{\"results\":[{}]",
        R"({"rows":"$.results[*]","columns":{"x":"$.x"}})",
        output);
    REQUIRE(rc == 1);
  }
}

TEST_CASE("JSON extract: Todoist-shaped activity fixture") {
  std::ostringstream output;
  const int rc = RunJsonExtractForText(
      R"({"results":[{"id":2148846797991822068047640471540859234,"event_type":"completed","event_date":"2026-04-29T17:40:10.391658Z","object_id":"6XMFh56Fm3xX7xg5","parent_project_id":"6Frc4Jvrw5Q4CQ85","extra_data":{"client":"Mozilla/5.0; Todoist/10416","content":"Gym workout","due_date":"2026-05-01T06:59:59Z","was_overdue":true}}],"next_cursor":null})",
      R"({"rows":"$.results[*]","columns":{"event_type":"$.event_type","completed_at":"$.event_date","task_id":"$.object_id","project_id":"$.parent_project_id","content":"$.extra_data.content","due_date":"$.extra_data.due_date","was_overdue":"$.extra_data.was_overdue","client":"$.extra_data.client"}})",
      output);

  REQUIRE(rc == 0);
  const auto rows = tests::ParseCsv(output.str());
  REQUIRE(rows.size() == 2);
  REQUIRE(rows[1][0] == "completed");
  REQUIRE(rows[1][1] == "2026-04-29T17:40:10.391658Z");
  REQUIRE(rows[1][4] == "Gym workout");
  REQUIRE(rows[1][5] == "2026-05-01T06:59:59Z");
  REQUIRE(rows[1][6] == "true");
}

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "../vendor/httplib/httplib.h"

#include "../src/pipeline/commands/view.hpp"
#include "common_test_utils.hpp"

using namespace csvzall;

namespace {

std::filesystem::path WriteTempCsv(const std::string& text, const std::string& name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream output(path, std::ios::binary);
  output << text;
  output.close();
  return path;
}

std::vector<std::vector<std::string>> ReadAllRows(const std::filesystem::path& path) {
  csv::CSVReader reader(path.string(), csv::CSVFormat().header_row(0));
  std::vector<std::vector<std::string>> rows;
  for (auto& row : reader) {
    rows.emplace_back(std::vector<std::string>(row));
  }
  return rows;
}

std::vector<std::string> ReadHeaders(const std::filesystem::path& path) {
  csv::CSVReader reader(path.string(), csv::CSVFormat().header_row(0));
  return reader.get_col_names();
}

std::filesystem::path WriteTempViewerAssets(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  {
    std::ofstream output(dir / "index.html", std::ios::binary);
    output << "<!doctype html><title>dev viewer</title>";
  }
  {
    std::ofstream output(dir / "viewer.css", std::ios::binary);
    output << "body { color: rgb(1, 2, 3); }";
  }
  {
    std::ofstream output(dir / "viewer.js", std::ios::binary);
    output << "window.csvzallDevViewer = true;";
  }
  return dir;
}

std::filesystem::path TempDir(const std::string& name) {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() / (name + "_" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input.is_open());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

}  // namespace

TEST_CASE("view: loads CSV rows with shared parser behavior") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}});
  const auto path = WriteTempCsv(csv, "csvzall view's sample.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvIndexedFile::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  REQUIRE(data.file_name() == "csvzall view's sample.csv");
  REQUIRE(data.headers() == std::vector<std::string>{"name", "value"});
  REQUIRE(data.row_count() == 2);
  REQUIRE(data.read_rows(1, 1) == std::vector<std::vector<std::string>>{{"bob", "20"}});
  REQUIRE(stats.rows_processed == 2);

  std::filesystem::remove(path);
}

TEST_CASE("view: empty CSV reports a helpful header message") {
  const auto path = WriteTempCsv("", "csvzall_view_empty.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  pipeline::RunStats stats;
  std::ostringstream output;
  std::string error;
  auto logger = tests::MakeNullLogger();
  logger.error = [&error](const std::string& message) {
    error = message;
  };

  const auto rc = pipeline::commands::RunView(
      path.string(), output, options, logger, stats, 0, false, true, true);

  REQUIRE(rc == 1);
  CHECK(error.find("CSV file is empty") != std::string::npos);
  CHECK(error.find("header row") != std::string::npos);
  CHECK(output.str().empty());

  std::filesystem::remove(path);
}

TEST_CASE("csv-parser: CSVRow byte_offset reports data row source offsets") {
  SECTION("LF rows") {
    const auto text = std::string{"a,b\n1,2\n3,4\n"};
    const auto path = WriteTempCsv(text, "csvzall_view_offsets_lf.csv");
    csv::CSVReader reader(path.string(), csv::CSVFormat().header_row(0));
    auto it = reader.begin();
    REQUIRE(it != reader.end());
    REQUIRE(it->byte_offset() == 4);
    ++it;
    REQUIRE(it != reader.end());
    REQUIRE(it->byte_offset() == 8);
    std::filesystem::remove(path);
  }

  SECTION("CRLF rows") {
    const auto text = std::string{"a,b\r\n1,2\r\n3,4\r\n"};
    const auto path = WriteTempCsv(text, "csvzall_view_offsets_crlf.csv");
    csv::CSVReader reader(path.string(), csv::CSVFormat().header_row(0));
    auto it = reader.begin();
    REQUIRE(it != reader.end());
    REQUIRE(it->byte_offset() == 5);
    ++it;
    REQUIRE(it != reader.end());
    REQUIRE(it->byte_offset() == 10);
    std::filesystem::remove(path);
  }

  SECTION("quoted multiline rows") {
    const auto text = std::string{"a,b\n\"x\ny\",2\nz,3\n"};
    const auto path = WriteTempCsv(text, "csvzall_view_offsets_multiline.csv");
    csv::CSVReader reader(path.string(), csv::CSVFormat().header_row(0));
    auto it = reader.begin();
    REQUIRE(it != reader.end());
    REQUIRE(it->byte_offset() == 4);
    ++it;
    REQUIRE(it != reader.end());
    REQUIRE(it->byte_offset() == 12);
    std::filesystem::remove(path);
  }

  SECTION("stream rows") {
    std::stringstream input{"a,b\n1,2\n3,4\n"};
    csv::CSVReader reader(input, csv::CSVFormat().header_row(0));
    auto it = reader.begin();
    REQUIRE(it != reader.end());
    REQUIRE(it->byte_offset() == 4);
    ++it;
    REQUIRE(it != reader.end());
    REQUIRE(it->byte_offset() == 8);
  }
}

TEST_CASE("view: indexed reader returns requested row windows without storing all cells") {
  const auto csv = std::string{
      "name,note\n"
      "alice,first\n"
      "\"bob\nsmith\",multiline\n"
      "charlie,last\n"};
  const auto path = WriteTempCsv(csv, "csvzall_view_indexed_windows.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  pipeline::RunStats stats;
  const auto indexed = pipeline::commands::CsvIndexedFile::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  REQUIRE(indexed.headers() == std::vector<std::string>{"name", "note"});
  REQUIRE(indexed.row_count() == 3);
  REQUIRE(indexed.read_rows(0, 1) == std::vector<std::vector<std::string>>{{"alice", "first"}});
  REQUIRE(indexed.read_rows(1, 2) == std::vector<std::vector<std::string>>{
      {"bob\nsmith", "multiline"},
      {"charlie", "last"}});
  REQUIRE(indexed.read_rows(3, 10).empty());

  std::filesystem::remove(path);
}

TEST_CASE("view: auto mode materializes ordinary files for client-side operations") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}, {"charlie", "30"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_materialized.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Auto;
  options.view_materialize_threshold_mb = 200;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  REQUIRE(data.mode() == pipeline::commands::CsvViewDataMode::Materialized);
  REQUIRE(data.mode_name() == "materialized");
  REQUIRE(data.headers() == std::vector<std::string>{"name", "value"});
  REQUIRE(data.row_count() == 3);
  REQUIRE(data.read_rows(0, 3) == std::vector<std::vector<std::string>>{
      {"alice", "10"}, {"bob", "20"}, {"charlie", "30"}});
  REQUIRE(stats.rows_processed == 3);

  std::filesystem::remove(path);
}

TEST_CASE("view: forced paged mode keeps row-offset paging for ordinary files") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}, {"charlie", "30"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_forced_paged.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  REQUIRE(data.mode() == pipeline::commands::CsvViewDataMode::Paged);
  REQUIRE(data.mode_name() == "paged");
  REQUIRE(data.read_rows(1, 1) == std::vector<std::vector<std::string>>{{"bob", "20"}});

  std::filesystem::remove(path);
}

TEST_CASE("view: serves token-gated schema and row pages over localhost") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}, {"charlie", "30"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_server.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);

  httplib::Headers headers{{"X-Session-Token", "test-token"}};
  httplib::Result ready;
  for (int attempt = 0; attempt < 20; ++attempt) {
    ready = client.Get("/api/health", headers);
    if (ready && ready->status == 200) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  REQUIRE(ready);
  REQUIRE(ready->status == 200);

  const auto unauthorized = client.Get("/");
  REQUIRE(unauthorized);
  REQUIRE(unauthorized->status == 403);

  const auto viewer = client.Get("/?token=test-token");
  REQUIRE(viewer);
  REQUIRE(viewer->status == 200);
  REQUIRE(viewer->body.find("csvzall view") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"query-mode-search\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"query-mode-sql\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"clear-query\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"sql-error\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"sql-toolbar\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"add-chart\"") != std::string::npos);
  REQUIRE(viewer->body.find(">Charts</button>") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"heatmap-chart-dialog\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"heatmap-chart-form\" class=\"dialog-form\" novalidate") !=
          std::string::npos);
  REQUIRE(viewer->body.find("id=\"chart-type\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"chart-orientation\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"chart-color-scheme\"") != std::string::npos);
  const auto chart_layout_pos = viewer->body.find("id=\"chart-layout-section\"");
  const auto chart_range_pos = viewer->body.find("id=\"chart-range-section\"");
  const auto chart_orientation_pos = viewer->body.find("id=\"chart-orientation\"");
  REQUIRE(chart_layout_pos != std::string::npos);
  REQUIRE(chart_range_pos != std::string::npos);
  REQUIRE(chart_layout_pos < chart_orientation_pos);
  REQUIRE(chart_orientation_pos < chart_range_pos);
  REQUIRE(viewer->body.find("id=\"chart-presentation\"") != std::string::npos);
  REQUIRE(viewer->body.find("value=\"markdown-table\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"chart-markdown-columns\"") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"chart-markdown-sql\"") != std::string::npos);
  REQUIRE(viewer->body.find("Weight column") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"rename-column-dialog\"") != std::string::npos);
  REQUIRE(viewer->body.find(">Bar</option>") != std::string::npos);
  REQUIRE(viewer->body.find("id=\"chart-id\" type=\"text\" autocomplete=\"off\" required") ==
          std::string::npos);
  REQUIRE(viewer->body.find("id=\"chart-date-column\" required") == std::string::npos);
  REQUIRE(viewer->body.find("id=\"chart-output\" type=\"text\" autocomplete=\"off\" required") ==
          std::string::npos);

  const auto viewer_js = client.Get("/assets/viewer.js");
  REQUIRE(viewer_js);
  REQUIRE(viewer_js->status == 200);
  REQUIRE(viewer_js->body.find("csvzallViewBootstrap") != std::string::npos);
  REQUIRE(viewer_js->body.find("/api/chart-config/heatmap") != std::string::npos);
  REQUIRE(viewer_js->body.find("/api/sql-query") != std::string::npos);
  REQUIRE(viewer_js->body.find("VIEWER_SQL_TABLE_NAME") != std::string::npos);
  REQUIRE(viewer_js->body.find("chartType.value") != std::string::npos);
  REQUIRE(viewer_js->body.find("markdown-table") != std::string::npos);
  REQUIRE(viewer_js->body.find("selectedMarkdownColumns") != std::string::npos);
  REQUIRE(viewer_js->body.find("missingChartFields") == std::string::npos);
  REQUIRE(viewer_js->body.find("Complete the required fields") == std::string::npos);
  REQUIRE(viewer_js->body.find(".required") == std::string::npos);
  REQUIRE(viewer_js->body.find("checkValidity") == std::string::npos);
  REQUIRE(viewer_js->body.find("aria-invalid") != std::string::npos);
  REQUIRE(viewer_js->body.find("Row Before") != std::string::npos);
  REQUIRE(viewer_js->body.find("Insert Row Before") != std::string::npos);
  REQUIRE(viewer_js->body.find("Insert Row After") != std::string::npos);
  REQUIRE(viewer_js->body.find("Column After") != std::string::npos);
  REQUIRE(viewer_js->body.find("Rename Column") != std::string::npos);
  REQUIRE(viewer_js->body.find("dirty-state") != std::string::npos);
  REQUIRE(viewer_js->body.find("await loadChartList();\n        clearChartError();") !=
          std::string::npos);

  const auto ag_grid_css = client.Get("/assets/ag-grid.css");
  REQUIRE(ag_grid_css);
  REQUIRE(ag_grid_css->status == 200);
  REQUIRE(ag_grid_css->body.find(".ag-") != std::string::npos);

  const auto popright_css = client.Get("/assets/popright/styles.css");
  REQUIRE(popright_css);
  REQUIRE(popright_css->status == 200);
  REQUIRE(popright_css->body.find("popright-menu") != std::string::npos);

  const auto popright_js = client.Get("/assets/popright/index.js");
  REQUIRE(popright_js);
  REQUIRE(popright_js->status == 200);
  REQUIRE(popright_js->body.find("createContextMenu") != std::string::npos);
  REQUIRE(popright_js->body.find("createDropdownMenu") != std::string::npos);

  const auto popright_render_js = client.Get("/assets/popright/render/root.js");
  REQUIRE(popright_render_js);
  REQUIRE(popright_render_js->status == 200);
  REQUIRE(popright_render_js->body.find("createMenuRoot") != std::string::npos);

  const auto schema = client.Get("/api/schema", headers);
  REQUIRE(schema);
  REQUIRE(schema->status == 200);
  REQUIRE(
      schema->body ==
      R"({"file":"csvzall_view_server.csv","columns":["name","value"],"readOnly":true,"editable":false,"mode":"paged","totalRows":3,"sqlTableName":"data"})");

  const auto rows = client.Get("/api/rows?offset=1&limit=1", headers);
  REQUIRE(rows);
  REQUIRE(rows->status == 200);
  REQUIRE(rows->body.find("\"offset\":1") != std::string::npos);
  REQUIRE(rows->body.find("\"limit\":1") != std::string::npos);
  REQUIRE(rows->body.find("\"totalRows\":3") != std::string::npos);
  REQUIRE(rows->body.find("\"rows\":[[\"bob\",\"20\"]]") != std::string::npos);
  REQUIRE(rows->body.find("charlie") == std::string::npos);

  const auto clamped_rows = client.Get("/api/rows?offset=0&limit=999999", headers);
  REQUIRE(clamped_rows);
  REQUIRE(clamped_rows->status == 200);
  REQUIRE(clamped_rows->body.find("\"limit\":5000") != std::string::npos);

  const auto invalid_rows = client.Get("/api/rows?offset=-1&limit=1", headers);
  REQUIRE(invalid_rows);
  REQUIRE(invalid_rows->status == 400);

  const auto sql = client.Post(
      "/api/sql-query",
      headers,
      R"({"sql":"SELECT name AS person, value + 5 AS adjusted FROM data WHERE value >= 20 ORDER BY adjusted DESC"})",
      "application/json");
  REQUIRE(sql);
  REQUIRE(sql->status == 200);
  REQUIRE(sql->body.find("\"columns\":[\"person\",\"adjusted\"]") != std::string::npos);
  REQUIRE(sql->body.find("\"rows\":[[\"charlie\",\"35\"],[\"bob\",\"25\"]]") != std::string::npos);

  const auto empty_sql = client.Post("/api/sql-query", headers, R"({"sql":""})", "application/json");
  REQUIRE(empty_sql);
  REQUIRE(empty_sql->status == 200);
  REQUIRE(empty_sql->body.find("\"columns\":[\"name\",\"value\"]") != std::string::npos);
  REQUIRE(empty_sql->body.find("\"totalRows\":3") != std::string::npos);

  const auto bad_sql = client.Post(
      "/api/sql-query", headers, R"({"sql":"SELECT missing FROM data"})", "application/json");
  REQUIRE(bad_sql);
  REQUIRE(bad_sql->status == 400);
  REQUIRE(bad_sql->body.find("no such column") != std::string::npos);

  const auto health = client.Get("/api/health", headers);
  REQUIRE(health);
  REQUIRE(health->status == 200);
  REQUIRE(health->body.find("\"readOnly\":true") != std::string::npos);

  const auto forbidden = client.Get("/api/schema");
  REQUIRE(forbidden);
  REQUIRE(forbidden->status == 403);

  const auto wrong_token = client.Get("/api/schema", httplib::Headers{{"X-Session-Token", "wrong-token"}});
  REQUIRE(wrong_token);
  REQUIRE(wrong_token->status == 403);

  const auto post_rows = client.Post("/api/rows", headers, "", "application/json");
  REQUIRE(post_rows);
  REQUIRE(post_rows->status == 404);

  server.Stop();
  std::filesystem::remove(path);
}

#ifdef CSVZALL_HAVE_SVGPLOT
TEST_CASE("view: Add chart endpoint creates heatmap config and missing SVG output without editing CSV") {
#else
TEST_CASE("view: Add chart endpoint creates heatmap config without editing CSV") {
#endif
  const auto dir = TempDir("csvzall_view_chart_config");
  const auto path = dir / "gym.csv";
  {
    std::ofstream output(path, std::ios::binary);
    output << "date,count,note\n2026-01-01,1,Gym\n";
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);

  const auto unauthorized = client.Post(
      "/api/chart-config/heatmap",
      R"({"id":"gym-heatmap","date":"date","value":"count","label":"note","start":"2026-01-01","end":"2026-12-31","output":"charts/gym.svg","runOnSave":true})",
      "application/json");
  REQUIRE(unauthorized);
  REQUIRE(unauthorized->status == 403);

  httplib::Headers headers{{"X-Session-Token", "test-token"}};
  const auto response = client.Post(
      "/api/chart-config/heatmap",
      headers,
      R"({"id":"gym-heatmap","date":"date","value":"count","label":"note","start":"2026-01-01","end":"2026-12-31","title":"Gym","output":"charts/gym.svg","runOnSave":true})",
      "application/json");
  REQUIRE(response);
#ifdef CSVZALL_HAVE_SVGPLOT
  REQUIRE(response->status == 200);
  REQUIRE(response->body.find("\"ok\":true") != std::string::npos);
  REQUIRE(response->body.find("\"generated\":true") != std::string::npos);
#else
  REQUIRE(response->status == 400);
  REQUIRE(response->body.find("SVG chart rendering is disabled") != std::string::npos);
#endif

  const auto config_text = ReadTextFile(dir / ".csvzall" / "charts.json");
  CHECK(config_text.find("\"id\": \"gym-heatmap\"") != std::string::npos);
  CHECK(config_text.find("\"type\": \"heatmap\"") != std::string::npos);
  CHECK(config_text.find("\"input\": \"gym.csv\"") != std::string::npos);
  CHECK(config_text.find("\"output\": \"charts/gym.svg\"") != std::string::npos);
  CHECK(config_text.find("\"date\": \"date\"") != std::string::npos);
  CHECK(config_text.find("\"value\": \"count\"") != std::string::npos);
  CHECK(config_text.find("\"label\": \"note\"") != std::string::npos);
  CHECK(config_text.find("\"runOnSave\": true") != std::string::npos);
  CHECK(ReadTextFile(path).find("2026-01-01,1,Gym") != std::string::npos);
#ifdef CSVZALL_HAVE_SVGPLOT
  const auto chart_output = dir / "charts" / "gym.svg";
  CHECK(std::filesystem::exists(chart_output));
  const auto svg = ReadTextFile(chart_output);
  CHECK(svg.find("<svg") != std::string::npos);
  CHECK(svg.find("Gym") != std::string::npos);
#endif

  std::filesystem::remove_all(dir);
}

#ifdef CSVZALL_HAVE_SVGPLOT
TEST_CASE("view: Add chart endpoint creates markdown table config and output") {
  const auto dir = TempDir("csvzall_view_markdown_table_config");
  const auto path = dir / "events.csv";
  {
    std::ofstream output(path, std::ios::binary);
    output << "date,content,count\n2026-01-01,Gym|Lift,1\n";
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto response = client.Post(
      "/api/chart-config/heatmap",
      headers,
      R"({"type":"markdown-table","id":"events-table","columns":["content","date"],"output":"charts/events.md","runOnSave":true})",
      "application/json");
  REQUIRE(response);
  REQUIRE(response->status == 200);
  REQUIRE(response->body.find("\"generated\":true") != std::string::npos);

  const auto config_text = ReadTextFile(dir / ".csvzall" / "charts.json");
  CHECK(config_text.find("\"type\": \"markdown-table\"") != std::string::npos);
  CHECK(config_text.find("\"columns\":") != std::string::npos);
  CHECK(config_text.find("\"content\"") != std::string::npos);
  CHECK(config_text.find("\"date\"") != std::string::npos);
  CHECK(config_text.find("\"output\": \"charts/events.md\"") != std::string::npos);

  const auto markdown = ReadTextFile(dir / "charts" / "events.md");
  CHECK(markdown.find("| content   | date       |") != std::string::npos);
  CHECK(markdown.find("Gym\\|Lift") != std::string::npos);
  CHECK(markdown.find("count") == std::string::npos);

  std::filesystem::remove_all(dir);
}
#endif

#ifdef CSVZALL_HAVE_SVGPLOT
TEST_CASE("view edit: save regenerates runOnSave markdown table outputs") {
  const auto dir = TempDir("csvzall_view_markdown_table_save");
  const auto path = dir / "events.csv";
  std::filesystem::create_directories(dir / ".csvzall");
  {
    std::ofstream output(path, std::ios::binary);
    output << "name,value\nalpha,1\ngamma,3\n";
  }
  {
    std::ofstream config(dir / ".csvzall" / "charts.json", std::ios::binary);
    config
        << "{\n"
        << "  \"charts\": [\n"
        << "    {\"id\":\"events-table\",\"type\":\"markdown-table\",\"input\":\"events.csv\","
        << "\"output\":\"charts/events.md\",\"runOnSave\":true,"
        << "\"options\":{\"columns\":[\"name\",\"value\"]}}\n"
        << "  ]\n"
        << "}\n";
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto insert = client.Post(
      "/api/insert-row", headers, R"({"row":1,"values":["beta","2"]})",
      "application/json");
  REQUIRE(insert);
  REQUIRE(insert->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  CHECK(save->body.find("\"chartsGenerated\":1") != std::string::npos);
  server.Stop();

  const auto markdown = ReadTextFile(dir / "charts" / "events.md");
  CHECK(markdown.find("| beta  | 2     |") != std::string::npos);
  CHECK(markdown.find("| gamma | 3     |") != std::string::npos);

  std::filesystem::remove_all(dir);
}
#endif

TEST_CASE("view: Add chart endpoint lists and upserts current CSV charts") {
  const auto dir = TempDir("csvzall_view_chart_config_existing");
  std::filesystem::create_directories(dir / ".csvzall");
  std::filesystem::create_directories(dir / "data");
  {
    std::ofstream config(dir / ".csvzall" / "charts.json", std::ios::binary);
    config
        << "{\n"
        << "  \"charts\": [\n"
        << "    {\"id\":\"existing\",\"type\":\"heatmap\",\"input\":\"old.csv\",\"output\":\"old.svg\",\"options\":{\"date\":\"date\",\"start\":\"2026-01-01\",\"end\":\"2026-01-31\"}},\n"
        << "    {\"id\":\"gym-heatmap\",\"type\":\"heatmap\",\"input\":\"data/gym.csv\",\"output\":\"charts/old.svg\",\"options\":{\"date\":\"date\",\"value\":\"count\",\"start\":\"2026-01-01\",\"end\":\"2026-01-31\"}},\n"
        << "    {\"id\":\"gym-heatmap\",\"type\":\"heatmap\",\"input\":\"data/gym.csv\",\"output\":\"charts/duplicate.svg\",\"options\":{\"date\":\"date\",\"value\":\"count\",\"start\":\"2026-02-01\",\"end\":\"2026-02-28\"}}\n"
        << "  ]\n"
        << "}\n";
  }
  const auto path = dir / "data" / "gym.csv";
  {
    std::ofstream output(path, std::ios::binary);
    output << "date,count\n2026-01-01,1\n";
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto list_before = client.Get("/api/chart-config", headers);
  REQUIRE(list_before);
  REQUIRE(list_before->status == 200);
  CHECK(list_before->body.find("\"id\":\"gym-heatmap\"") != std::string::npos);
  const auto listed_chart = list_before->body.find("\"id\":\"gym-heatmap\"");
  CHECK(list_before->body.find("\"id\":\"gym-heatmap\"", listed_chart + 1) == std::string::npos);
  CHECK(list_before->body.find("\"id\":\"existing\"") == std::string::npos);

  const auto response = client.Post(
      "/api/chart-config/heatmap",
      headers,
      R"({"id":"gym-heatmap","date":"date","value":"count","lookback":"365d","end":"2026-12-31","output":"charts/gym.svg","runOnSave":false})",
      "application/json");
  REQUIRE(response);
#ifdef CSVZALL_HAVE_SVGPLOT
  REQUIRE(response->status == 200);
  CHECK(response->body.find("\"action\":\"updated\"") != std::string::npos);
#else
  REQUIRE(response->status == 400);
#endif

  const auto config_text = ReadTextFile(dir / ".csvzall" / "charts.json");
  CHECK(config_text.find("\"id\": \"existing\"") != std::string::npos);
  CHECK(config_text.find("\"id\": \"gym-heatmap\"") != std::string::npos);
  CHECK(config_text.find("\"input\": \"data/gym.csv\"") != std::string::npos);
  CHECK(config_text.find("\"output\": \"charts/gym.svg\"") != std::string::npos);
  CHECK(config_text.find("\"lookback\": \"365d\"") != std::string::npos);
  CHECK(config_text.find("\"runOnSave\": false") != std::string::npos);
  CHECK(config_text.find("duplicate.svg") == std::string::npos);

#ifdef CSVZALL_HAVE_SVGPLOT
  const auto generate = client.Post(
      "/api/chart-config/generate",
      headers,
      R"({"id":"gym-heatmap"})",
      "application/json");
  REQUIRE(generate);
  REQUIRE(generate->status == 200);
  CHECK(generate->body.find("\"generated\":true") != std::string::npos);
  CHECK(std::filesystem::exists(dir / "charts" / "gym.svg"));
#endif

  std::filesystem::remove_all(dir);
}

#ifdef CSVZALL_HAVE_SVGPLOT
TEST_CASE("view: Add chart endpoint creates bar chart configs") {
  const auto dir = TempDir("csvzall_view_bar_chart_config");
  const auto path = dir / "volume.csv";
  {
    std::ofstream output(path, std::ios::binary);
    output << "week,volume,cardio\n1,100,25\n2,125,30\n";
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto response = client.Post(
      "/api/chart-config/heatmap",
      headers,
      R"({"type":"bar","id":"volume-bar","label":"week","values":["volume","cardio"],"colorScheme":"diverging","presentation":"grouped","title":"Volume","output":"charts/volume.svg","runOnSave":true})",
      "application/json");
  REQUIRE(response);
  REQUIRE(response->status == 200);

  const auto config_text = ReadTextFile(dir / ".csvzall" / "charts.json");
  CHECK(config_text.find("\"type\": \"bar\"") != std::string::npos);
  CHECK(config_text.find("\"label\": \"week\"") != std::string::npos);
  CHECK(config_text.find("\"column\": \"volume\"") != std::string::npos);
  CHECK(config_text.find("\"column\": \"cardio\"") != std::string::npos);
  CHECK(config_text.find("\"colorScheme\": \"diverging\"") != std::string::npos);
  CHECK(config_text.find("\"presentation\": \"grouped\"") != std::string::npos);
  CHECK(ReadTextFile(dir / "charts" / "volume.svg").find("class=\"bar bar-grouped") != std::string::npos);

  std::filesystem::remove_all(dir);
}
#endif

#ifdef CSVZALL_HAVE_SVGPLOT
TEST_CASE("view: Add chart endpoint creates multi-value heatmap configs") {
  const auto dir = TempDir("csvzall_view_multi_value_chart_config");
  const auto path = dir / "training.csv";
  {
    std::ofstream output(path, std::ios::binary);
    output << "date,gym,bike,note\n2026-01-01,1,0,Squat\n2026-01-02,1,1,Brick\n";
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto response = client.Post(
      "/api/chart-config/heatmap",
      headers,
      R"({"id":"training-heatmap","date":"date","values":["gym","bike"],"label":"note","start":"2026-01-01","end":"2026-01-07","orientation":"months-vertical","title":"Training","output":"charts/training.svg","runOnSave":true})",
      "application/json");
  REQUIRE(response);
  REQUIRE(response->status == 200);

  const auto config_text = ReadTextFile(dir / ".csvzall" / "charts.json");
  CHECK(config_text.find("\"values\":") != std::string::npos);
  CHECK(config_text.find("\"column\": \"gym\"") != std::string::npos);
  CHECK(config_text.find("\"column\": \"bike\"") != std::string::npos);
  CHECK(config_text.find("\"orientation\": \"months-vertical\"") != std::string::npos);
  CHECK(ReadTextFile(dir / "charts" / "training.svg").find("gym + bike") != std::string::npos);

  std::filesystem::remove_all(dir);
}
#endif

#ifdef CSVZALL_HAVE_SVGPLOT
TEST_CASE("view: Add chart endpoint returns CLI validation errors before writing config") {
  const auto dir = TempDir("csvzall_view_chart_cli_validation");
  const auto path = dir / "volume.csv";
  {
    std::ofstream output(path, std::ios::binary);
    output << "week,volume\n1,100\n";
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto response = client.Post(
      "/api/chart-config/heatmap",
      headers,
      R"({"type":"bar","id":"volume-bar","label":"","value":"volume","title":"Volume","output":"charts/volume.svg","runOnSave":true})",
      "application/json");
  REQUIRE(response);
  REQUIRE(response->status == 400);
  CHECK(response->body.find("bar: label column is required") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(dir / ".csvzall" / "charts.json"));
  CHECK_FALSE(std::filesystem::exists(dir / "charts" / "volume.svg"));

  std::filesystem::remove_all(dir);
}
#endif

#ifdef CSVZALL_HAVE_SVGPLOT
TEST_CASE("view: Add chart endpoint returns heatmap render diagnostics") {
  const auto dir = TempDir("csvzall_view_chart_config_diagnostics");
  const auto path = dir / "gym.csv";
  {
    std::ofstream output(path, std::ios::binary);
    output << "date,content\n2026-01-01,Gym\n";
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto response = client.Post(
      "/api/chart-config/heatmap",
      headers,
      R"({"id":"bad-value","date":"date","value":"content","lookback":"365d","end":"2026-01-01","output":"charts/bad.svg","runOnSave":true})",
      "application/json");
  REQUIRE(response);
  REQUIRE(response->status == 400);
  CHECK(response->body.find("non-numeric heatmap value in column: content") != std::string::npos);
  CHECK(response->body.find("Choose no value/weight column") != std::string::npos);

  std::filesystem::remove_all(dir);
}
#endif

TEST_CASE("view: developer asset directory overrides first-party embedded viewer assets") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_dev_assets.csv");
  const auto asset_dir = WriteTempViewerAssets("csvzall_view_dev_assets");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token", asset_dir.string()}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto viewer = client.Get("/?token=test-token");
  REQUIRE(viewer);
  REQUIRE(viewer->status == 200);
  REQUIRE(viewer->body.find("dev viewer") != std::string::npos);

  const auto viewer_js = client.Get("/assets/viewer.js");
  REQUIRE(viewer_js);
  REQUIRE(viewer_js->status == 200);
  REQUIRE(viewer_js->body.find("csvzallDevViewer") != std::string::npos);

  const auto ag_grid_css = client.Get("/assets/ag-grid.css");
  REQUIRE(ag_grid_css);
  REQUIRE(ag_grid_css->status == 200);
  REQUIRE(ag_grid_css->body.find(".ag-") != std::string::npos);

  server.Stop();
  std::filesystem::remove(path);
  std::filesystem::remove_all(asset_dir);
}

TEST_CASE("view: developer asset directory resolves relative to source tree") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_source_relative_dev_assets.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Paged;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  const auto old_cwd = std::filesystem::current_path();
  std::filesystem::current_path(std::filesystem::temp_directory_path());
  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  const auto start_result = server.Start({0, false, false, "test-token", "src/viewer"});
  std::filesystem::current_path(old_cwd);
  REQUIRE(start_result == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto viewer = client.Get("/?token=test-token");
  REQUIRE(viewer);
  REQUIRE(viewer->status == 200);
  REQUIRE(viewer->body.find("csvzall view") != std::string::npos);

  server.Stop();
  std::filesystem::remove(path);
}

TEST_CASE("view: materialized server returns all rows for client-side grid") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}, {"charlie", "30"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_materialized_server.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_mode = pipeline::ViewModeSelection::Materialized;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);

  httplib::Client client("127.0.0.1", server.bound_port());
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  client.set_write_timeout(2, 0);
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto schema = client.Get("/api/schema", headers);
  REQUIRE(schema);
  REQUIRE(schema->status == 200);
  REQUIRE(schema->body.find(R"("mode":"materialized")") != std::string::npos);

  const auto rows = client.Get("/api/rows?offset=0&limit=999999", headers);
  REQUIRE(rows);
  REQUIRE(rows->status == 200);
  REQUIRE(rows->body.find("\"limit\":3") != std::string::npos);
  REQUIRE(rows->body.find("\"rows\":[[\"alice\",\"10\"],[\"bob\",\"20\"],[\"charlie\",\"30\"]]") != std::string::npos);

  server.Stop();
  std::filesystem::remove(path);
}

TEST_CASE("view edit: cell edits persist through save") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_cell.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto edit = client.Post(
      "/api/edit-cell", headers, R"({"row":1,"column":"value","value":"25"})",
      "application/json");
  REQUIRE(edit);
  REQUIRE(edit->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  server.Stop();

  REQUIRE(ReadAllRows(path) == std::vector<std::vector<std::string>>{{"alice", "10"}, {"bob", "25"}});
  std::filesystem::remove(path);
}

TEST_CASE("view edit: save follows a source file renamed on disk") {
  const auto dir = TempDir("csvzall_view_edit_renamed_source");
  const auto path = dir / "before.csv";
  const auto renamed = dir / "after.csv";
  {
    std::ofstream output(path, std::ios::binary);
    output << tests::MakeTestCsv({"name", "value"}, {{"alice", "10"}, {"bob", "20"}});
  }

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  std::filesystem::rename(path, renamed);

  const auto schema = client.Get("/api/schema", headers);
  REQUIRE(schema);
  REQUIRE(schema->status == 200);
  REQUIRE(
      schema->body ==
      R"({"file":"after.csv","columns":["name","value"],"readOnly":false,"editable":true,"mode":"materialized","totalRows":2,"sqlTableName":"data"})");

  const auto edit = client.Post(
      "/api/edit-cell", headers, R"({"row":1,"column":"value","value":"25"})",
      "application/json");
  REQUIRE(edit);
  REQUIRE(edit->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  server.Stop();

  REQUIRE_FALSE(std::filesystem::exists(path));
  REQUIRE(ReadAllRows(renamed) == std::vector<std::vector<std::string>>{{"alice", "10"}, {"bob", "25"}});
  std::filesystem::remove_all(dir);
}

TEST_CASE("view edit: row deletion persists through save") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}, {"charlie", "30"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_delete.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto del = client.Post("/api/delete-row", headers, R"({"row":1})", "application/json");
  REQUIRE(del);
  REQUIRE(del->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  server.Stop();

  REQUIRE(ReadAllRows(path) == std::vector<std::vector<std::string>>{{"alice", "10"}, {"charlie", "30"}});
  std::filesystem::remove(path);
}

TEST_CASE("view edit: arbitrary row insertion persists through save") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"charlie", "30"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_insert.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto insert = client.Post(
      "/api/insert-row", headers, R"({"row":1,"values":["bob","20"]})",
      "application/json");
  REQUIRE(insert);
  REQUIRE(insert->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  server.Stop();

  REQUIRE(ReadAllRows(path) == std::vector<std::vector<std::string>>{
      {"alice", "10"}, {"bob", "20"}, {"charlie", "30"}});
  std::filesystem::remove(path);
}

TEST_CASE("view edit: row swaps persist through save") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}, {"charlie", "30"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_swap.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto swap = client.Post(
      "/api/swap-rows", headers, R"({"first":1,"second":2})",
      "application/json");
  REQUIRE(swap);
  REQUIRE(swap->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  server.Stop();

  REQUIRE(ReadAllRows(path) == std::vector<std::vector<std::string>>{
      {"alice", "10"}, {"charlie", "30"}, {"bob", "20"}});
  std::filesystem::remove(path);
}

TEST_CASE("view edit: column insertion persists through save") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_insert_column.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto insert = client.Post(
      "/api/insert-column", headers, R"({"column":1,"name":"note","value":"x"})",
      "application/json");
  REQUIRE(insert);
  REQUIRE(insert->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  server.Stop();

  REQUIRE(ReadHeaders(path) == std::vector<std::string>{"name", "note", "value"});
  REQUIRE(ReadAllRows(path) == std::vector<std::vector<std::string>>{
      {"alice", "x", "10"}, {"bob", "x", "20"}});
  std::filesystem::remove(path);
}

TEST_CASE("view edit: column deletion persists through save") {
  const auto csv = tests::MakeTestCsv(
      {"name", "note", "value"},
      {{"alice", "x", "10"}, {"bob", "y", "20"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_delete_column.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto del = client.Post(
      "/api/delete-column", headers, R"({"column":"note"})", "application/json");
  REQUIRE(del);
  REQUIRE(del->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  server.Stop();

  REQUIRE(ReadHeaders(path) == std::vector<std::string>{"name", "value"});
  REQUIRE(ReadAllRows(path) == std::vector<std::vector<std::string>>{
      {"alice", "10"}, {"bob", "20"}});
  std::filesystem::remove(path);
}

TEST_CASE("view edit: column rename persists through save") {
  const auto csv = tests::MakeTestCsv(
      {"name", "note", "value"},
      {{"alice", "x", "10"}, {"bob", "y", "20"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_rename_column.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto rename = client.Post(
      "/api/rename-column", headers, R"({"column":"note","name":"status"})",
      "application/json");
  REQUIRE(rename);
  REQUIRE(rename->status == 200);
  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);
  server.Stop();

  REQUIRE(ReadHeaders(path) == std::vector<std::string>{"name", "status", "value"});
  REQUIRE(ReadAllRows(path) == std::vector<std::vector<std::string>>{
      {"alice", "x", "10"}, {"bob", "y", "20"}});
  std::filesystem::remove(path);
}

TEST_CASE("view edit: save applies column order with pending edits atomically") {
  const auto csv = tests::MakeTestCsv(
      {"name", "note", "value"},
      {{"alice", "x", "10"}, {"bob", "y", "20"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_reorder_column.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto edit = client.Post(
      "/api/edit-cell", headers, R"({"row":1,"column":"note","value":"edited"})",
      "application/json");
  REQUIRE(edit);
  REQUIRE(edit->status == 200);
  const auto save = client.Post(
      "/api/save", headers, R"({"columns":["value","name","note"]})",
      "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 200);

  const auto schema = client.Get("/api/schema", headers);
  REQUIRE(schema);
  REQUIRE(schema->status == 200);
  REQUIRE(
      schema->body ==
      R"({"file":"csvzall_view_edit_reorder_column.csv","columns":["value","name","note"],"readOnly":false,"editable":true,"mode":"materialized","totalRows":2,"sqlTableName":"data"})");

  const auto rows = client.Get("/api/rows?offset=0&limit=2", headers);
  REQUIRE(rows);
  REQUIRE(rows->status == 200);
  REQUIRE(rows->body.find("\"rows\":[[\"10\",\"alice\",\"x\"],[\"20\",\"bob\",\"edited\"]]") !=
          std::string::npos);
  server.Stop();

  REQUIRE(ReadHeaders(path) == std::vector<std::string>{"value", "name", "note"});
  REQUIRE(ReadAllRows(path) == std::vector<std::vector<std::string>>{
      {"10", "alice", "x"}, {"20", "bob", "edited"}});
  std::filesystem::remove(path);
}

TEST_CASE("view edit: reset reloads source from disk and discards unsaved changes") {
  const auto csv = tests::MakeTestCsv(
      {"name", "value"},
      {{"alice", "10"}, {"bob", "20"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_reset.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto edit = client.Post(
      "/api/edit-cell", headers, R"({"row":0,"column":"value","value":"99"})",
      "application/json");
  REQUIRE(edit);
  REQUIRE(edit->status == 200);
  const auto insert_column = client.Post(
      "/api/insert-column", headers, R"({"column":1,"name":"note","value":""})",
      "application/json");
  REQUIRE(insert_column);
  REQUIRE(insert_column->status == 200);

  const auto reset = client.Post("/api/reset", headers, "{}", "application/json");
  REQUIRE(reset);
  REQUIRE(reset->status == 200);

  const auto schema = client.Get("/api/schema", headers);
  REQUIRE(schema);
  REQUIRE(schema->status == 200);
  REQUIRE(
      schema->body ==
      R"({"file":"csvzall_view_edit_reset.csv","columns":["name","value"],"readOnly":false,"editable":true,"mode":"materialized","totalRows":2,"sqlTableName":"data"})");

  const auto rows = client.Get("/api/rows?offset=0&limit=2", headers);
  REQUIRE(rows);
  REQUIRE(rows->status == 200);
  REQUIRE(rows->body.find("\"rows\":[[\"alice\",\"10\"],[\"bob\",\"20\"]]") != std::string::npos);

  server.Stop();
  std::filesystem::remove(path);
}

TEST_CASE("view edit: save is token-gated and read-only mode rejects mutations") {
  const auto csv = tests::MakeTestCsv({"name", "value"}, {{"alice", "10"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_auth.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, false, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  const auto no_token = client.Post("/api/save", "{}", "application/json");
  REQUIRE(no_token);
  REQUIRE(no_token->status == 403);
  const auto readonly = client.Post(
      "/api/edit-cell", headers, R"({"row":0,"column":"value","value":"11"})",
      "application/json");
  REQUIRE(readonly);
  REQUIRE(readonly->status == 405);

  server.Stop();
  std::filesystem::remove(path);
}

TEST_CASE("view edit: save refuses when source file changed externally") {
  const auto csv = tests::MakeTestCsv({"name", "value"}, {{"alice", "10"}});
  const auto path = WriteTempCsv(csv, "csvzall_view_edit_conflict.csv");

  pipeline::RunOptions options;
  options.input_path = path.string();
  options.view_edit = true;
  pipeline::RunStats stats;
  const auto data = pipeline::commands::CsvViewData::Open(
      path.string(), options, tests::MakeNullLogger(), stats);

  pipeline::commands::ViewServer server(data, tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "test-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  httplib::Headers headers{{"X-Session-Token", "test-token"}};

  {
    std::ofstream external(path, std::ios::app | std::ios::binary);
    external << "external,99\n";
  }

  const auto save = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(save);
  REQUIRE(save->status == 409);
  REQUIRE(save->body.find("changed externally") != std::string::npos);

  server.Stop();
  std::filesystem::remove(path);
}

TEST_CASE("view: startup-json wraps the tokenized localhost URL") {
  const std::string url = "http://127.0.0.1:49152/?token=abc123";

  REQUIRE(pipeline::commands::FormatViewStartupOutput(url, false) == url);
  REQUIRE(
      pipeline::commands::FormatViewStartupOutput(url, true) ==
      R"({"url":"http://127.0.0.1:49152/?token=abc123"})");
}

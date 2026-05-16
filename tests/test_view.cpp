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
  REQUIRE(server.Start({0, false, "test-token"}) == 0);

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

  const auto schema = client.Get("/api/schema", headers);
  REQUIRE(schema);
  REQUIRE(schema->status == 200);
  REQUIRE(
      schema->body ==
      R"({"file":"csvzall_view_server.csv","columns":["name","value"],"readOnly":true,"mode":"paged","totalRows":3})");

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
  REQUIRE(server.Start({0, false, "test-token"}) == 0);

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

TEST_CASE("view: startup-json wraps the tokenized localhost URL") {
  const std::string url = "http://127.0.0.1:49152/?token=abc123";

  REQUIRE(pipeline::commands::FormatViewStartupOutput(url, false) == url);
  REQUIRE(
      pipeline::commands::FormatViewStartupOutput(url, true) ==
      R"({"url":"http://127.0.0.1:49152/?token=abc123"})");
}

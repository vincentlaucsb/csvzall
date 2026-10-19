#include <catch2/catch_test_macros.hpp>

#include "../src/viewer_core/indexed_csv.hpp"
#include "../src/pipeline/commands/view.hpp"
#include "../vendor/httplib/httplib.h"
#include "common_test_utils.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
struct CsvFixture {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("csvzall_dialect_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
  explicit CsvFixture(const std::string& bytes) { std::ofstream(path, std::ios::binary) << bytes; }
  ~CsvFixture() { std::error_code ec; std::filesystem::remove(path, ec); }
  std::string bytes() const {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
  auto open(csv::CSVFormat format = csv::CSVFormat().delimiter({',', '\t', ';', '|', '^'}).header_row(0)) const {
    return csvzall::viewer::CsvViewData::Open(path.string(), format);
  }
};
}

TEST_CASE("view dialect: ordinary saves preserve delimiter, quoting and BOM", "[view-dialect]") {
  for (const char delimiter : {',', '\t', ';', '|'}) {
    for (const char quote : {'"', '\''}) {
      for (const bool bom : {false, true}) {
        const std::string d(1, delimiter), q(1, quote);
        const auto prefix = bom ? std::string("\xEF\xBB\xBF") : std::string();
        CsvFixture file(prefix + "name" + d + "note\r\n" + q + "a" + d + "b" + q + d + q + "two\nlines" + q + "\r\n");
        auto data = file.open(csv::CSVFormat().delimiter(delimiter).quote(quote).header_row(0));
        CHECK(data.delimiter() == d);
        CHECK(data.quote() == q);
        CHECK(data.has_bom() == bom);
        data.save();
        CHECK(file.bytes() == prefix + "name" + d + "note\r\n" + q + "a" + d + "b" + q + d + q + "two\nlines" + q + "\r\n");
        data.edit_cell(0, "note", "one" + q + "quote");
        data.save();
        CHECK(data.read_rows(0, 1).at(0).at(1) == "one" + q + "quote");
        CHECK(file.bytes().find(d + q + "one" + q + q + "quote" + q) != std::string::npos);
        CHECK(data.has_bom() == bom);
      }
    }
  }
}

TEST_CASE("view dialect: toggling headers preserves first row and pending edits", "[view-dialect]") {
  CsvFixture file("name;note\nalice;original\n");
  auto data = file.open();
  data.edit_cell(0, "note", "edited");
  data.set_csv_options(";", "\"", false);
  CHECK(data.headers() == std::vector<std::string>{"A", "B"});
  CHECK(data.read_rows(0, 3) == std::vector<std::vector<std::string>>{{"name", "note"}, {"alice", "edited"}});
  data.set_csv_options(";", "\"", true);
  CHECK(data.headers() == std::vector<std::string>{"name", "note"});
  CHECK(data.read_rows(0, 3) == std::vector<std::vector<std::string>>{{"alice", "edited"}});
  data.set_csv_options(";", "\"", false);
  data.save();
  CHECK(file.bytes() == "name;note\nalice;edited\n");
  CHECK_FALSE(data.has_headers());
  CHECK(data.row_count() == 2);
  data.save();
  CHECK(file.bytes() == "name;note\nalice;edited\n");
}

TEST_CASE("view dialect: headerless edits and structural changes never serialize labels", "[view-dialect]") {
  CsvFixture file("alice|10\nbob|20\n");
  auto data = file.open(csv::CSVFormat().delimiter('|').no_header());
  CHECK(data.headers() == std::vector<std::string>{"A", "B"});
  data.edit_cell(0, "B", "11");
  data.insert_column(0, "", "prefix");
  CHECK(data.headers() == std::vector<std::string>{"A", "B", "C"});
  CHECK(data.read_rows(0, 1).at(0) == std::vector<std::string>{"prefix", "alice", "11"});
  data.delete_column("A");
  CHECK(data.read_rows(0, 1).at(0) == std::vector<std::string>{"alice", "11"});
  CHECK_THROWS(data.rename_column("A", "name"));
  data.save({"B", "A"});
  CHECK(file.bytes() == "11|alice\n20|bob\n");
  CHECK(data.headers() == std::vector<std::string>{"A", "B"});
}

TEST_CASE("view dialect: conversion preserves values and subsequent saves use selected dialect", "[view-dialect]") {
  CsvFixture file("\xEF\xBB\xBFname;note\na;\"x,y\"\n");
  auto data = file.open();
  data.set_csv_options(",", "'", true);
  CHECK(file.bytes() == "\xEF\xBB\xBFname;note\na;\"x,y\"\n");
  data.save();
  CHECK(file.bytes() == "\xEF\xBB\xBFname,note\na,'x,y'\n");
  data.edit_cell(0, "name", "b,c");
  data.save();
  CHECK(file.bytes() == "\xEF\xBB\xBFname,note\n'b,c','x,y'\n");
  data.set_csv_options("|", "\"", false);
  data.reset();
  CHECK(data.delimiter() == ",");
  CHECK(data.quote() == "'");
  CHECK(data.has_headers());
}

TEST_CASE("view dialect: single rows, empty cells and empty sources", "[view-dialect]") {
  for (const bool bom : {false, true}) {
    const auto prefix = bom ? std::string("\xEF\xBB\xBF") : std::string();
    CsvFixture file(prefix + "only\n");
    auto data = file.open();
    CHECK(data.row_count() == 0);
    data.set_csv_options(",", "\"", false);
    CHECK(data.read_rows(0, 1).at(0).at(0) == "only");
    data.edit_cell(0, "A", "");
    data.save();
    CHECK(file.bytes() == prefix + "\"\"\n");
    CHECK(data.row_count() == 1);
    data.delete_row(0);
    data.save();
    CHECK(file.bytes() == prefix);
    CHECK(data.row_count() == 0);
    data.insert_row(0, {"again"});
    data.save();
    CHECK(file.bytes() == prefix + "again\n");
  }
  CsvFixture empty("");
  CHECK_THROWS(empty.open());
}

TEST_CASE("view dialect: apostrophe-wrapped CSV text stays literal without structural quote evidence", "[view-dialect]") {
  for (const auto& value : {"'hello'", "'semi;colon'", "'pipe|value'", "'it''s'", "O'Brien"}) {
    const auto original = std::string("name,note\na,") + value + "\n";
    CsvFixture file(original);
    auto data = file.open();
    CHECK(data.quote() == "\"");
    CHECK(data.read_rows(0, 1).at(0).at(1) == value);
    data.save();
    CHECK(file.bytes() == original);
  }
}

TEST_CASE("view dialect: explicit input characters recover ambiguous conversions without writing", "[view-dialect]") {
  for (const auto& quote : {std::string("'"), std::string("~")}) {
    CsvFixture file("name,note\na,O'Brien~literal\n");
    auto data = file.open();
    data.set_csv_options(",", quote, true);
    CHECK_THROWS(data.read_csv_options(",", quote, true));
    data.save();
    const auto converted = file.bytes();
    auto reopened = file.open();
    reopened.read_csv_options(",", quote, true);
    CHECK(reopened.read_rows(0, 1).at(0).at(1) == "O'Brien~literal");
    CHECK(file.bytes() == converted);
    CHECK_THROWS(reopened.read_csv_options(",", ",", true));
    CHECK(reopened.quote() == quote);
    reopened.edit_cell(0, "name", "changed");
    CHECK_THROWS(reopened.read_csv_options(";", "\"", false));
    CHECK(reopened.read_rows(0, 1).at(0).at(0) == "changed");
    reopened.reset();
    reopened.read_csv_options(",", quote, false);
    CHECK(reopened.headers() == std::vector<std::string>{"A", "B"});
    CHECK(reopened.row_count() == 2);
    reopened.save();
    CHECK(file.bytes() == converted);
  }
}

TEST_CASE("view dialect: synthetic labels beyond Z and failed options are safe", "[view-dialect]") {
  CsvFixture file("first,second\na,b\n");
  auto data = file.open();
  data.set_csv_options(",", "\"", false);
  for (int i = 0; i < 26; ++i) data.insert_column(data.headers().size(), "", "");
  CHECK(data.headers().at(25) == "Z");
  CHECK(data.headers().at(26) == "AA");
  CHECK(data.headers().at(27) == "AB");
  CHECK_THROWS(data.set_csv_options(";", "'", true));
  CHECK(data.delimiter() == ",");
  CHECK_FALSE(data.has_headers());
  CHECK(data.row_count() == 2);
  CHECK_THROWS(data.set_csv_options(";", ";", false));
  data.set_csv_options(";", "", false);
  data.edit_cell(0, "A", "contains;delimiter");
  const auto original = file.bytes();
  CHECK_THROWS(data.save());
  CHECK(file.bytes() == original);
}

TEST_CASE("view dialect: native options endpoint validates authentication and preserves state", "[view-dialect]") {
  CsvFixture file("\xEF\xBB\xBFname;note\nalice;'two;values'\n");
  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto data = csvzall::pipeline::commands::CsvViewData::Open(
      file.path.string(), options, csvzall::tests::MakeNullLogger(), stats);
  csvzall::pipeline::commands::ViewServer server(data, csvzall::tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "dialect-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  const httplib::Headers headers{{"X-Session-Token", "dialect-token"}};
  const auto schema = client.Get("/api/schema", headers);
  REQUIRE(schema);
  CHECK(schema->body.find(R"("csvOptions":{"delimiter":";","quote":"'","hasHeaders":true,"bom":true})") != std::string::npos);
  const auto unauthorized = client.Post("/api/csv-options", "{}", "application/json");
  REQUIRE(unauthorized);
  CHECK(unauthorized->status == 403);
  auto response = client.Post("/api/csv-options", headers,
      R"({"delimiter":",","quote":"\"","hasHeaders":false})", "application/json");
  REQUIRE(response);
  CHECK(response->status == 200);
  response = client.Get("/api/rows?offset=0&limit=2", headers);
  REQUIRE(response);
  CHECK(response->body.find(R"("rows":[["name","note"],["alice","two;values"]])") != std::string::npos);
  response = client.Post("/api/csv-options", headers,
      R"({"delimiter":",","quote":",","hasHeaders":true})", "application/json");
  REQUIRE(response);
  CHECK(response->status == 400);
  response = client.Post("/api/save", headers, "{}", "application/json");
  REQUIRE(response);
  CHECK(response->status == 200);
  CHECK(file.bytes() == "\xEF\xBB\xBFname,note\nalice,two;values\n");
  const auto saved_bytes = file.bytes();
  response = client.Post("/api/read-csv-options", headers,
      R"({"delimiter":",","quote":"\"","hasHeaders":true})", "application/json");
  REQUIRE(response);
  CHECK(response->status == 200);
  CHECK(file.bytes() == saved_bytes);
  response = client.Post("/api/edit-cell", headers,
      R"({"row":0,"column":"name","value":"edited"})", "application/json");
  REQUIRE(response);
  CHECK(response->status == 200);
  response = client.Post("/api/read-csv-options", headers,
      R"({"delimiter":",","quote":"\"","hasHeaders":false})", "application/json");
  REQUIRE(response);
  CHECK(response->status == 400);
  CHECK(file.bytes() == saved_bytes);
  server.Stop();
  CHECK(server.Wait() == 0);

  csvzall::pipeline::commands::ViewServer readonly(data, csvzall::tests::MakeNullLogger());
  REQUIRE(readonly.Start({0, false, false, "dialect-token"}) == 0);
  httplib::Client readonly_client("127.0.0.1", readonly.bound_port());
  const auto denied = readonly_client.Post("/api/csv-options", headers, "{}", "application/json");
  REQUIRE(denied);
  CHECK(denied->status == 405);
  const auto reread_denied = readonly_client.Post("/api/read-csv-options", headers, "{}", "application/json");
  REQUIRE(reread_denied);
  CHECK(reread_denied->status == 405);
  readonly.Stop();
  CHECK(readonly.Wait() == 0);
}


TEST_CASE("view dialect: wider rows force headerless mode without losing records", "[view-dialect]") {
  for (const char separator : {',', '\t'}) {
    const std::string d(1, separator);
    CsvFixture file("a" + d + "b\n1" + d + "2\n3" + d + "4" + d + "EXTRA\n5" + d + "6\n7\n");
    const auto original = file.bytes();
    auto data = file.open(csv::CSVFormat().delimiter(separator).header_row(0));
    CHECK_FALSE(data.has_headers());
    CHECK(data.headers() == std::vector<std::string>{"A", "B", "C"});
    CHECK(data.row_count() == 5);
    CHECK(data.headers_disabled_reason().find("3 versus 2") != std::string::npos);
    CHECK(data.headers_disabled_reason().find("Add missing header names") != std::string::npos);
    const std::vector<std::vector<std::string>> expected{{"a", "b", ""}, {"1", "2", ""},
        {"3", "4", "EXTRA"}, {"5", "6", ""}, {"7", "", ""}};
    CHECK(data.read_rows(0, 100) == expected);
    for (std::size_t index = 0; index < expected.size(); ++index) {
      CHECK(data.read_rows(index, 1).at(0) == expected[index]);
    }
    CHECK_THROWS(data.set_csv_options(d, "\"", true));
    CHECK(file.bytes() == original);
    CHECK(data.read_rows(0, 100) == expected);
    data.edit_cell(2, "C", "kept");
    data.save();
    CHECK(file.bytes() == "a" + d + "b" + d + "\n1" + d + "2" + d + "\n3" + d + "4" + d + "kept\n5" + d + "6" + d + "\n7" + d + d + "\n");
    CHECK(data.row_count() == 5);
  }
}

TEST_CASE("view dialect: shorter records retain headers and page without skipping", "[view-dialect]") {
  CsvFixture file("a,b,c\n1,2,3\n4\n5,6\n7,8,9\n");
  auto data = file.open();
  CHECK(data.has_headers());
  CHECK(data.headers_disabled_reason().empty());
  const std::vector<std::vector<std::string>> expected{{"1", "2", "3"}, {"4", "", ""},
      {"5", "6", ""}, {"7", "8", "9"}};
  CHECK(data.row_count() == expected.size());
  CHECK(data.read_rows(0, 100) == expected);
  for (std::size_t index = 0; index < expected.size(); ++index) CHECK(data.read_rows(index, 1).at(0) == expected[index]);
  data.save();
  CHECK(data.read_rows(0, 100) == expected);
}

TEST_CASE("view dialect: correcting the first row restores header availability", "[view-dialect]") {
  CsvFixture file("a,b\n1,2,3\n");
  auto data = file.open();
  REQUIRE_FALSE(data.has_headers());
  REQUIRE_FALSE(data.headers_disabled_reason().empty());
  data.edit_cell(0, "C", "c");
  CHECK_THROWS(data.set_csv_options(",", "\"", true));
  data.save();
  CHECK(data.headers_disabled_reason().empty());
  data.set_csv_options(",", "\"", true);
  CHECK(data.headers() == std::vector<std::string>{"a", "b", "c"});
  CHECK(data.row_count() == 1);
  CHECK(data.read_rows(0, 1).at(0) == std::vector<std::string>{"1", "2", "3"});
  data.save();
  CHECK(file.bytes() == "a,b,c\n1,2,3\n");
  CHECK(file.open().has_headers());
}

TEST_CASE("view dialect: correcting input parsing clears the header restriction without writing", "[view-dialect]") {
  CsvFixture file("a;b,c\n1;2,3,extra\n");
  auto data = file.open(csv::CSVFormat().delimiter(',').header_row(0));
  REQUIRE_FALSE(data.headers_disabled_reason().empty());
  const auto original = file.bytes();
  data.read_csv_options(";", "\"", true);
  CHECK(data.has_headers());
  CHECK(data.headers_disabled_reason().empty());
  CHECK(file.bytes() == original);
  CHECK(data.read_rows(0, 1).at(0) == std::vector<std::string>{"1", "2,3,extra"});
}

TEST_CASE("view dialect: native API reports and enforces header restriction", "[view-dialect]") {
  CsvFixture file("a,b\n1,2\n3,4,EXTRA\n5,6\n");
  csvzall::pipeline::RunOptions options;
  csvzall::pipeline::RunStats stats;
  const auto data = csvzall::pipeline::commands::CsvViewData::Open(
      file.path.string(), options, csvzall::tests::MakeNullLogger(), stats);
  csvzall::pipeline::commands::ViewServer server(data, csvzall::tests::MakeNullLogger());
  REQUIRE(server.Start({0, false, true, "ragged-token"}) == 0);
  httplib::Client client("127.0.0.1", server.bound_port());
  const httplib::Headers headers{{"X-Session-Token", "ragged-token"}};
  auto response = client.Get("/api/schema", headers);
  REQUIRE(response);
  CHECK(response->body.find(R"("hasHeaders":false)") != std::string::npos);
  CHECK(response->body.find(R"("headersDisabledReason":)") != std::string::npos);
  CHECK(response->body.find(R"("totalRows":4)") != std::string::npos);
  response = client.Post("/api/csv-options", headers,
      R"({"delimiter":",","quote":"\"","hasHeaders":true})", "application/json");
  REQUIRE(response); CHECK(response->status == 400);
  CHECK(response->body.find("Headers are disabled") != std::string::npos);
  response = client.Get("/api/rows?offset=2&limit=1", headers);
  REQUIRE(response);
  CHECK(response->body.find(R"("rows":[["3","4","EXTRA"]])") != std::string::npos);
  server.Stop(); CHECK(server.Wait() == 0);
}


TEST_CASE("view dialect: sparse ragged samples do not collapse to an absent delimiter", "[view-dialect]") {
  for (const char separator : {',', '\t'}) {
    const std::string d(1, separator);
    CsvFixture file("cargo" + d + "destination\nGlitter" + d + "Moon\nThunder" + d + "Cloud" + d + "EXTRA\nSpoon\n");
    auto data = file.open();
    CHECK(data.delimiter() == d);
    CHECK_FALSE(data.has_headers());
    CHECK(data.row_count() == 4);
    CHECK(data.read_rows(2, 1).at(0) == std::vector<std::string>{"Thunder", "Cloud", "EXTRA"});
  }
  CsvFixture single("name; literal punctuation\nplain value\nanother value\n");
  auto data = single.open();
  CHECK(data.headers().size() == 1);
  CHECK(data.read_rows(0, 1).at(0) == std::vector<std::string>{"plain value"});
}

TEST_CASE("view dialect: blank records cannot shift page offsets", "[view-dialect]") {
  CsvFixture file("a,b\n1,2\n\n3,4\n");
  auto data = file.open(csv::CSVFormat().delimiter(',').header_row(0));
  const std::vector<std::vector<std::string>> expected{{"1", "2"}, {"", ""}, {"3", "4"}};
  CHECK(data.row_count() == expected.size());
  CHECK(data.read_rows(0, 100) == expected);
  for (std::size_t index = 0; index < expected.size(); ++index) CHECK(data.read_rows(index, 1).at(0) == expected[index]);
}

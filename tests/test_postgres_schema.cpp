#include <catch2/catch_test_macros.hpp>

#ifdef CSVZALL_HAVE_POSTGRESQL

#include "../src/pipeline/postgres/schema_inference.hpp"

#include <vector>
#include <string>

using namespace csvzall;

namespace {

std::vector<std::string> ColumnTypes(const std::vector<pipeline::postgres::InferredColumn>& cols) {
  std::vector<std::string> out;
  out.reserve(cols.size());
  for (const auto& col : cols) {
    out.push_back(pipeline::postgres::ColumnTypeToString(col.type));
  }
  return out;
}

}  // namespace

TEST_CASE("PostgresSchemaInference: infers integer numeric timestamp text") {
  pipeline::postgres::SchemaInference inference({"year", "price", "listed_at", "make"});

  inference.observe_row({"2020", "24999.5", "2024-01-10 09:30:00", "Toyota"});
  inference.observe_row({"2021", "31000.0", "2024-03-10 10:00:00", "Honda"});
  inference.observe_row({"2022", "15000.0", "2024-05-01 12:15:10", "Ford"});

  const auto cols = inference.finalize();
  REQUIRE(cols.size() == 4);

  const auto types = ColumnTypes(cols);
  REQUIRE(types[0] == "INTEGER");
  REQUIRE(types[1] == "NUMERIC");
  REQUIRE(types[2] == "TIMESTAMP");
  REQUIRE(types[3] == "TEXT");
}

TEST_CASE("PostgresSchemaInference: mixed values default to text conservatively") {
  pipeline::postgres::SchemaInference inference({"odometer"});

  inference.observe_row({"12345"});
  inference.observe_row({"unknown"});
  inference.observe_row({"67000"});

  const auto cols = inference.finalize();
  REQUIRE(cols.size() == 1);
  REQUIRE(pipeline::postgres::ColumnTypeToString(cols[0].type) == "TEXT");
}

TEST_CASE("PostgresSchemaInference: high null ratio defaults to text") {
  pipeline::postgres::SchemaInference inference({"price"});

  inference.observe_row({""});
  inference.observe_row({""});
  inference.observe_row({"25000"});

  const auto cols = inference.finalize();
  REQUIRE(cols.size() == 1);
  REQUIRE(pipeline::postgres::ColumnTypeToString(cols[0].type) == "TEXT");
}

TEST_CASE("PostgresSchemaInference: samples capped at 1000 rows") {
  pipeline::postgres::SchemaInference inference({"year"});

  for (int i = 0; i < 1000; ++i) {
    inference.observe_row({"2020"});
  }
  REQUIRE(inference.has_enough_samples());

  // This additional mixed row should be ignored because sampling is capped.
  inference.observe_row({"not_a_year"});

  const auto cols = inference.finalize();
  REQUIRE(cols.size() == 1);
  REQUIRE(pipeline::postgres::ColumnTypeToString(cols[0].type) == "INTEGER");
}

#endif  // CSVZALL_HAVE_POSTGRESQL

include(FetchContent)

# Catch2 (testing framework)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(csvzall_tests
  ${CSVZALL_VIEWER_ASSET_CPP}
  ${CSVZALL_VIEWER_ASSET_HPP}
  test_filter.cpp
  test_derive.cpp
  test_head.cpp
  test_sql_query.cpp
  test_sql_export.cpp
  test_sqlite_db.cpp
  test_postgres_schema.cpp
  test_json_extract.cpp
  test_csv_utilities.cpp
  test_markdown_calendar.cpp
  test_summarize.cpp
  test_timeseries.cpp
  ../src/credentials.cpp
  ../src/util.cpp
  ../src/transform_pipeline.cpp
  ../src/pipeline/common/chart_spec.cpp
  ../src/pipeline/common/column_lookup.cpp
  ../src/pipeline/common/gzip_stream.cpp
  ../src/pipeline/common/json_path.cpp
  ../src/pipeline/common/markdown_calendar.cpp
  ../src/pipeline/common/markdown_table.cpp
  ../src/pipeline/common/row_utils.cpp
  ../src/pipeline/sqlite/sqlite_db.cpp
  ../src/pipeline/sqlite/csv_loader.cpp
  ../src/pipeline/commands/append.cpp
  ../src/pipeline/commands/CsvInputCommand.cpp
  ../src/pipeline/commands/CsvTransformCommand.cpp
  ../src/pipeline/commands/derive.cpp
  ../src/pipeline/commands/filter.cpp
  ../src/pipeline/commands/json_extract.cpp
  ../src/pipeline/commands/max.cpp
  ../src/pipeline/commands/sql_query.cpp
  ../src/pipeline/commands/sql_export.cpp
  ../src/pipeline/commands/summarize.cpp
  ../src/pipeline/commands/timeseries.cpp
  ../src/pipeline/commands/view.cpp
  ../src/pipeline/commands/view/assets.cpp
  ../src/pipeline/commands/view/charts.cpp
  ../src/pipeline/commands/view/json.cpp
  ../src/pipeline/commands/view/model.cpp
  ../src/pipeline/commands/view/server.cpp
  ../src/pipeline/commands/view/support.cpp
  test_view.cpp)

if(CSVZALL_HAVE_SVGPLOT)
  target_sources(csvzall_tests PRIVATE
    test_heatmap.cpp
    ../src/pipeline/commands/charts.cpp
    ../src/pipeline/commands/heatmap.cpp)
endif()

if(CSVZALL_HAVE_POSTGRESQL)
  target_sources(csvzall_tests PRIVATE
    ../src/pipeline/postgres/postgres_connection.cpp
    ../src/pipeline/postgres/schema_inference.cpp
    ../src/pipeline/postgres/row_loader.cpp
    ../src/pipeline/commands/postgres_export.cpp)
endif()

target_link_libraries(csvzall_tests
  PRIVATE
    Catch2::Catch2WithMain
    ${CSVZALL_ARGPARSE_TARGET}
    ${CSVZALL_INDICATORS_TARGET}
    ${CSVZALL_SIMDJSON_TARGET}
    csv
    SQLiteCpp
    Threads::Threads
    ${CSVZALL_ZLIB_TARGET})

if(CSVZALL_HAVE_POSTGRESQL)
  target_link_libraries(csvzall_tests PRIVATE ${CSVZALL_LIBPQXX_TARGET})
endif()
if(CSVZALL_HAVE_KEYCHAIN)
  target_link_libraries(csvzall_tests PRIVATE ${CSVZALL_KEYCHAIN_TARGET})
endif()
if(CSVZALL_HAVE_SVGPLOT)
  target_link_libraries(csvzall_tests PRIVATE ${CSVZALL_SVGPLOT_TARGET})
endif()

csvzall_copy_runtime_dependencies(csvzall_tests)

target_include_directories(csvzall_tests
  PRIVATE
    "${CSVZALL_VIEWER_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/..")

set_property(TARGET csvzall_tests PROPERTY CXX_STANDARD ${CSVZALL_CXX_STANDARD})
set_property(TARGET csvzall_tests PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET csvzall_tests PROPERTY CXX_EXTENSIONS OFF)

target_compile_definitions(csvzall_tests PRIVATE CSVZALL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")

if(CSVZALL_HAVE_POSTGRESQL)
  target_compile_definitions(csvzall_tests PRIVATE CSVZALL_HAVE_POSTGRESQL=1)
endif()
if(CSVZALL_HAVE_KEYCHAIN)
  target_compile_definitions(csvzall_tests PRIVATE CSVZALL_HAVE_KEYCHAIN=1)
endif()
if(CSVZALL_HAVE_SVGPLOT)
  target_compile_definitions(csvzall_tests PRIVATE CSVZALL_HAVE_SVGPLOT=1)
endif()

if(MSVC)
  target_compile_options(csvzall_tests PRIVATE /W4 /permissive- /bigobj)
  if(CSVZALL_WARNINGS_AS_ERRORS)
    target_compile_options(csvzall_tests PRIVATE /WX)
  endif()
else()
  target_compile_options(csvzall_tests PRIVATE -Wall -Wextra -Wpedantic)
  if(CSVZALL_WARNINGS_AS_ERRORS)
    target_compile_options(csvzall_tests PRIVATE -Werror)
  endif()
endif()

add_test(NAME csvzall_tests COMMAND csvzall_tests)

add_test(
  NAME csvzall_cli_smoke
  COMMAND
    ${CMAKE_COMMAND}
    -DCSVZALL_EXE=$<TARGET_FILE:csvzall>
    -DCSVZALL_HAVE_SVGPLOT=$<BOOL:${CSVZALL_HAVE_SVGPLOT}>
    -DCLI_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-smoke
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake)

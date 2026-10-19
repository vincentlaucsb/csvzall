include(FetchContent)

# Catch2 (testing framework)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(csvzall_tests
  test_filter.cpp
  test_derive.cpp
  test_head.cpp
  test_sql_query.cpp
  test_sql_export.cpp
  test_sql_edges.cpp
  test_sql_functions.cpp
  test_sql_tables.cpp
  test_sqlite_db.cpp
  test_postgres_schema.cpp
  test_json_extract.cpp
  test_csv_utilities.cpp
  test_markdown_calendar.cpp
  test_summarize.cpp
  test_timeseries.cpp
  test_view.cpp)

if(CSVZALL_HAVE_SVGPLOT)
  target_sources(csvzall_tests PRIVATE
    test_heatmap.cpp)
endif()

target_link_libraries(csvzall_tests
  PRIVATE
    Catch2::Catch2WithMain
    csvzall::core)

csvzall_copy_runtime_dependencies(csvzall_tests)

target_include_directories(csvzall_tests
  PRIVATE
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

# A tiny loadable SQLite extension proves `sql query --load-extension` loads
# native code. SQLiteCpp bundles sqlite3.h but not sqlite3ext.h, so fetch the
# loadable-extension header from the matching SQLite release (test-only; the
# sqlite3_api_routines table is append-only, so later SQLite builds accept it).
set(CSVZALL_TEST_SQLITE3EXT_DIR "${CMAKE_CURRENT_BINARY_DIR}/sqlite3ext")
set(CSVZALL_TEST_SQLITE3EXT "${CSVZALL_TEST_SQLITE3EXT_DIR}/sqlite3ext.h")
set(CSVZALL_TEST_SQLITE3EXT_SHA256 "b184dd1586d935133d37ad76fa353faf0a1021ff2fdedeedcc3498fff74bbb94")
function(csvzall_sqlite3ext_is_valid out_var)
  set(_valid OFF)
  if(EXISTS "${CSVZALL_TEST_SQLITE3EXT}")
    file(SHA256 "${CSVZALL_TEST_SQLITE3EXT}" _hash)
    if(_hash STREQUAL CSVZALL_TEST_SQLITE3EXT_SHA256)
      set(_valid ON)
    endif()
  endif()
  set(${out_var} ${_valid} PARENT_SCOPE)
endfunction()

csvzall_sqlite3ext_is_valid(_csvzall_sqlite3ext_valid)
if(NOT _csvzall_sqlite3ext_valid)
  # Download without EXPECTED_HASH: with it, a network failure is a fatal
  # configure error. Verify the hash ourselves and only then move it in place,
  # so an offline configure skips these tests instead of failing.
  file(REMOVE "${CSVZALL_TEST_SQLITE3EXT}")
  set(_csvzall_sqlite3ext_tmp "${CSVZALL_TEST_SQLITE3EXT}.download")
  file(DOWNLOAD
    "https://raw.githubusercontent.com/sqlite/sqlite/version-3.46.1/src/sqlite3ext.h"
    "${_csvzall_sqlite3ext_tmp}"
    STATUS _csvzall_sqlite3ext_status)
  list(GET _csvzall_sqlite3ext_status 0 _csvzall_sqlite3ext_code)
  set(_csvzall_sqlite3ext_hash "")
  if(_csvzall_sqlite3ext_code EQUAL 0)
    file(SHA256 "${_csvzall_sqlite3ext_tmp}" _csvzall_sqlite3ext_hash)
  endif()
  if(_csvzall_sqlite3ext_hash STREQUAL CSVZALL_TEST_SQLITE3EXT_SHA256)
    file(RENAME "${_csvzall_sqlite3ext_tmp}" "${CSVZALL_TEST_SQLITE3EXT}")
  else()
    file(REMOVE "${_csvzall_sqlite3ext_tmp}")
    message(STATUS
      "sqlite3ext.h download failed or did not match its SHA-256; "
      "skipping --load-extension success tests: ${_csvzall_sqlite3ext_status}")
  endif()
endif()
set(CSVZALL_TEST_EXTENSION_ARGS "")
if(EXISTS "${CSVZALL_TEST_SQLITE3EXT}")
  add_library(csvzall_test_sqlite_extension MODULE test_sqlite_extension.c)
  target_include_directories(csvzall_test_sqlite_extension
    PRIVATE
      "${CSVZALL_TEST_SQLITE3EXT_DIR}"
      "$<TARGET_PROPERTY:sqlite3,INTERFACE_INCLUDE_DIRECTORIES>")
  add_dependencies(csvzall_tests csvzall_test_sqlite_extension)
  target_compile_definitions(csvzall_tests
    PRIVATE "CSVZALL_TEST_EXTENSION_PATH=\"$<TARGET_FILE:csvzall_test_sqlite_extension>\"")
  set(CSVZALL_TEST_EXTENSION_ARGS "-DCSVZALL_TEST_EXTENSION=$<TARGET_FILE:csvzall_test_sqlite_extension>")
endif()

add_test(NAME csvzall_tests COMMAND csvzall_tests)

find_program(CSVZALL_NODE_EXE NAMES node nodejs)
if(CSVZALL_NODE_EXE)
  add_test(
    NAME csvzall_viewer_asset_build_test
    COMMAND "${CMAKE_COMMAND}" -E env
      "CSVZALL_TEST_CMAKE=${CMAKE_COMMAND}"
      "CSVZALL_TEST_GENERATOR=${CMAKE_GENERATOR}"
      "CSVZALL_TEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/viewer-asset-fixtures"
      "${CSVZALL_NODE_EXE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/viewer_assets_build.test.mjs")
  add_test(
    NAME csvzall_viewer_state_tests
    COMMAND ${CSVZALL_NODE_EXE} --test ${CMAKE_CURRENT_SOURCE_DIR}/viewer_state.test.mjs ${CMAKE_CURRENT_SOURCE_DIR}/viewer_adapters.test.mjs ${CMAKE_CURRENT_SOURCE_DIR}/table_form.test.mjs ${CMAKE_CURRENT_SOURCE_DIR}/chart_form.test.mjs ${CMAKE_CURRENT_SOURCE_DIR}/viewer_theme.test.mjs ${CMAKE_CURRENT_SOURCE_DIR}/viewer_dialog.test.mjs ${CMAKE_CURRENT_SOURCE_DIR}/viewer_menu.test.mjs ${CMAKE_CURRENT_SOURCE_DIR}/compact_layout.test.mjs)
endif()

add_test(
  NAME csvzall_cli_smoke
  COMMAND
    ${CMAKE_COMMAND}
    -DCSVZALL_EXE=$<TARGET_FILE:csvzall>
    -DCSVZALL_HAVE_SVGPLOT=$<BOOL:${CSVZALL_HAVE_SVGPLOT}>
    -DCSVZALL_HAVE_COMPRESSED_INPUT=$<BOOL:${CSVZALL_ENABLE_COMPRESSED_INPUT}>
    ${CSVZALL_TEST_EXTENSION_ARGS}
    -DCLI_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-smoke
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake)

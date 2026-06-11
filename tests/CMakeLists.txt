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

add_test(NAME csvzall_tests COMMAND csvzall_tests)

find_program(CSVZALL_NODE_EXE NAMES node nodejs)
if(CSVZALL_NODE_EXE)
  add_test(
    NAME csvzall_viewer_state_tests
    COMMAND ${CSVZALL_NODE_EXE} --test ${CMAKE_CURRENT_SOURCE_DIR}/viewer_state.test.mjs)
endif()

add_test(
  NAME csvzall_cli_smoke
  COMMAND
    ${CMAKE_COMMAND}
    -DCSVZALL_EXE=$<TARGET_FILE:csvzall>
    -DCSVZALL_HAVE_SVGPLOT=$<BOOL:${CSVZALL_HAVE_SVGPLOT}>
    -DCLI_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-smoke
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake)

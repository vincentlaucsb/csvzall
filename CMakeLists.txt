cmake_minimum_required(VERSION 3.25)

project(csvzall VERSION 0.1.0 LANGUAGES CXX)

set(CSVZALL_CXX_STANDARD 26 CACHE STRING "C++ standard used by csvzall and in-tree dependencies")

set(CMAKE_CXX_STANDARD ${CSVZALL_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep runtime library consistent across all targets, including csv-parser subdirectory.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
include(GNUInstallDirs)
include(CTest)

option(CSVZALL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CSVZALL_ENABLE_POSTGRESQL "Enable PostgreSQL export support" ON)
option(CSVZALL_FETCH_LIBPQXX "Fetch libpqxx from GitHub when not found on system" ON)
option(CSVZALL_REQUIRE_POSTGRESQL "Fail configure if PostgreSQL support cannot be enabled" OFF)
option(CSVZALL_FETCH_CSV_PARSER "Fetch csv-parser when no explicit or preferred local checkout is selected" ON)
option(CSVZALL_PREFER_SIBLING_CSV_PARSER "Prefer ../csv-parser over other csv-parser sources when it exists" ON)

# Share FetchContent downloads across Visual Studio/Ninja build directories.
# Without this, x64-Debug, x64-Release, and scratch configure dirs each create
# their own _deps tree and may repeatedly hit GitHub for the same dependency.
set(CSVZALL_FETCHCONTENT_BASE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/out/fetchcontent"
    CACHE PATH "Shared FetchContent cache directory for csvzall dependencies")
set(FETCHCONTENT_BASE_DIR
    "${CSVZALL_FETCHCONTENT_BASE_DIR}"
    CACHE PATH "Shared FetchContent base directory" FORCE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update already-populated FetchContent dependencies" FORCE)
message(STATUS "FetchContent cache: ${FETCHCONTENT_BASE_DIR}")

# csv-parser location strategy:
# 1) User-provided -DCSV_PARSER_ROOT=...
# 2) Sibling checkout at ../csv-parser when CSVZALL_PREFER_SIBLING_CSV_PARSER=ON
# 3) FetchContent for normal users and CI
# 4) In-repo submodule at external/csv-parser
set(CSV_PARSER_ROOT "" CACHE PATH "Path to csv-parser checkout (must contain include/ and single_include/)")

set(CSVZALL_IN_REPO_CSV_PARSER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/csv-parser")
set(CSVZALL_SIBLING_CSV_PARSER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../csv-parser")
get_filename_component(CSVZALL_IN_REPO_CSV_PARSER_ROOT "${CSVZALL_IN_REPO_CSV_PARSER_ROOT}" ABSOLUTE)
get_filename_component(CSVZALL_SIBLING_CSV_PARSER_ROOT "${CSVZALL_SIBLING_CSV_PARSER_ROOT}" ABSOLUTE)

set(CSV_BUILD_PROGRAMS OFF CACHE BOOL "Disable csv-parser program targets" FORCE)
set(BUILD_PYTHON OFF CACHE BOOL "Disable csv-parser Python bindings" FORCE)
set(CSV_BUILD_SINGLE_INCLUDE_TEST OFF CACHE BOOL "Disable csv-parser single include test" FORCE)
set(CSV_CXX_STANDARD "${CSVZALL_CXX_STANDARD}" CACHE STRING "C++ standard for csv-parser" FORCE)

set(CSVZALL_LOCAL_CSV_PARSER_ROOT "")
if(NOT CSV_PARSER_ROOT STREQUAL "")
  get_filename_component(CSVZALL_LOCAL_CSV_PARSER_ROOT "${CSV_PARSER_ROOT}" ABSOLUTE)
elseif(CSVZALL_PREFER_SIBLING_CSV_PARSER AND EXISTS "${CSVZALL_SIBLING_CSV_PARSER_ROOT}")
  set(CSVZALL_LOCAL_CSV_PARSER_ROOT "${CSVZALL_SIBLING_CSV_PARSER_ROOT}")
endif()

if(NOT CSVZALL_LOCAL_CSV_PARSER_ROOT STREQUAL "")
  if(NOT EXISTS "${CSVZALL_LOCAL_CSV_PARSER_ROOT}/include" OR NOT EXISTS "${CSVZALL_LOCAL_CSV_PARSER_ROOT}/single_include")
    message(FATAL_ERROR
      "csv-parser not found or invalid: ${CSVZALL_LOCAL_CSV_PARSER_ROOT}\n"
      "Set -DCSV_PARSER_ROOT=<path-to-csv-parser>, fix ../csv-parser, or unset CSV_PARSER_ROOT.")
  endif()

  message(STATUS "Using csv-parser from: ${CSVZALL_LOCAL_CSV_PARSER_ROOT}")
  add_subdirectory("${CSVZALL_LOCAL_CSV_PARSER_ROOT}" "${CMAKE_BINARY_DIR}/csv-parser" EXCLUDE_FROM_ALL)
elseif(CSVZALL_FETCH_CSV_PARSER)
  message(STATUS "Fetching csv-parser with FetchContent")
  FetchContent_Declare(
    csv_parser
    GIT_REPOSITORY https://github.com/vincentlaucsb/csv-parser.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(csv_parser)
elseif(EXISTS "${CSVZALL_IN_REPO_CSV_PARSER_ROOT}")
  message(STATUS "Using csv-parser from: ${CSVZALL_IN_REPO_CSV_PARSER_ROOT}")
  add_subdirectory("${CSVZALL_IN_REPO_CSV_PARSER_ROOT}" "${CMAKE_BINARY_DIR}/csv-parser" EXCLUDE_FROM_ALL)
else()
  message(FATAL_ERROR
    "csv-parser not found.\n"
    "Enable CSVZALL_FETCH_CSV_PARSER, set -DCSV_PARSER_ROOT=<path-to-csv-parser>, or provide one of:\n"
    "  ${CSVZALL_SIBLING_CSV_PARSER_ROOT}\n"
    "  ${CSVZALL_IN_REPO_CSV_PARSER_ROOT}")
endif()

if(NOT TARGET csv)
  message(FATAL_ERROR "csv-parser target 'csv' was not created. Check CSV_PARSER_ROOT and version.")
endif()

# argparse (header-only)
FetchContent_Declare(
  argparse
  GIT_REPOSITORY https://github.com/p-ranav/argparse.git
  GIT_TAG v3.1
)
FetchContent_MakeAvailable(argparse)

set(CSVZALL_ARGPARSE_TARGET argparse)
if(TARGET argparse::argparse)
  set(CSVZALL_ARGPARSE_TARGET argparse::argparse)
endif()

# indicators (header-only terminal progress bars/spinners)
set(INDICATORS_INSTALL OFF CACHE BOOL "Disable indicators install target" FORCE)
set(INDICATORS_PACKAGE OFF CACHE BOOL "Disable indicators package target" FORCE)
set(INDICATORS_BUILD_TESTS OFF CACHE BOOL "Disable indicators tests" FORCE)
set(INDICATORS_SAMPLES OFF CACHE BOOL "Disable indicators samples" FORCE)
set(INDICATORS_DEMO OFF CACHE BOOL "Disable indicators demo" FORCE)
FetchContent_Declare(
  indicators
  GIT_REPOSITORY https://github.com/p-ranav/indicators.git
  GIT_TAG v2.3
  GIT_SHALLOW TRUE
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(indicators)

set(CSVZALL_INDICATORS_TARGET indicators)
if(TARGET indicators::indicators)
  set(CSVZALL_INDICATORS_TARGET indicators::indicators)
endif()

# SQLiteCpp — RAII C++ wrapper with bundled internal SQLite.
# Patched via cmake/patches/SQLiteCpp-bind-text-with-length.patch to add a
# bind(int, const char*, int) text-with-length overload (no null termination required).
# The patch is applied by a CMake script so Visual Studio configure does not
# require git.exe to be on PATH.
# SQLITECPP_INTERNAL_SQLITE is ON by default; no system SQLite required.
FetchContent_Declare(
  SQLiteCpp
  GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
  GIT_TAG 3.3.2
  GIT_SHALLOW TRUE
  PATCH_COMMAND
    ${CMAKE_COMMAND}
      -D "SQLiteCpp_SOURCE_DIR=<SOURCE_DIR>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_sqlitecpp_bind_text_length.cmake"
  UPDATE_DISCONNECTED TRUE
)
set(SQLITECPP_RUN_CPPCHECK   OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPLINT    OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SQLiteCpp)

find_package(Threads REQUIRED)

set(CSVZALL_RUNTIME_DEPENDENCY_SCRIPT
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_runtime_dependencies.cmake")

function(csvzall_copy_runtime_dependencies target_name)
  if(WIN32)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        -D "exe=$<TARGET_FILE:${target_name}>"
        -D "dest=$<TARGET_FILE_DIR:${target_name}>"
        -D "dirs=${CSVZALL_RUNTIME_DEPENDENCY_DIRS}"
        -P "${CSVZALL_RUNTIME_DEPENDENCY_SCRIPT}"
      VERBATIM)
  endif()
endfunction()

# PostgreSQL support via libpqxx (modern C++ wrapper around libpq).
# Strategy:
# 1) Respect CSVZALL_ENABLE_POSTGRESQL
# 2) Require PostgreSQL client libs (`libpq`)
# 3) Try system libpqxx package
# 4) If missing and enabled, fetch libpqxx from GitHub
set(CSVZALL_HAVE_POSTGRESQL OFF)
set(CSVZALL_LIBPQXX_TARGET "")

if(CSVZALL_ENABLE_POSTGRESQL)
  if(WIN32 AND NOT DEFINED PostgreSQL_ROOT)
    set(_csvzall_postgresql_roots
      "C:/Program Files/PostgreSQL/18"
      "C:/Program Files/PostgreSQL/17"
      "C:/Program Files/PostgreSQL/16"
      "C:/Program Files/PostgreSQL/15")

    foreach(_candidate_root IN LISTS _csvzall_postgresql_roots)
      if(EXISTS "${_candidate_root}/include/libpq-fe.h" AND EXISTS "${_candidate_root}/lib")
        set(PostgreSQL_ROOT "${_candidate_root}" CACHE PATH "PostgreSQL installation root" FORCE)
        message(STATUS "Using PostgreSQL_ROOT hint: ${PostgreSQL_ROOT}")
        break()
      endif()
    endforeach()
  endif()

  find_package(PostgreSQL QUIET)
  if(NOT PostgreSQL_FOUND)
    if(CSVZALL_REQUIRE_POSTGRESQL)
      message(FATAL_ERROR
        "PostgreSQL client libraries not found but CSVZALL_REQUIRE_POSTGRESQL=ON. "
        "Set -DPostgreSQL_ROOT=<path> (e.g. C:/Program Files/PostgreSQL/18).")
    else()
      message(STATUS "PostgreSQL client libraries not found; postgres subcommand will not be built (set -DPostgreSQL_ROOT=<path>, e.g. C:/Program Files/PostgreSQL/18)")
    endif()
  else()
    find_package(libpqxx CONFIG QUIET)

    if(NOT libpqxx_FOUND AND CSVZALL_FETCH_LIBPQXX)
      message(STATUS "libpqxx not found on system; using FetchContent cache/fetch")
      FetchContent_Declare(
        libpqxx
        GIT_REPOSITORY https://github.com/jtv/libpqxx.git
        GIT_TAG 7.10.1
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
      )
      set(SKIP_BUILD_TEST ON CACHE BOOL "Disable libpqxx tests" FORCE)
      FetchContent_MakeAvailable(libpqxx)
    endif()

    if(TARGET libpqxx::pqxx)
      set(CSVZALL_LIBPQXX_TARGET libpqxx::pqxx)
    elseif(TARGET pqxx::pqxx)
      set(CSVZALL_LIBPQXX_TARGET pqxx::pqxx)
    elseif(TARGET pqxx)
      set(CSVZALL_LIBPQXX_TARGET pqxx)
    endif()

    if(CSVZALL_LIBPQXX_TARGET STREQUAL "")
      if(CSVZALL_REQUIRE_POSTGRESQL)
        message(FATAL_ERROR
          "libpqxx target not found after package lookup/fetch while CSVZALL_REQUIRE_POSTGRESQL=ON.")
      else()
        message(STATUS "libpqxx target not found after package lookup/fetch; postgres subcommand will not be built")
      endif()
    else()
      message(STATUS "PostgreSQL support enabled via target: ${CSVZALL_LIBPQXX_TARGET}")
      set(CSVZALL_HAVE_POSTGRESQL ON)
    endif()
  endif()
else()
  if(CSVZALL_REQUIRE_POSTGRESQL)
    message(FATAL_ERROR "CSVZALL_REQUIRE_POSTGRESQL=ON requires CSVZALL_ENABLE_POSTGRESQL=ON")
  endif()
  message(STATUS "PostgreSQL support disabled (CSVZALL_ENABLE_POSTGRESQL=OFF)")
endif()

set(CSVZALL_RUNTIME_DEPENDENCY_DIRS "")
if(CSVZALL_HAVE_POSTGRESQL AND WIN32)
  if(PostgreSQL_ROOT AND EXISTS "${PostgreSQL_ROOT}/bin")
    list(APPEND CSVZALL_RUNTIME_DEPENDENCY_DIRS "${PostgreSQL_ROOT}/bin")
  endif()
endif()

set(CSVZALL_APP_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/transform_pipeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/column_lookup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/row_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/sqlite/sqlite_db.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/sqlite/csv_loader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/CsvInputCommand.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/CsvTransformCommand.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/derive.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/filter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/sql_query.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/sql_export.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/summarize.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/timeseries.cpp)

if(CSVZALL_HAVE_POSTGRESQL)
  list(APPEND CSVZALL_APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/postgres/postgres_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/postgres/schema_inference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/postgres/row_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/postgres_export.cpp)
endif()

function(csvzall_configure_app target_name)
  set(LINK_LIBS
    ${CSVZALL_ARGPARSE_TARGET}
    ${CSVZALL_INDICATORS_TARGET}
    csv
    SQLiteCpp
    Threads::Threads)

  if(CSVZALL_HAVE_POSTGRESQL)
    list(APPEND LINK_LIBS ${CSVZALL_LIBPQXX_TARGET})
  endif()

  target_link_libraries(${target_name} PRIVATE ${LINK_LIBS})
  csvzall_copy_runtime_dependencies(${target_name})

  set_property(TARGET ${target_name} PROPERTY CXX_STANDARD ${CSVZALL_CXX_STANDARD})
  set_property(TARGET ${target_name} PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET ${target_name} PROPERTY CXX_EXTENSIONS OFF)

  if(CSVZALL_HAVE_POSTGRESQL)
    target_compile_definitions(${target_name} PRIVATE CSVZALL_HAVE_POSTGRESQL=1)
  endif()

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive- /bigobj)
    if(CSVZALL_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    if(CSVZALL_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_executable(csvzall ${CSVZALL_APP_SOURCES})
csvzall_configure_app(csvzall)

add_subdirectory(programs)

install(TARGETS csvzall
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

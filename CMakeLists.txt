cmake_minimum_required(VERSION 3.25)

project(csvzall VERSION 0.1.0 LANGUAGES CXX)

set(CSVZALL_CXX_STANDARD 23 CACHE STRING "C++ standard used by csvzall and in-tree dependencies")

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
option(CSVZALL_ENABLE_KEYCHAIN "Enable OS keychain credential storage when available" ON)
option(CSVZALL_FETCH_KEYCHAIN "Fetch hrantzsch/keychain when not found on system" ON)
option(CSVZALL_FETCH_ZLIB "Fetch zlib when not found on system" ON)
option(CSVZALL_FETCH_SIMDJSON "Fetch simdjson when not found on system" ON)
option(CSVZALL_FETCH_NLOHMANN_JSON "Fetch nlohmann/json when not found on system" ON)
option(CSVZALL_ENABLE_SVGPLOT "Enable SVG chart output through a local svgplot checkout" ON)
set(CSVZALL_PREFER_SYSTEM_ZLIB_DEFAULT ON)
if(MSVC)
  set(CSVZALL_PREFER_SYSTEM_ZLIB_DEFAULT OFF)
endif()
option(CSVZALL_PREFER_SYSTEM_ZLIB
  "Prefer a system zlib over FetchContent. Defaults off on MSVC to avoid CRT mismatches with prebuilt zlib libraries."
  ${CSVZALL_PREFER_SYSTEM_ZLIB_DEFAULT})

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
# 3) In-repo submodule at external/csv-parser
# 4) FetchContent fallback for source archives or checkouts without submodules
set(CSV_PARSER_ROOT "" CACHE PATH "Path to csv-parser checkout (must contain include/ and single_include/)")
set(CSVZALL_CSV_PARSER_GIT_TAG "f5d24ab4a38ba0b74cbc7057f21543014d2ec472"
    CACHE STRING "csv-parser revision used by FetchContent fallback")

set(CSVZALL_IN_REPO_CSV_PARSER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/csv-parser")
set(CSVZALL_SIBLING_CSV_PARSER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../csv-parser")
get_filename_component(CSVZALL_IN_REPO_CSV_PARSER_ROOT "${CSVZALL_IN_REPO_CSV_PARSER_ROOT}" ABSOLUTE)
get_filename_component(CSVZALL_SIBLING_CSV_PARSER_ROOT "${CSVZALL_SIBLING_CSV_PARSER_ROOT}" ABSOLUTE)

set(CSV_BUILD_PROGRAMS OFF CACHE BOOL "Disable csv-parser program targets" FORCE)
set(BUILD_PYTHON OFF CACHE BOOL "Disable csv-parser Python bindings" FORCE)
set(CSV_BUILD_SINGLE_INCLUDE_TEST OFF CACHE BOOL "Disable csv-parser single include test" FORCE)
set(CSV_CXX_STANDARD "${CSVZALL_CXX_STANDARD}" CACHE STRING "C++ standard for csv-parser" FORCE)

function(csvzall_validate_csv_parser source_dir)
  set(_csvzall_csv_row "${source_dir}/include/internal/csv_row.hpp")
  set(_csvzall_data_frame "${source_dir}/include/internal/data_frame/data_frame.hpp")
  if(NOT EXISTS "${_csvzall_csv_row}" OR NOT EXISTS "${_csvzall_data_frame}")
    message(FATAL_ERROR
      "csv-parser checkout is too old for csvzall: ${source_dir}\n"
      "Expected split DataFrame headers and native CSVRow::byte_offset().")
  endif()

  file(READ "${_csvzall_csv_row}" _csvzall_csv_row_content)
  file(READ "${_csvzall_data_frame}" _csvzall_data_frame_content)
  if(NOT _csvzall_csv_row_content MATCHES "byte_offset\\(" OR
     NOT _csvzall_data_frame_content MATCHES "insert_column\\(" OR
     NOT _csvzall_data_frame_content MATCHES "insert_row\\(")
    message(FATAL_ERROR
      "csv-parser checkout is too old for csvzall: ${source_dir}\n"
      "Update csv-parser to a revision with CSVRow::byte_offset(), "
      "DataFrame::insert_row(), and DataFrame::insert_column().")
  endif()
endfunction()

set(CSVZALL_LOCAL_CSV_PARSER_ROOT "")
set(CSVZALL_CSV_PARSER_SOURCE_DIR "")
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
  set(CSVZALL_CSV_PARSER_SOURCE_DIR "${CSVZALL_LOCAL_CSV_PARSER_ROOT}")
  csvzall_validate_csv_parser("${CSVZALL_CSV_PARSER_SOURCE_DIR}")
  add_subdirectory("${CSVZALL_CSV_PARSER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/csv-parser-local-build" EXCLUDE_FROM_ALL)
elseif(EXISTS "${CSVZALL_IN_REPO_CSV_PARSER_ROOT}/include" AND EXISTS "${CSVZALL_IN_REPO_CSV_PARSER_ROOT}/single_include")
  message(STATUS "Using csv-parser from: ${CSVZALL_IN_REPO_CSV_PARSER_ROOT}")
  set(CSVZALL_CSV_PARSER_SOURCE_DIR "${CSVZALL_IN_REPO_CSV_PARSER_ROOT}")
  csvzall_validate_csv_parser("${CSVZALL_CSV_PARSER_SOURCE_DIR}")
  add_subdirectory("${CSVZALL_CSV_PARSER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/csv-parser-local-build" EXCLUDE_FROM_ALL)
elseif(CSVZALL_FETCH_CSV_PARSER)
  message(STATUS "Fetching csv-parser with FetchContent")
  FetchContent_Declare(
    csv_parser
    GIT_REPOSITORY https://github.com/vincentlaucsb/csv-parser.git
    GIT_TAG ${CSVZALL_CSV_PARSER_GIT_TAG}
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(csv_parser)
  FetchContent_GetProperties(csv_parser SOURCE_DIR CSVZALL_CSV_PARSER_SOURCE_DIR)
  if(CSVZALL_CSV_PARSER_SOURCE_DIR)
    csvzall_validate_csv_parser("${CSVZALL_CSV_PARSER_SOURCE_DIR}")
  endif()
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
  PATCH_COMMAND
    ${CMAKE_COMMAND}
      -D "INDICATORS_SOURCE_DIR=<SOURCE_DIR>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_indicators_cstdint.cmake"
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(indicators)
FetchContent_GetProperties(indicators SOURCE_DIR CSVZALL_INDICATORS_SOURCE_DIR)
if(CSVZALL_INDICATORS_SOURCE_DIR)
  execute_process(
    COMMAND ${CMAKE_COMMAND}
      -D "INDICATORS_SOURCE_DIR=${CSVZALL_INDICATORS_SOURCE_DIR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_indicators_cstdint.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
endif()

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

find_package(simdjson CONFIG QUIET)
if(NOT TARGET simdjson::simdjson AND NOT TARGET simdjson AND CSVZALL_FETCH_SIMDJSON)
  message(STATUS "simdjson not found; using FetchContent cache/fetch")
  FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v3.13.0
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
  )
  set(SIMDJSON_BUILD_STATIC ON CACHE BOOL "Build static simdjson" FORCE)
  set(SIMDJSON_BUILD_TESTS OFF CACHE BOOL "Disable simdjson tests" FORCE)
  set(SIMDJSON_BUILD_BENCHMARKS OFF CACHE BOOL "Disable simdjson benchmarks" FORCE)
  set(SIMDJSON_BUILD_EXAMPLES OFF CACHE BOOL "Disable simdjson examples" FORCE)
  FetchContent_MakeAvailable(simdjson)
endif()

set(CSVZALL_SIMDJSON_TARGET "")
if(TARGET simdjson::simdjson)
  set(CSVZALL_SIMDJSON_TARGET simdjson::simdjson)
elseif(TARGET simdjson)
  set(CSVZALL_SIMDJSON_TARGET simdjson)
endif()
if(CSVZALL_SIMDJSON_TARGET STREQUAL "")
  message(FATAL_ERROR "simdjson is required for JSON extraction. Install simdjson or enable CSVZALL_FETCH_SIMDJSON.")
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json AND CSVZALL_FETCH_NLOHMANN_JSON)
  message(STATUS "nlohmann/json not found; using FetchContent cache/fetch")
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    UPDATE_DISCONNECTED TRUE
  )
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  set(JSON_Install OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(nlohmann_json)
endif()

set(CSVZALL_NLOHMANN_JSON_TARGET "")
if(TARGET nlohmann_json::nlohmann_json)
  set(CSVZALL_NLOHMANN_JSON_TARGET nlohmann_json::nlohmann_json)
endif()
if(CSVZALL_NLOHMANN_JSON_TARGET STREQUAL "")
  message(FATAL_ERROR "nlohmann/json is required. Install nlohmann_json or enable CSVZALL_FETCH_NLOHMANN_JSON.")
endif()

set(SVGPLOT_ROOT "" CACHE PATH "Path to svgplot checkout (must contain include/svgplot/svgplot.hpp)")
set(SVG_ROOT "" CACHE PATH "Path to vincentlaucsb/svg checkout (must contain src/svg.hpp)")
set(CSVZALL_HAVE_SVGPLOT OFF)
set(CSVZALL_SVGPLOT_TARGET "")
set(CSVZALL_SIBLING_SVGPLOT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../svgplot")
set(CSVZALL_SIBLING_SVG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../svg")
get_filename_component(CSVZALL_SIBLING_SVGPLOT_ROOT "${CSVZALL_SIBLING_SVGPLOT_ROOT}" ABSOLUTE)
get_filename_component(CSVZALL_SIBLING_SVG_ROOT "${CSVZALL_SIBLING_SVG_ROOT}" ABSOLUTE)

if(CSVZALL_ENABLE_SVGPLOT)
  set(CSVZALL_LOCAL_SVGPLOT_ROOT "")
  if(NOT SVGPLOT_ROOT STREQUAL "")
    get_filename_component(CSVZALL_LOCAL_SVGPLOT_ROOT "${SVGPLOT_ROOT}" ABSOLUTE)
  elseif(EXISTS "${CSVZALL_SIBLING_SVGPLOT_ROOT}/include/svgplot/svgplot.hpp")
    set(CSVZALL_LOCAL_SVGPLOT_ROOT "${CSVZALL_SIBLING_SVGPLOT_ROOT}")
  endif()

  if(NOT CSVZALL_LOCAL_SVGPLOT_ROOT STREQUAL "" AND
     EXISTS "${CSVZALL_LOCAL_SVGPLOT_ROOT}/include/svgplot/svgplot.hpp")
    set(CSVZALL_LOCAL_SVG_ROOT "")
    if(NOT SVG_ROOT STREQUAL "")
      get_filename_component(CSVZALL_LOCAL_SVG_ROOT "${SVG_ROOT}" ABSOLUTE)
    elseif(EXISTS "${CSVZALL_SIBLING_SVG_ROOT}/src/svg.hpp")
      set(CSVZALL_LOCAL_SVG_ROOT "${CSVZALL_SIBLING_SVG_ROOT}")
    else()
      set(_csvzall_svg_candidates
        "${CSVZALL_LOCAL_SVGPLOT_ROOT}/out/build/x64-Release/_deps/svg-src"
        "${CSVZALL_LOCAL_SVGPLOT_ROOT}/out/build/x64-Debug/_deps/svg-src"
        "${CSVZALL_LOCAL_SVGPLOT_ROOT}/out/build/_deps/svg-src")
      foreach(_candidate IN LISTS _csvzall_svg_candidates)
        if(EXISTS "${_candidate}/src/svg.hpp")
          set(CSVZALL_LOCAL_SVG_ROOT "${_candidate}")
          break()
        endif()
      endforeach()
    endif()

    if(CSVZALL_LOCAL_SVG_ROOT STREQUAL "" OR NOT EXISTS "${CSVZALL_LOCAL_SVG_ROOT}/src/svg.hpp")
      message(STATUS "svgplot found, but vincentlaucsb/svg was not found; heatmap command disabled. Set -DSVG_ROOT=<path-to-svg>.")
    else()
      message(STATUS "Using svgplot from: ${CSVZALL_LOCAL_SVGPLOT_ROOT}")
      message(STATUS "Using svg dependency from: ${CSVZALL_LOCAL_SVG_ROOT}")
      add_library(csvzall_svgplot INTERFACE)
      target_include_directories(csvzall_svgplot INTERFACE
        "${CSVZALL_LOCAL_SVGPLOT_ROOT}/include"
        "${CSVZALL_LOCAL_SVG_ROOT}/src")
      target_compile_features(csvzall_svgplot INTERFACE cxx_std_23)
      set(CSVZALL_SVGPLOT_TARGET csvzall_svgplot)
      set(CSVZALL_HAVE_SVGPLOT ON)
    endif()
  else()
    message(STATUS "svgplot checkout not found; heatmap command disabled. Set -DSVGPLOT_ROOT=<path-to-svgplot>.")
  endif()
else()
  message(STATUS "SVG chart output disabled (CSVZALL_ENABLE_SVGPLOT=OFF)")
endif()

find_package(Threads REQUIRED)

# gzip input support. Prefer a system zlib on Unix-like platforms, but default
# to an in-tree zlib build on MSVC so it uses the same CRT settings as csvzall.
if(CSVZALL_PREFER_SYSTEM_ZLIB AND WIN32 AND NOT DEFINED ZLIB_ROOT)
  set(_csvzall_zlib_roots
    "C:/Program Files/PostgreSQL/18"
    "C:/Program Files/PostgreSQL/17"
    "C:/Program Files/PostgreSQL/16"
    "C:/Program Files/PostgreSQL/15")

  foreach(_candidate_root IN LISTS _csvzall_zlib_roots)
    if(EXISTS "${_candidate_root}/include/zlib.h" AND EXISTS "${_candidate_root}/lib")
      set(ZLIB_ROOT "${_candidate_root}" CACHE PATH "zlib installation root" FORCE)
      message(STATUS "Using ZLIB_ROOT hint: ${ZLIB_ROOT}")
      break()
    endif()
  endforeach()
endif()

if(CSVZALL_PREFER_SYSTEM_ZLIB)
  find_package(ZLIB QUIET)
endif()
if(NOT ZLIB_FOUND AND CSVZALL_FETCH_ZLIB)
  message(STATUS "zlib not found on system; using FetchContent cache/fetch")
  FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
  )
  set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Disable zlib examples" FORCE)
  FetchContent_MakeAvailable(zlib)
endif()

set(CSVZALL_ZLIB_TARGET "")
if(TARGET ZLIB::ZLIB)
  set(CSVZALL_ZLIB_TARGET ZLIB::ZLIB)
elseif(TARGET zlibstatic)
  set(CSVZALL_ZLIB_TARGET zlibstatic)
elseif(TARGET zlib)
  set(CSVZALL_ZLIB_TARGET zlib)
endif()

if(CSVZALL_ZLIB_TARGET STREQUAL "")
  message(FATAL_ERROR "zlib is required for gzip CSV input support. Install zlib or enable CSVZALL_FETCH_ZLIB.")
endif()

# Optional OS credential storage. This is deliberately soft: if the library or
# its platform requirements are unavailable, csvzall still builds and falls back
# to prompting / explicit password sources.
set(CSVZALL_HAVE_KEYCHAIN OFF)
set(CSVZALL_KEYCHAIN_TARGET "")

if(CSVZALL_ENABLE_KEYCHAIN)
  set(_csvzall_keychain_platform_ok ON)
  if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(CSVZALL_LIBSECRET QUIET libsecret-1)
    endif()
    if(NOT CSVZALL_LIBSECRET_FOUND)
      set(_csvzall_keychain_platform_ok OFF)
      message(STATUS "libsecret-1 not found; keychain credential storage disabled")
    endif()
  endif()

  if(_csvzall_keychain_platform_ok)
    find_package(keychain QUIET)

    if(NOT keychain_FOUND AND CSVZALL_FETCH_KEYCHAIN)
      FetchContent_Declare(
        keychain
        GIT_REPOSITORY https://github.com/hrantzsch/keychain.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
      )
      set(BUILD_TESTS OFF CACHE BOOL "Disable keychain tests" FORCE)
      FetchContent_MakeAvailable(keychain)
    endif()

    if(TARGET keychain)
      set(CSVZALL_KEYCHAIN_TARGET keychain)
    elseif(TARGET keychain::keychain)
      set(CSVZALL_KEYCHAIN_TARGET keychain::keychain)
    endif()

    if(NOT CSVZALL_KEYCHAIN_TARGET STREQUAL "")
      set(CSVZALL_HAVE_KEYCHAIN ON)
      message(STATUS "Keychain credential storage enabled via target: ${CSVZALL_KEYCHAIN_TARGET}")
    else()
      message(STATUS "keychain target not found; credential storage disabled")
    endif()
  endif()
else()
  message(STATUS "Keychain credential storage disabled (CSVZALL_ENABLE_KEYCHAIN=OFF)")
endif()

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
  elseif(APPLE AND NOT DEFINED PostgreSQL_ROOT)
    set(_csvzall_postgresql_roots
      "/opt/homebrew/opt/libpq"
      "/usr/local/opt/libpq"
      "/opt/homebrew/opt/postgresql"
      "/usr/local/opt/postgresql")

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
        "Set -DPostgreSQL_ROOT=<path> (e.g. C:/Program Files/PostgreSQL/18 or /opt/homebrew/opt/libpq).")
    else()
      message(STATUS "PostgreSQL client libraries not found; postgres subcommand will not be built (set -DPostgreSQL_ROOT=<path>, e.g. C:/Program Files/PostgreSQL/18 or /opt/homebrew/opt/libpq)")
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
if(WIN32 AND CSVZALL_ZLIB_TARGET STREQUAL "ZLIB::ZLIB" AND ZLIB_ROOT AND EXISTS "${ZLIB_ROOT}/bin")
  list(APPEND CSVZALL_RUNTIME_DEPENDENCY_DIRS "${ZLIB_ROOT}/bin")
endif()
if(CSVZALL_HAVE_POSTGRESQL AND WIN32)
  if(PostgreSQL_ROOT AND EXISTS "${PostgreSQL_ROOT}/bin")
    list(APPEND CSVZALL_RUNTIME_DEPENDENCY_DIRS "${PostgreSQL_ROOT}/bin")
  endif()
endif()

set(CSVZALL_VIEWER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/viewer")
set(CSVZALL_VIEWER_ASSET_CPP "${CSVZALL_VIEWER_GENERATED_DIR}/viewer_assets.cpp")
set(CSVZALL_VIEWER_ASSET_HPP "${CSVZALL_VIEWER_GENERATED_DIR}/viewer_assets.hpp")
set(CSVZALL_VIEWER_ASSET_INPUTS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/viewer/index.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/viewer/viewer.css"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/viewer/viewer.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ag-grid/ag-grid-community.min.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ag-grid/ag-grid.css"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ag-grid/ag-theme-alpine.css"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/constants.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/ContextMenu.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/index.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/MenuController.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/positioning.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/render.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/styles.css"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/theme-utils.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/theme.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/ThemeStore.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/types.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist/utils.js")

add_custom_command(
  OUTPUT "${CSVZALL_VIEWER_ASSET_CPP}" "${CSVZALL_VIEWER_ASSET_HPP}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CSVZALL_VIEWER_GENERATED_DIR}"
  COMMAND ${CMAKE_COMMAND}
    -D "OUTPUT_CPP=${CSVZALL_VIEWER_ASSET_CPP}"
    -D "OUTPUT_HPP=${CSVZALL_VIEWER_ASSET_HPP}"
    -D "VIEWER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/viewer"
    -D "AG_GRID_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/vendor/ag-grid"
    -D "POPRIGHT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/vendor/popright/dist"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_viewer_assets.cmake"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_viewer_assets.cmake"
    ${CSVZALL_VIEWER_ASSET_INPUTS}
  VERBATIM)

set_source_files_properties(
  "${CSVZALL_VIEWER_ASSET_CPP}"
  "${CSVZALL_VIEWER_ASSET_HPP}"
  PROPERTIES GENERATED TRUE)

add_custom_target(csvzall_viewer_assets
  DEPENDS "${CSVZALL_VIEWER_ASSET_CPP}" "${CSVZALL_VIEWER_ASSET_HPP}")

set(CSVZALL_APP_SOURCES
  ${CSVZALL_VIEWER_ASSET_CPP}
  ${CSVZALL_VIEWER_ASSET_HPP}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/credentials.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/util.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/transform_pipeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/chart_spec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/column_lookup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/gzip_stream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/json_path.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/markdown_calendar.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/markdown_table.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/row_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/sqlite/sqlite_db.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/sqlite/csv_loader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/append.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/CsvInputCommand.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/CsvTransformCommand.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/derive.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/filter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/json_extract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/max.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/sql_query.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/sql_export.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/summarize.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/timeseries.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/assets.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/charts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/json.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/model.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/server.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/support.cpp)

if(CSVZALL_HAVE_SVGPLOT)
  list(APPEND CSVZALL_APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/charts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/heatmap.cpp)
endif()

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
    ${CSVZALL_SIMDJSON_TARGET}
    ${CSVZALL_NLOHMANN_JSON_TARGET}
    csv
    SQLiteCpp
    Threads::Threads
    ${CSVZALL_ZLIB_TARGET})

  if(CSVZALL_HAVE_POSTGRESQL)
    list(APPEND LINK_LIBS ${CSVZALL_LIBPQXX_TARGET})
  endif()
  if(CSVZALL_HAVE_KEYCHAIN)
    list(APPEND LINK_LIBS ${CSVZALL_KEYCHAIN_TARGET})
  endif()
  if(CSVZALL_HAVE_SVGPLOT)
    list(APPEND LINK_LIBS ${CSVZALL_SVGPLOT_TARGET})
  endif()

  target_link_libraries(${target_name} PRIVATE ${LINK_LIBS})
  csvzall_copy_runtime_dependencies(${target_name})
  add_dependencies(${target_name} csvzall_viewer_assets)
  target_include_directories(${target_name} PRIVATE "${CSVZALL_VIEWER_GENERATED_DIR}")

  set_property(TARGET ${target_name} PROPERTY CXX_STANDARD ${CSVZALL_CXX_STANDARD})
  set_property(TARGET ${target_name} PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET ${target_name} PROPERTY CXX_EXTENSIONS OFF)

  target_compile_definitions(${target_name} PRIVATE CSVZALL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  if(CSVZALL_HAVE_POSTGRESQL)
    target_compile_definitions(${target_name} PRIVATE CSVZALL_HAVE_POSTGRESQL=1)
  endif()
  if(CSVZALL_HAVE_KEYCHAIN)
    target_compile_definitions(${target_name} PRIVATE CSVZALL_HAVE_KEYCHAIN=1)
  endif()
  if(CSVZALL_HAVE_SVGPLOT)
    target_compile_definitions(${target_name} PRIVATE CSVZALL_HAVE_SVGPLOT=1)
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
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT csvzall)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

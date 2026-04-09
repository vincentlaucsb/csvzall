cmake_minimum_required(VERSION 3.20)

project(csvzall VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
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

# csv-parser location strategy:
# 1) User-provided -DCSV_PARSER_ROOT=...
# 2) In-repo submodule at external/csv-parser
# 3) Sibling checkout at ../csv-parser
set(CSV_PARSER_ROOT "" CACHE PATH "Path to csv-parser checkout (must contain include/ and single_include/)")

if(CSV_PARSER_ROOT STREQUAL "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/csv-parser")
    set(CSV_PARSER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/csv-parser")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../csv-parser")
    set(CSV_PARSER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../csv-parser")
  endif()
endif()

if(NOT EXISTS "${CSV_PARSER_ROOT}/include" OR NOT EXISTS "${CSV_PARSER_ROOT}/single_include")
  message(FATAL_ERROR
    "csv-parser not found or invalid.\n"
    "Set -DCSV_PARSER_ROOT=<path-to-csv-parser> or use one of:\n"
    "  ${CMAKE_CURRENT_SOURCE_DIR}/external/csv-parser\n"
    "  ${CMAKE_CURRENT_SOURCE_DIR}/../csv-parser")
endif()

message(STATUS "Using csv-parser from: ${CSV_PARSER_ROOT}")

set(CSV_BUILD_PROGRAMS OFF CACHE BOOL "Disable csv-parser program targets" FORCE)
set(BUILD_PYTHON OFF CACHE BOOL "Disable csv-parser Python bindings" FORCE)
set(CSV_BUILD_SINGLE_INCLUDE_TEST OFF CACHE BOOL "Disable csv-parser single include test" FORCE)

add_subdirectory("${CSV_PARSER_ROOT}" "${CMAKE_BINARY_DIR}/csv-parser" EXCLUDE_FROM_ALL)

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

# ExprTk (header-only expression engine)
FetchContent_Declare(
  exprtk
  GIT_REPOSITORY https://github.com/ArashPartow/exprtk.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(exprtk)
if(NOT TARGET exprtk)
  add_library(exprtk INTERFACE)
  target_include_directories(exprtk SYSTEM INTERFACE
    "${exprtk_SOURCE_DIR}")
endif()

find_package(Threads REQUIRED)

add_executable(csvzall
  src/main.cpp
  src/transform_pipeline.cpp
  src/pipeline/common/column_lookup.cpp
  src/pipeline/common/row_utils.cpp
  src/pipeline/expression/expr_engine.cpp
  src/pipeline/commands/derive.cpp
  src/pipeline/commands/filter.cpp
  src/pipeline/commands/summarize.cpp
  src/pipeline/commands/timeseries.cpp)

target_link_libraries(csvzall
  PRIVATE
    ${CSVZALL_ARGPARSE_TARGET}
    csv
    exprtk
    Threads::Threads)

target_compile_features(csvzall PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(csvzall PRIVATE /W4 /permissive- /bigobj)
  if(CSVZALL_WARNINGS_AS_ERRORS)
    target_compile_options(csvzall PRIVATE /WX)
  endif()
else()
  target_compile_options(csvzall PRIVATE -Wall -Wextra -Wpedantic)
  if(CSVZALL_WARNINGS_AS_ERRORS)
    target_compile_options(csvzall PRIVATE -Werror)
  endif()
endif()

install(TARGETS csvzall
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

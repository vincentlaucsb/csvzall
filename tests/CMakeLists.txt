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
  test_summarize.cpp
  test_timeseries.cpp
  ../src/transform_pipeline.cpp
  ../src/pipeline/common/column_lookup.cpp
  ../src/pipeline/common/row_utils.cpp
  ../src/pipeline/expression/expr_engine.cpp
  ../src/pipeline/commands/derive.cpp
  ../src/pipeline/commands/filter.cpp
  ../src/pipeline/commands/summarize.cpp
  ../src/pipeline/commands/timeseries.cpp)

target_link_libraries(csvzall_tests
  PRIVATE
    Catch2::Catch2WithMain
    ${CSVZALL_ARGPARSE_TARGET}
    csv
    exprtk
    Threads::Threads)

target_include_directories(csvzall_tests
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_features(csvzall_tests PRIVATE cxx_std_20)

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

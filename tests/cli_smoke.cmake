if(NOT DEFINED CSVZALL_EXE)
  message(FATAL_ERROR "CSVZALL_EXE is required")
endif()
if(NOT DEFINED CLI_TEST_DIR)
  message(FATAL_ERROR "CLI_TEST_DIR is required")
endif()

file(REMOVE_RECURSE "${CLI_TEST_DIR}")
file(MAKE_DIRECTORY "${CLI_TEST_DIR}")

set(csv_path "${CLI_TEST_DIR}/data.csv")
set(psv_path "${CLI_TEST_DIR}/data.psv")
set(db_path "${CLI_TEST_DIR}/loaded.sqlite")

file(WRITE "${csv_path}" "name,value,date,series\nalice,10,2024-01-01,A\nbob,20,2024-01-02,A\ncara,30,2024-01-02,B\n")
file(WRITE "${psv_path}" "name|value\nalice|10\nbob|20\n")

function(run_csvzall_case case_name expected_rc expected_stdout expected_stderr)
  execute_process(
    COMMAND "${CSVZALL_EXE}" ${ARGN}
    RESULT_VARIABLE actual_rc
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr)

  if(NOT actual_rc EQUAL expected_rc)
    message(FATAL_ERROR
      "${case_name}: expected exit ${expected_rc}, got ${actual_rc}\n"
      "stdout:\n${actual_stdout}\n"
      "stderr:\n${actual_stderr}")
  endif()
  if(NOT "${expected_stdout}" STREQUAL "" AND NOT actual_stdout MATCHES "${expected_stdout}")
    message(FATAL_ERROR
      "${case_name}: stdout did not match /${expected_stdout}/\n"
      "stdout:\n${actual_stdout}\n"
      "stderr:\n${actual_stderr}")
  endif()
  if(NOT "${expected_stderr}" STREQUAL "" AND NOT actual_stderr MATCHES "${expected_stderr}")
    message(FATAL_ERROR
      "${case_name}: stderr did not match /${expected_stderr}/\n"
      "stdout:\n${actual_stdout}\n"
      "stderr:\n${actual_stderr}")
  endif()
endfunction()

function(run_csvzall_stdin_case case_name expected_rc input_file expected_stdout expected_stderr)
  execute_process(
    COMMAND "${CSVZALL_EXE}" ${ARGN}
    INPUT_FILE "${input_file}"
    RESULT_VARIABLE actual_rc
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr)

  if(NOT actual_rc EQUAL expected_rc)
    message(FATAL_ERROR
      "${case_name}: expected exit ${expected_rc}, got ${actual_rc}\n"
      "stdout:\n${actual_stdout}\n"
      "stderr:\n${actual_stderr}")
  endif()
  if(NOT "${expected_stdout}" STREQUAL "" AND NOT actual_stdout MATCHES "${expected_stdout}")
    message(FATAL_ERROR
      "${case_name}: stdout did not match /${expected_stdout}/\n"
      "stdout:\n${actual_stdout}\n"
      "stderr:\n${actual_stderr}")
  endif()
  if(NOT "${expected_stderr}" STREQUAL "" AND NOT actual_stderr MATCHES "${expected_stderr}")
    message(FATAL_ERROR
      "${case_name}: stderr did not match /${expected_stderr}/\n"
      "stdout:\n${actual_stdout}\n"
      "stderr:\n${actual_stderr}")
  endif()
endfunction()

run_csvzall_case("help without subcommand" 0 "Usage: csvzall" "" --help)
run_csvzall_case("head with explicit pipe delimiter" 0 "bob" "" head "${psv_path}" --rows 2 --delimiter pipe --quiet)
run_csvzall_stdin_case("filter from stdin" 0 "${csv_path}" "bob,20" "" filter "value > 10" --quiet)
run_csvzall_case("filter missing file" 1 "" "Unable to open input file" filter "value > 10" "${CLI_TEST_DIR}/missing.csv" --quiet)
run_csvzall_case("derive from file" 0 "alice,10,20" "" derive "double_value = value * 2" "${csv_path}" --quiet)
run_csvzall_case("summarize from file" 0 "A,20" "" summarize --group-by series --max value "${csv_path}" --quiet)
run_csvzall_case("timeseries markdown output" 0 "\\| 2024-01-02 \\| 30 \\|" "" timeseries --x date --y value --reduce max --format markdown "${csv_path}" --quiet)
run_csvzall_case("sql query from csv" 0 "bob,20" "" sql query --csv "${csv_path}" --sql "SELECT name, value FROM data WHERE value = 20" --quiet)
run_csvzall_case("sql load creates database" 0 "" "" sql load "${csv_path}" --dest "${db_path}" --table cars --quiet)

if(NOT EXISTS "${db_path}")
  message(FATAL_ERROR "sql load did not create ${db_path}")
endif()

run_csvzall_case("sql query from loaded db" 0 "3" "" sql query --db "${db_path}" --sql "SELECT COUNT(*) AS rows FROM cars" --quiet)
run_csvzall_case("unknown command fails" 1 "" "Failed to parse" nope)

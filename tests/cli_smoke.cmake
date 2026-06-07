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
set(calendar_path "${CLI_TEST_DIR}/calendar.csv")
set(heatmap_path "${CLI_TEST_DIR}/heatmap.csv")
set(charts_config_dir "${CLI_TEST_DIR}/.csvzall")
set(charts_output_path "${CLI_TEST_DIR}/reports/heatmap.svg")
set(charts_markdown_path "${CLI_TEST_DIR}/reports/summary.md")
set(db_path "${CLI_TEST_DIR}/loaded.sqlite")
set(append_existing_path "${CLI_TEST_DIR}/append-existing.csv")
set(append_incoming_path "${CLI_TEST_DIR}/append-incoming.csv")

file(WRITE "${csv_path}" "name,value,date,series\nalice,10,2024-01-01,A\nbob,20,2024-01-02,A\ncara,30,2024-01-02,B\n")
file(WRITE "${psv_path}" "name|value\nalice|10\nbob|20\n")
file(WRITE "${calendar_path}" "date,content\n2024-01-01,Done\n2024-01-02,\n")
file(WRITE "${heatmap_path}" "date,count,content\n2024-01-01,1,Gym\n2024-01-01,2,Lift\n2024-01-02,1,Workout\n")
file(MAKE_DIRECTORY "${charts_config_dir}")
file(WRITE "${charts_config_dir}/charts.json" "{\"charts\":[{\"id\":\"cli-heatmap\",\"type\":\"heatmap\",\"input\":\"heatmap.csv\",\"output\":\"reports/heatmap.svg\",\"options\":{\"date\":\"date\",\"value\":\"count\",\"label\":\"content\",\"start\":\"2024-01-01\",\"end\":\"2024-01-07\",\"title\":\"CLI Gym\"}},{\"id\":\"cli-markdown\",\"type\":\"markdown-table\",\"input\":\"data.csv\",\"output\":\"reports/summary.md\",\"options\":{\"columns\":[\"name\",\"value\"]}}]}")
file(WRITE "${append_existing_path}" "id,name\n1,alice\n2,bob\n")
file(WRITE "${append_incoming_path}" "id,name\n2,bob duplicate\n3,cara\n")

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
run_csvzall_case("json extract help documents JSONPath subset" 0 "Supported JSONPath subset" "" json extract --help)
run_csvzall_case("json extract help includes mapping example" 0 "\"event_id\"" "" json extract --help)
run_csvzall_case("sql query help documents aggregation" 0 "COUNT\\(DISTINCT" "" sql query --help)
run_csvzall_case("top-level help groups intent" 0 "ETL/data: head, filter, derive, summarize, timeseries, sql, json, append, merge" "" --help)
run_csvzall_case("top-level help mentions view intent group" 0 "Inspect/view: view" "" --help)
run_csvzall_case("top-level help mentions sql regex functions" 0 "regexp_like\\(value, pattern\\)" "" --help)
run_csvzall_case("merge help documents existing wins" 0 "Existing rows win" "" merge --help)
run_csvzall_case("view help documents token-gated local server" 0 "API requests require a random session token" "" view --help)
run_csvzall_case("view help documents startup-json" 0 "--startup-json" "" view --help)
run_csvzall_case("view help documents edit mode" 0 "--edit" "" view --help)
run_csvzall_case("calendar help documents fixed schema" 0 "date,content" "" calendar --help)
run_csvzall_case("calendar help documents duplicate behavior" 0 "Duplicate dates are rejected" "" calendar --help)
if(CSVZALL_HAVE_SVGPLOT)
  run_csvzall_case("heatmap help documents aggregation" 0 "Duplicate dates are aggregated" "" heatmap --help)
  run_csvzall_case("heatmap help documents orientation" 0 "months-vertical" "" heatmap --help)
  run_csvzall_case("charts help documents config command" 0 "csvzall charts run" "" charts --help)
  run_csvzall_case("charts help points to schema reference" 0 "csvzall charts schema" "" charts --help)
  run_csvzall_case("charts schema documents config shape" 0 "Config file shape" "" charts schema)
  run_csvzall_case("charts schema documents options by type" 0 "Options by type" "" charts schema)
  run_csvzall_case("charts schema documents type drill-down" 0 "Drill-down by type" "" charts schema)
  run_csvzall_case("charts schema documents lookback values" 0 "30d.*1y" "" charts schema)
  run_csvzall_case("charts schema documents heatmap orientation" 0 "orientation.*months-vertical" "" charts schema)
  run_csvzall_case("charts schema documents bar presentation" 0 "presentation.*grouped" "" charts schema)
  run_csvzall_case("charts schema documents color schemes" 0 "colorScheme.*diverging" "" charts schema)
  run_csvzall_case("charts help documents markdown tables" 0 "markdown-table" "" charts --help)
endif()
run_csvzall_case("head with explicit pipe delimiter" 0 "bob" "" head "${psv_path}" --rows 2 --delimiter pipe --quiet)
run_csvzall_stdin_case("filter from stdin" 0 "${csv_path}" "bob,20" "" filter "value > 10" --quiet)
run_csvzall_case("filter missing file" 1 "" "Unable to open input file" filter "value > 10" "${CLI_TEST_DIR}/missing.csv" --quiet)
run_csvzall_case("derive from file" 0 "alice,10,20" "" derive "double_value = value * 2" "${csv_path}" --quiet)
run_csvzall_case("summarize from file" 0 "A,20" "" summarize --group-by series --max value "${csv_path}" --quiet)
run_csvzall_case("timeseries markdown output" 0 "\\| 2024-01-02 \\| 30 \\|" "" timeseries --x date --y value --reduce max --format markdown "${csv_path}" --quiet)
run_csvzall_case("calendar markdown output" 0 "### 2024-1" "" calendar "${calendar_path}" --start 2024-01-01 --end 2024-01-02 --month-header "{year}-{month}" --quiet)
if(CSVZALL_HAVE_SVGPLOT)
  run_csvzall_case("heatmap svg output" 0 "<svg.*Gym Attendance.*2024-01-01: 3" "" heatmap "${heatmap_path}" --start 2024-01-01 --end 2024-01-07 --date date --value count --label content --title "Gym Attendance" --quiet)
  run_csvzall_case("heatmap output file" 0 "" "" heatmap "${heatmap_path}" --start 2024-01-01 --end 2024-01-07 --date date --value count --output "${CLI_TEST_DIR}/direct-heatmap.svg" --quiet)
  if(NOT EXISTS "${CLI_TEST_DIR}/direct-heatmap.svg")
    message(FATAL_ERROR "heatmap --output did not write direct-heatmap.svg")
  endif()
  run_csvzall_case("heatmap vertical orientation" 0 "<svg" "" heatmap "${heatmap_path}" --start 2024-01-01 --end 2024-01-07 --date date --value count --orientation months-vertical --quiet)
  run_csvzall_case("charts validate explicit config" 0 "" "" charts run cli-heatmap --config "${charts_config_dir}/charts.json" --validate --quiet)
  run_csvzall_case("charts run explicit config writes output" 0 "" "" charts run cli-heatmap --config "${charts_config_dir}/charts.json" --quiet)
  if(NOT EXISTS "${charts_output_path}")
    message(FATAL_ERROR "charts run did not write ${charts_output_path}")
  endif()
  file(READ "${charts_output_path}" charts_svg)
  if(NOT charts_svg MATCHES "<svg.*CLI Gym.*2024-01-01: 3")
    message(FATAL_ERROR "charts run output did not contain expected heatmap SVG")
  endif()
  run_csvzall_case("charts run writes markdown table output" 0 "" "" charts run cli-markdown --config "${charts_config_dir}/charts.json" --quiet)
  if(NOT EXISTS "${charts_markdown_path}")
    message(FATAL_ERROR "charts run did not write ${charts_markdown_path}")
  endif()
  file(READ "${charts_markdown_path}" charts_markdown)
  if(NOT charts_markdown MATCHES "\\| name  \\| value \\|")
    message(FATAL_ERROR "charts run markdown output did not contain expected table")
  endif()
endif()
run_csvzall_case("sql query from csv" 0 "bob,20" "" sql query --csv "${csv_path}" --sql "SELECT name, value FROM data WHERE value = 20" --quiet)
run_csvzall_case("sql query markdown from csv" 0 "\\| series \\| rows \\|" "" sql query --csv "${csv_path}" --format markdown --sql "SELECT series, COUNT(*) AS rows FROM data GROUP BY series ORDER BY series" --quiet)
run_csvzall_case("sql query regexp from csv" 0 "alice" "" sql query --csv "${csv_path}" --sql "SELECT name FROM data WHERE name REGEXP '(?i)^a'" --quiet)
run_csvzall_case("append concatenates duplicate keys" 0 "bob duplicate" "" append "${append_existing_path}" "${append_incoming_path}")
run_csvzall_case("merge skips existing stdout" 0 "cara" "added 1 row\\(s\\), skipped 1 row\\(s\\)" merge "${append_existing_path}" "${append_incoming_path}" --key id)
run_csvzall_case("sql load creates database" 0 "" "" sql load "${csv_path}" --dest "${db_path}" --table cars --quiet)

if(NOT EXISTS "${db_path}")
  message(FATAL_ERROR "sql load did not create ${db_path}")
endif()

run_csvzall_case("sql query from loaded db" 0 "3" "" sql query --db "${db_path}" --sql "SELECT COUNT(*) AS rows FROM cars" --quiet)
run_csvzall_case("unknown command fails" 1 "" "Failed to parse" nope)

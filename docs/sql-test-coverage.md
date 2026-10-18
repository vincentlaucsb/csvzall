# SQL load and query coverage

Issue [#3](https://github.com/vincentlaucsb/csvzall/issues/3) is covered by
`tests/test_sql_edges.cpp`, the existing SQL integration tests, and
`tests/cli_smoke.cmake`. The edge suite uses `[sql-edges]` for focused runs.

| Behavior | Regression coverage |
| --- | --- |
| Destination inference for `.csv`, other extensions, and no extension; explicit override | SQL export destination inference and explicit override |
| Existing file bytes/directories protected; invalid destination diagnosed | SQL export preserves existing destinations; SQL invalid inputs |
| Quoted table identifiers and failed-load cleanup after database close | SQL export quotes table names and cleans up failed loads |
| Numeric affinities, text identifiers, booleans, dates, large integers, header-only input | SQL export infers numeric affinities; existing big-integer tests; header-only section |
| Empty table/SQL, malformed SQL, comments-only SQL, non-result SQL, invalid format | SQL query CSV validation matrix, with and without callbacks |
| Trimmed and embedded-quote table names; NULL, escaping, zero rows, row counts | SQL query writers preserve quoted values NULL and empty results |
| Missing/invalid database, empty path, read-only writes, malformed SQL | SQL query database failures never create or mutate input |
| Working database creation failure and CSV loader failure | SQL query CSV catches invalid explicit SQLite working path; SQL invalid inputs |
| CSV and Markdown write/flush failures, exception-enabled streams, restored exception masks | SQL query reports output failures |
| Input extension detection and integer division token/comment/string handling | SQL input detection and warning token boundaries; existing SQL query tests |
| Gzip input through both load passes; destination inference | SQL gzip inputs support export inference and CSV query |
| ZIP load/query, automatic and explicit routing, stdin, legacy shorthand, delimiter, ambiguous flags, missing SQL, warning suppression | CLI SQL regression matrix |

The normal native CI matrix runs compressed input on Linux, Windows, and macOS.
An additional Linux configuration disables compressed input and verifies the ZIP
diagnostic through the same CLI suite. Gzip tests compile only when the feature
is enabled; disabled builds do not require zlib test helpers.

The coverage job runs the full suite with GCC instrumentation, prints a gcovr
summary, saves `coverage.xml` as the `coverage` artifact, and uploads it to
Codecov. Inspect the report for `src/pipeline/commands/sql_export.cpp` and
`src/pipeline/commands/sql_query.cpp`; absolute branch coverage is not a gate.
Operating-system permission errors, concurrent destination replacement, allocation
failure, and transient disk/device errors are not exhaustively simulated.
Rejecting stream buffers cover deterministic write and flush failures instead.

Local validation uses the existing Windows MSVC/Ninja build at
`out/build/codex-shared-shell-tests`, with compressed input enabled. The focused
suite passes 294 assertions across 15 cases. The full CTest suite also passes
when run outside the execution sandbox, which permits localhost HTTP tests,
temporary-file renames, and the MSVC compiler probe. The compressed-disabled
configuration is validated by the added Linux CI job rather than this local run.

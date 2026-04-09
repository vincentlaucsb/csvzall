# csvzall MVP Plan

## Scope
- Build a fast, single-binary CSV transform CLI with `derive` and `filter` commands.
- Keep data flow Unix-style: stdin/stdout for data, stderr for logs/errors.
- Use `csv-parser` v3.0.0, `argparse`, and `TinyExpr` only.

## FitNotes Summary
- Goal: compute per-set Epley estimates and prepare a summary-ready stream from FitNotes exports.
- Formula: `Epley1RM = Weight * (1 + Reps / 30)`.
- Command sequence target:
	- `filter "Weight > 0 && Reps > 0"`
	- `derive "Epley1RM = Weight * (1 + Reps / 30)"`
	- (next) add aggregation command to get `max(Epley1RM)` by `Exercise`.
- Implementation focus now:
	- Robust numeric parsing for `Weight` and `Reps`.
	- Clear errors for missing columns and non-numeric expression inputs.
	- Preserve stdin/stdout pipeline behavior for large CSVs.

## Milestones
- [x] Bootstrap build system with CMake and dependencies wired.
- [x] Add CLI skeleton with subcommands, stdin/file input plumbing, and stderr logger.
- [x] Add `head` command with table preview (`--rows`, default 50).
- [x] Extract initial `transform_pipeline` module and wire derive/filter through it.
- [x] Implement row-wise expression evaluation via TinyExpr in `transform_pipeline`.
- [x] Add `derive` command behavior: parse `NewCol = expression`, compute and emit output CSV.
- [x] Add `filter` command behavior: evaluate expression and emit matching rows.
- [x] Add clear error reporting for missing columns and expression failures.
- [x] Replace TinyExpr with ExprTk for full comparison and boolean operator support.
- [x] Add `summarize` command: `--group-by`, `--max`, `--show` for per-group aggregation.
- [x] Set up a test framework (Catch2) with CTest integration. (17 integration tests: filter, derive, summarize, timeseries — covers column matching, reducers, formatters)
- [x] Add case-insensitive column matching for `derive`, `filter`, and `summarize`:
	- Default behavior is case-insensitive for user-friendly lookup.
	- `--exact` flag enables strict case-sensitive matching per command.
	- Lookup precedence: exact match first, then case-insensitive fallback.
	- Ambiguous case-insensitive matches resolve to the first header in file order.
	- Verified: wrong-case expressions succeed by default and fail under `--exact`.
- [ ] Emit throughput stats on `--verbose` (rows, time, MiB/s).
- [ ] Add smoke tests for stdin piping, file input, derive/filter basics.
- [ ] Add generic `timeseries` command for x/y(+series) analytics output.

## Notes
- `csv-parser` can come from `-DCSV_PARSER_ROOT`, `external/csv-parser`, or sibling `../csv-parser`.
- Current `main.cpp` is intentionally a bootstrap skeleton and returns a non-zero code for unimplemented transforms.

## DataFrame Uses
- The csv-parser `DataFrame` stores columns as contiguous vectors (column-vectorized layout), which is cache-friendly for column-wise operations.
- This does **not** benefit the current `derive` and `filter` commands, which are row-oriented: ExprTk evaluates one expression per row, so column layout is irrelevant.
- The current single-pass streaming `summarize` is already optimal for single aggregations: O(n) time, O(groups) memory, and works on stdin without loading the full file.
- The crossover point where the DataFrame earns its keep is **multiple aggregations on the same groups** — e.g. `--max`, `--mean`, `--sum`, `--count` in one `summarize` call. A streaming implementation would require multiple passes; the DataFrame loads once and computes all aggregates together.
- Suggested approach when richer aggregations are added: if input is a file (bounded, seekable) load into DataFrame and use `group_by`; if input is stdin stay streaming. `RunOptions::input_is_stdin` already provides the branch condition.

## csv-parser Upstream Changes

### Case-insensitive `ColNames` lookup
- Currently `ColNames::index_of(name)` is exact-match only. Any string-based column lookup with wrong casing silently misses.
- Proposed change: add a parallel lowercase map built once when headers are set. Lookup tries exact match first (preserving existing behavior), then falls back to the lowercase map.
- Ambiguous case (e.g. headers `Weight` and `weight` both present): exact match wins; lowercase fallback only fires when exact match fails. Document this contract.
- No hot-path cost — the map is built at header parse time, and all per-row access in csvzall already goes through integer indexes after that.
- Once merged upstream, csvzall can remove its own `ToLower` workaround and rely on `index_of` directly.
- **Note:** csvzall's `../csv-parser` is a local fork. Re-apply this patch if pulling upstream changes.

## Timeseries Command (Planned)

### Goal
- Add a generic analytics primitive for time series extraction without introducing domain-specific commands.
- Keep rendering concerns (Obsidian chart code blocks, append behavior) in shell scripts, not in csvzall.

### Proposed CLI
- `timeseries --x <col> --y <col> [input]`
- `timeseries --x <col> --y <col> --series <col> [input]`
- `timeseries --x <col> --y <col> --series <col> --reduce <max|min|sum|avg|last> [input]`
- `timeseries --x <col> --y <col> --series <col> --format <csv|markdown> [input]`
- `timeseries --x <col> --y <col> --series <col> --exact [input]`

### Output Contract
- Without `--series`: output columns are `x,y`.
- With `--series`: output columns are `series,x,y`.
- Duplicate key behavior for `(series, x)` is controlled by `--reduce`.

### Implementation Notes
- Use default case-insensitive column matching with `--exact` opt-in for strict mode.
- For date-like values in `YYYY-MM-DD`, lexical ordering is chronological.
- Rows with non-numeric `y` are skipped with verbose diagnostics.
- Keep this command chart-tool agnostic; the PowerShell layer injects Obsidian chart code blocks.

### Validation
- Verify default case-insensitive lookups for `--x`, `--y`, and `--series`.
- Verify `--exact` fails on wrong-case column names.
- Verify reducer correctness on duplicate `(series, x)` rows.
- Verify markdown output renders valid Obsidian tables.

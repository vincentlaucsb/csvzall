# Project Guidance (Mirror)

This file mirrors [AGENTS.md](AGENTS.md).

`AGENTS.md` is canonical. Keep both files in sync in the same commit.

## Structure

- Keep pipeline orchestration in `src/transform_pipeline.*`.
- Keep shared helpers in `src/pipeline/common/`.
- Keep SQLite integration (CSV loader, query execution, memory strategy) in `src/pipeline/sqlite/`.
- Keep command implementations in `src/pipeline/commands/`.

## Command categories

- **Streaming** (`head`): row-by-row, no buffering, no SQLite.
- **SQLite-backed** (`filter`, `derive`, `summarize`, `timeseries`): load CSV into SQLite, execute SQL, stream output.
- **Export** (`sql`): load CSV into a persistent SQLite database file; no transform output stream.
- **File update** (`append`, `merge`): exact-header CSV file combination; `merge` is the keyed rerunnable import primitive.
- **Rendering** (`calendar`, `heatmap`): convert fixed-shape tabular data to Markdown or SVG report artifacts.

## SQLite conventions

- Use [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) with its bundled internal SQLite.
- All columns loaded as `NUMERIC` affinity; SQLite stores each cell as INTEGER, REAL, or TEXT based on content — no per-column inference needed. Numeric comparisons in WHERE work correctly.
- All column names quoted with `"` in generated SQL.
- Memory strategy: in-memory below threshold, temp-file database above it.
- Default threshold: 256 MB (`RunOptions::sqlite_threshold_mb`). Not yet exposed as a CLI flag.
- Stdin always uses in-memory. Explicit file path can be set via `RunOptions::sqlite_db_path`. Not yet exposed as a CLI flag.
- Temp-file databases: unique name, deleted on exit via RAII.

## Rules

- Target C++23 and prefer modern features.
- Use `std::string_view` at safe API boundaries.
- Do not store `std::string_view` in persistent structures.
- Keep stdout for data and stderr for diagnostics.
- Keep `RunOptions` as the shared cross-command behavior point (including SQLite threshold).
- Avoid duplicating logic. Move shared helpers to `src/pipeline/common/` (general) or `src/pipeline/sqlite/` (SQLite-specific). If no clean home exists, add a `// TODO(dedup):` comment rather than copying.

## CLI help quality

- Every public CLI command must be self-discoverable from `--help`.
- Command help must include the required input shape, output shape, one realistic example, important edge-case behavior, and related commands or a pipeline example when relevant.
- Do not rely on README-only documentation for command-critical behavior. Agents often inspect `--help` before reading project docs.

## Dependency documentation

- When adding, removing, or materially changing a third-party dependency, update the README dependency table in the same change.
- Include the dependency name, author or maintainer with a link, role in csvzall, and how it is sourced (local checkout, FetchContent, system package, test-only, optional, etc.).

## Local CMake workflow

- Prefer the local, ignored `CMakeUserPresets.json` when it exists; it is the workspace-specific record of the preferred generator and build directory.
- Do not reconfigure an existing CMake build directory with a different generator. If the generator needs to change, create a new build directory instead.
- Before manually running `cmake -S ... -B ...`, inspect the target build directory's existing `CMakeCache.txt` or use a fresh build directory to avoid generator mismatches.
- On Windows with MSVC + Ninja, run configure/build from a Visual Studio developer environment, preferably through the Visual Studio-bundled CMake. Bare sandboxed shells can hang during CMake's compiler/linker probe.

## Viewer development mode

- The production `view` command serves embedded first-party viewer assets plus embedded AG Grid and Popright assets.
- For vanilla JS/CSS/HTML iteration, use `csvzall view <file.csv> --viewer-assets src/viewer` or set `CSVZALL_VIEWER_ASSETS=<absolute-or-relative-src/viewer-path>`.
- Developer asset mode reloads `src/viewer/index.html`, `src/viewer/viewer.css`, and `src/viewer/viewer.js` from disk on each request. Refreshing the browser is enough after editing those files; rebuilding C++ is not required.
- AG Grid and Popright vendor files remain embedded in developer asset mode. Changes to `vendor/ag-grid/*`, `vendor/popright/*`, or `cmake/embed_viewer_assets.cmake` still require regenerating/rebuilding.
- Keep the viewer framework-free. Prefer small vanilla JS modules/helpers for modals, context menus, grid adapters, and API calls.

## csv-parser feedback loop

- `CSV_PARSER_TODOS.md` is a committed working log for issues discovered while csvzall exercises csv-parser in real workflows.
- When csv-parser integration reveals API smells, heavy materialization paths, ambiguous documentation, missing examples, or missing library features, add a concise note to `CSV_PARSER_TODOS.md`.
- Prefer actionable notes: describe the workflow pressure from csvzall, why the current csv-parser API/docs made the choice easy or hard, and what improvement would help.
- Do not block csvzall work on speculative csv-parser changes. Use the working log when the right fix belongs upstream or needs more design thought.

## Testing docs location

- `tests/AGENTS.md` is canonical for tests.
- `tests/CLAUDE.md` is the tests mirror.

## Sync requirement

If this file changes, update [AGENTS.md](AGENTS.md).
If [AGENTS.md](AGENTS.md) changes, update this file.

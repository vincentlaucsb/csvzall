# Project Guidance (Mirror)

This file mirrors [AGENTS.md](AGENTS.md).

`AGENTS.md` is canonical. Keep both files in sync in the same commit.

## Structure

- Keep pipeline orchestration in `src/transform_pipeline.*`.
- Keep shared helpers in `src/pipeline/common/`.
- Keep reusable CSV-to-chart config and rendering helpers in `src/charts/`; SVGPlot remains the general-purpose chart drawing library.
- Keep reusable SQLite integration (CSV loader, query execution, memory strategy) in `src/sqlite/`.
- Keep reusable PostgreSQL integration (connection, schema inference, row loading, export orchestration) in `src/postgres/`.
- Keep command implementations in `src/pipeline/commands/`.

## Library refactor status

- csvzall is being refactored toward a reusable C++ library plus thin CLI orchestration. This split is not complete yet.
- Prefer moving code that is useful outside the CLI into top-level module folders and non-`pipeline` namespaces such as `src/sqlite` + `csvzall::sqlite` or `src/postgres` + `csvzall::postgres`.
- Keep `*Command` classes and `Run*` command entry points as thin adapters: parse command-level choices, adapt `RunOptions`/`LoggerCallbacks`, call reusable library functions, and map results back to `RunStats`/return codes.
- Keep temporary compatibility aliases when useful, for example `namespace csvzall::pipeline { namespace postgres = ::csvzall::postgres; }`, so the public pipeline API can remain stable while internals move.
- Do not change CLI behavior, endpoint behavior, file formats, or diagnostics just because code moved layers.

## Command categories

- **Streaming** (`head`): row-by-row, no buffering, no SQLite.
- **SQLite-backed** (`filter`, `derive`, `summarize`, `timeseries`): load CSV into SQLite, execute SQL, stream output.
- **Export** (`sql`): load CSV into a persistent SQLite database file; no transform output stream.
- **File update** (`append`, `merge`): exact-header CSV file combination; `merge` is the keyed rerunnable import primitive.
- **Rendering** (`calendar`, `heatmap`): convert fixed-shape tabular data to Markdown or SVG report artifacts.

## SQLite conventions

- Use [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) with its bundled internal SQLite.
- CSV input is scanned with csv-parser scalar type inference before SQLite load.
- Columns containing only integer/real values load as `NUMERIC`; columns containing text, booleans, timestamps, or `CSV_BIGINT` values load as `TEXT` to preserve exact lexical identifiers. Numeric comparisons in WHERE work correctly for inferred numeric columns.
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
- Avoid duplicating logic. Move shared helpers to `src/pipeline/common/` (general), `src/sqlite/` (SQLite-specific), or another top-level reusable module when appropriate. If no clean home exists, add a `// TODO(dedup):` comment rather than copying.

## CLI help quality

- Every public CLI command must be self-discoverable from `--help`.
- Command help must include the required input shape, output shape, one realistic example, important edge-case behavior, and related commands or a pipeline example when relevant.
- Do not rely on README-only documentation for command-critical behavior. Agents often inspect `--help` before reading project docs.

## Dependency documentation

- When adding, removing, or materially changing a third-party dependency, update the README dependency table in the same change.
- Include the dependency name, author or maintainer with a link, role in csvzall, and how it is sourced (local checkout, FetchContent, system package, test-only, optional, etc.).

## Versioning

- Git tags are the canonical release markers.
- The csvzall source version lives in exactly these source markers:
  - `CMakeLists.txt`: `project(csvzall VERSION ...)`
  - `Doxyfile`: `PROJECT_NUMBER`
  - `src/main.cpp`: `argparse::ArgumentParser program("csvzall", "...")`
- Keep all three source version markers in sync with each other and with the intended release tag.
- Before bumping, releasing, or stating the current csvzall version, verify the three source markers directly. Do not rely on memory, issue text, branch names, release notes, or previous conversation context.
- If the source markers disagree with each other or with the requested/current release tag, stop and report the mismatch before choosing a new version.
- When preparing a release bump, update all three source markers in the same commit, then create the matching Git tag after that commit.
- Do not update unrelated dependency version references, such as the README dependency table, when bumping csvzall itself.

## Release artifacts

- The Obsidian helper release asset is intentionally narrower than the full CLI release. It supports CSV editing, viewing, charting/SVGPlot, and SQLite-backed workflows.
- Obsidian helper builds must be configured with `CSVZALL_OBSIDIAN_BUILD=ON`.
- `CSVZALL_OBSIDIAN_BUILD=ON` must keep PostgreSQL support, keychain support, and compressed input support disabled.
- The Windows Obsidian helper asset must contain `csvzall.exe` only and must not package DLLs. The executable may depend on Windows system DLLs, but it must not require non-system runtime DLLs such as PostgreSQL, OpenSSL, zlib, libintl/iconv, or MinGW runtime DLLs.
- If an Obsidian build or package check reports a non-system runtime DLL dependency, do not copy the DLL into the Obsidian asset. Disable the feature, switch the dependency to a static build, or stop and report that the Obsidian artifact contract would change.

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

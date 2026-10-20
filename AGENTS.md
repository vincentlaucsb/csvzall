# Project Guidance

This file is the canonical AI reference for this repository.

`CLAUDE.md` must stay in sync with this file in the same commit.

## Git synchronization

- When working on `master` or `main`, pull from `origin` before editing if the last successful pull is unknown or more than one hour old. Use `git pull --ff-only origin master` (or `main`, matching the current branch). A fetch alone does not update the working branch.
- During long sessions, refresh again before starting a new independent task once that window has elapsed. Do not assume a checkout is current because an earlier task fetched it.
- Preserve uncommitted and untracked work, and coordinate with other agents using the checkout before pulling. Never reset, discard changes, or force-update a branch to make a pull succeed; inspect and reconcile any overlap or divergence.
- On a feature branch, keep its intended upstream and do not switch branches or merge the default branch merely to satisfy this rule.

## Structure

- Keep public pipeline orchestration in `src/transform_pipeline.*`.
- Keep reusable helpers in `src/pipeline/common/`.
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
- **Export** (`sql load`): load CSV into a persistent SQLite database file (a new file, or another table via `--add-table`/`--replace-table`); no transform output stream.
- **File update** (`append`, `merge`): exact-header CSV file combination; `merge` is the keyed rerunnable import primitive.
- **Rendering** (`calendar`, `heatmap`): convert fixed-shape tabular data to Markdown or SVG report artifacts.

## SQLite conventions

- Use [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) with its bundled internal SQLite.
- CSV input is scanned with csv-parser scalar type inference before SQLite load.
- Columns that contain only integer/real values are loaded as `NUMERIC`; columns that contain text, booleans, timestamps, or `CSV_BIGINT` values are loaded as `TEXT` to preserve exact lexical identifiers. Numeric comparisons in WHERE work correctly for inferred numeric columns.
- All column names are quoted with `"` in generated SQL.
- Memory strategy: in-memory (`:memory:`) below threshold, temp-file database above it.
- Default threshold: 256 MB (`RunOptions::sqlite_threshold_mb`). Not yet exposed as a CLI flag.
- Stdin input always uses in-memory. Explicit file path can be set via `RunOptions::sqlite_db_path`. Not yet exposed as a CLI flag.
- Temp-file databases use a unique name (`csvzall_<pid>_<random>.db`) and are deleted on exit via RAII.
- Every csvzall connection gets `RegisterCsvzallSqliteFunctions()` (`src/sqlite/sql_functions.*`): compiled-in sqlean modules (crypto, fuzzy, regexp/PCRE2, stats, text, uuid), `sqlean_version()`, and `parse_date`/`date_precision`. SQLite math functions are enabled on the bundled `sqlite3` target.
- Only compile in sqlean modules that are pure functions. Do not add modules that read or write files (`fileio`, `vsv`), define SQL functions (`define`), or replace SQLite built-ins (`unicode`). A new module or sqlean bump must keep `pragma_function_list` free of non-built-in functions that share a built-in's name; drop such aliases in `cmake/sqlean_patches.cmake`.
- Native extensions load only from the local `sql query --load-extension` flag through the C API (`RunOptions::sqlite_load_extensions`). Never let `charts.json`, the viewer, or SQL text enable extension loading.

## Design rules

- Target C++23 and prefer modern language features.
- Use `std::string_view` at API boundaries where lifetimes are safe.
- Never store `std::string_view` in persistent data structures; store `std::string` instead.
- Keep stdout for data output and stderr for diagnostics.
- Keep `RunOptions` as the shared flow point for cross-command behavior (for example exact matching and SQLite threshold).
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
- Bump versions only with `node scripts/version.mjs bump X.Y.Z`; it rewrites all three markers together. `node scripts/version.mjs check` verifies them (CI fails when they disagree, and the release workflow runs `check --tag <tag>` before building assets). If a new version marker is added, add it to the script's `markers` list in the same change.
- If the source markers disagree with each other or with the requested/current release tag, stop and report the mismatch before choosing a new version.
- When preparing a release bump, run the bump script and commit all three source markers together, then create the matching Git tag after that commit.
- Do not update unrelated dependency version references, such as the README dependency table, when bumping csvzall itself.

## Controlled dependency remediation

- When an issue must be fixed in, or shipped through, a repository we control (svg, svgplot, csv-parser, csvzall, obsidian-csvzall), follow `.claude/skills/dependency-remediation/SKILL.md`.
- Walk the controlled dependency tree deepest-first: fix, bump the version (patch or minor per that workflow), get the PR green, merge, release, then re-pin the next consumer. Never pin a consumer to an unmerged or unreleased commit.

## Release artifacts

- Obsidian helper builds are owned by the obsidian-csvzall release-preparation pipeline, which pins this repository and its dependencies by commit and reuses verified binaries for unchanged build identities. Do not restore duplicate Obsidian helper jobs in csvzall release workflows. The narrower build still supports CSV editing, viewing, charting/SVGPlot, and SQLite-backed workflows.
- Obsidian helper builds must be configured with `CSVZALL_OBSIDIAN_BUILD=ON`.
- `CSVZALL_OBSIDIAN_BUILD=ON` must keep PostgreSQL support, keychain support, and compressed input support disabled.
- Obsidian helper builds must prefer in-tree/static dependencies over system shared packages for compiled/runtime dependencies. In particular, keep `CSVZALL_PREFER_SYSTEM_SIMDJSON=OFF`, `CSVZALL_PREFER_SYSTEM_NLOHMANN_JSON=OFF`, and `CSVZALL_PREFER_SYSTEM_SVGPLOT=OFF` for Obsidian release assets.
- Obsidian helper assets must contain only the native `csvzall` executable (`csvzall.exe` on Windows, `csvzall` on Unix-like platforms). Do not package DLLs, `.so` files, `.so.*` files, or `.dylib` files.
- The executable may depend on OS-provided system libraries, but it must not require non-system runtime shared libraries such as PostgreSQL, OpenSSL, zlib, libintl/iconv, MinGW runtime DLLs, or Homebrew/shared-library equivalents.
- If an Obsidian build or package check reports a non-system runtime shared library dependency, do not copy the library into the Obsidian asset. Disable the feature, switch the dependency to a static build, or stop and report that the Obsidian artifact contract would change.

## Local CMake workflow

- Prefer the local, ignored `CMakeUserPresets.json` when it exists; it is the workspace-specific record of the preferred generator and build directory.
- Do not reconfigure an existing CMake build directory with a different generator. If the generator needs to change, create a new build directory instead.
- Before manually running `cmake -S ... -B ...`, inspect the target build directory's existing `CMakeCache.txt` or use a fresh build directory to avoid generator mismatches.
- On Windows with MSVC + Ninja, run configure/build from a Visual Studio developer environment, preferably through the Visual Studio-bundled CMake. Bare sandboxed shells can hang during CMake's compiler/linker probe.

## Viewer development mode

- The production `view` command serves embedded first-party viewer assets plus embedded AG Grid and Popright assets.
- For vanilla JS/CSS/HTML iteration, use `csvzall view <file.csv> --viewer-assets src/viewer` or set `CSVZALL_VIEWER_ASSETS=<absolute-or-relative-src/viewer-path>`.
- Developer asset mode reloads `src/viewer/index.html`, `src/viewer/viewer.css`, and `src/viewer/viewer.js` from disk on each request. Refreshing the browser is enough after editing those files; rebuilding C++ is not required.
- Every native build checks embedded asset content hashes, including the generator script, so vendor updates with preserved archive timestamps are detected. Unchanged content preserves generated output timestamps and avoids recompilation. Keep this check in the shared `cmake/viewer_assets.cmake` target; do not replace it with timestamp-only file dependencies.
- AG Grid and Popright vendor files remain embedded in developer asset mode. Changes to `vendor/ag-grid/*`, `vendor/popright/*`, or `cmake/embed_viewer_assets.cmake` still require regenerating/rebuilding.
- Keep the viewer framework-free. Prefer small vanilla JS modules/helpers for modals, context menus, grid adapters, and API calls.

## Viewer theme design

- `view --obsidian` is an explicit plugin-supplied mode: the host owns automatic chart regeneration after CSV saves. Keep standalone automation, manual chart actions, and CLI JSON storage unchanged. Do not discover Obsidian vaults/profiles or access plugin data/Sync settings upstream.
- Native startup JSON `obsidianMode.version` is the host chart-automation protocol version (currently 1), independent of the three source release markers. Keep its host acknowledgment contract and tests synchronized with obsidian-csvzall.

- Embedded Obsidian viewers intentionally follow the host's resolved colors, accent, interface font, and light/dark mode, including live changes. Keep this behavior shared between native and WASM viewers through `src/viewer/modules/host-theme.mjs`.
- Host theming is optional. Standalone viewers must remain usable without Obsidian and retain their system-aware appearance when no host theme is supplied. Do not introduce an Obsidian runtime dependency, persisted theme file, or required handshake for startup.
- Keep the versioned parent-window `postMessage` protocol and its allowlist validation. Theme updates must preserve active edits and focus without reloading CSV data or recreating the grid.
- Preserve regression coverage for both hosted theme updates and the standalone fallback.

## csv-parser feedback loop

- `CSV_PARSER_TODOS.md` is a committed working log for issues discovered while csvzall exercises csv-parser in real workflows.
- When csv-parser integration reveals API smells, heavy materialization paths, ambiguous documentation, missing examples, or missing library features, add a concise note to `CSV_PARSER_TODOS.md`.
- Prefer actionable notes: describe the workflow pressure from csvzall, why the current csv-parser API/docs made the choice easy or hard, and what improvement would help.
- Do not block csvzall work on speculative csv-parser changes. Use the working log when the right fix belongs upstream or needs more design thought.

## Testing docs location

Testing-specific concerns live in:
- `tests/AGENTS.md` (canonical)
- `tests/CLAUDE.md` (mirror)

## Sync requirement

`AGENTS.md` is canonical. `CLAUDE.md` is the mirror.
If one changes, update the other in the same commit.

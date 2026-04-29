# Project Guidance

This file is the canonical AI reference for this repository.

`CLAUDE.md` must stay in sync with this file in the same commit.

## Structure

- Keep public pipeline orchestration in `src/transform_pipeline.*`.
- Keep reusable helpers in `src/pipeline/common/`.
- Keep SQLite integration (CSV loader, query execution, memory strategy) in `src/pipeline/sqlite/`.
- Keep command implementations in `src/pipeline/commands/`.

## Command categories

- **Streaming** (`head`): row-by-row, no buffering, no SQLite.
- **SQLite-backed** (`filter`, `derive`, `summarize`, `timeseries`): load CSV into SQLite, execute SQL, stream output.
- **Export** (`sql`): load CSV into a persistent SQLite database file; no transform output stream.

## SQLite conventions

- Use [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) with its bundled internal SQLite.
- All columns are loaded as `NUMERIC` affinity; SQLite stores each cell as INTEGER, REAL, or TEXT based on content — no per-column inference needed. Numeric comparisons in WHERE work correctly.
- All column names are quoted with `"` in generated SQL.
- Memory strategy: in-memory (`:memory:`) below threshold, temp-file database above it.
- Default threshold: 256 MB (`RunOptions::sqlite_threshold_mb`). Not yet exposed as a CLI flag.
- Stdin input always uses in-memory. Explicit file path can be set via `RunOptions::sqlite_db_path`. Not yet exposed as a CLI flag.
- Temp-file databases use a unique name (`csvzall_<pid>_<random>.db`) and are deleted on exit via RAII.

## Design rules

- Target C++23 and prefer modern language features.
- Use `std::string_view` at API boundaries where lifetimes are safe.
- Never store `std::string_view` in persistent data structures; store `std::string` instead.
- Keep stdout for data output and stderr for diagnostics.
- Keep `RunOptions` as the shared flow point for cross-command behavior (for example exact matching and SQLite threshold).
- Avoid duplicating logic. Move shared helpers to `src/pipeline/common/` (general) or `src/pipeline/sqlite/` (SQLite-specific). If no clean home exists, add a `// TODO(dedup):` comment rather than copying.

## Dependency documentation

- When adding, removing, or materially changing a third-party dependency, update the README dependency table in the same change.
- Include the dependency name, author or maintainer with a link, role in csvzall, and how it is sourced (local checkout, FetchContent, system package, test-only, optional, etc.).

## Local CMake workflow

- Prefer the local, ignored `CMakeUserPresets.json` when it exists; it is the workspace-specific record of the preferred generator and build directory.
- Do not reconfigure an existing CMake build directory with a different generator. If the generator needs to change, create a new build directory instead.
- Before manually running `cmake -S ... -B ...`, inspect the target build directory's existing `CMakeCache.txt` or use a fresh build directory to avoid generator mismatches.
- On Windows with MSVC + Ninja, run configure/build from a Visual Studio developer environment, preferably through the Visual Studio-bundled CMake. Bare sandboxed shells can hang during CMake's compiler/linker probe.

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

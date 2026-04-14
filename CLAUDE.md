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

## SQLite conventions

- Use [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) with its bundled internal SQLite.
- All columns loaded as `NUMERIC` affinity; SQLite stores each cell as INTEGER, REAL, or TEXT based on content — no per-column inference needed. Numeric comparisons in WHERE work correctly.
- All column names quoted with `"` in generated SQL.
- Memory strategy: in-memory below threshold, temp-file database above it.
- Default threshold: 256 MB (`RunOptions::sqlite_threshold_mb`). Not yet exposed as a CLI flag.
- Stdin always uses in-memory. Explicit file path can be set via `RunOptions::sqlite_db_path`. Not yet exposed as a CLI flag.
- Temp-file databases: unique name, deleted on exit via RAII.

## Rules

- Target C++20 and prefer modern features.
- Use `std::string_view` at safe API boundaries.
- Do not store `std::string_view` in persistent structures.
- Keep stdout for data and stderr for diagnostics.
- Keep `RunOptions` as the shared cross-command behavior point (including SQLite threshold).
- Avoid duplicating logic. Move shared helpers to `src/pipeline/common/` (general) or `src/pipeline/sqlite/` (SQLite-specific). If no clean home exists, add a `// TODO(dedup):` comment rather than copying.

## Testing docs location

- `tests/AGENTS.md` is canonical for tests.
- `tests/CLAUDE.md` is the tests mirror.

## Sync requirement

If this file changes, update [AGENTS.md](AGENTS.md).
If [AGENTS.md](AGENTS.md) changes, update this file.

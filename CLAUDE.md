# Project Guidance (Mirror)

This file mirrors [AGENTS.md](AGENTS.md).

`AGENTS.md` is canonical. Keep both files in sync in the same commit.

## Structure

- Keep pipeline orchestration in `src/transform_pipeline.*`.
- Keep shared helpers in `src/pipeline/common/`.
- Keep expression integration in `src/pipeline/expression/`.
- Keep command implementations in `src/pipeline/commands/`.

## Rules

- Target C++20 and prefer modern features.
- Use `std::string_view` at safe API boundaries.
- Do not store `std::string_view` in persistent structures.
- Keep stdout for data and stderr for diagnostics.
- Keep `RunOptions` as the shared cross-command behavior point.

## Testing docs location

- `tests/AGENTS.md` is canonical for tests.
- `tests/CLAUDE.md` is the tests mirror.

## Sync requirement

If this file changes, update [AGENTS.md](AGENTS.md).
If [AGENTS.md](AGENTS.md) changes, update this file.

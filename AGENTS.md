# Project Guidance

This file is the canonical AI reference for this repository.

`CLAUDE.md` must stay in sync with this file in the same commit.

## Structure

- Keep public pipeline orchestration in `src/transform_pipeline.*`.
- Keep reusable helpers in `src/pipeline/common/`.
- Keep expression integration in `src/pipeline/expression/`.
- Keep command implementations in `src/pipeline/commands/`.

## Design rules

- Target C++20 and prefer modern language features.
- Use `std::string_view` at API boundaries where lifetimes are safe.
- Never store `std::string_view` in persistent data structures; store `std::string` instead.
- Keep stdout for data output and stderr for diagnostics.
- Keep `RunOptions` as the shared flow point for cross-command behavior (for example exact matching).

## Testing docs location

Testing-specific concerns live in:
- `tests/AGENTS.md` (canonical)
- `tests/CLAUDE.md` (mirror)

## Sync requirement

`AGENTS.md` is canonical. `CLAUDE.md` is the mirror.
If one changes, update the other in the same commit.

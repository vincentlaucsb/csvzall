# Test Guidance

This file is the canonical AI reference for tests.

`tests/CLAUDE.md` must stay in sync with this file in the same commit.

## Scope

- Keep test-specific guidance in `tests/` only.
- Keep general project guidance at the repository root (`AGENTS.md`, `CLAUDE.md`).

## Test rules

- Prefer integration tests using StringStream I/O.
- Use helpers in `common_test_utils.hpp` for fixture creation and CSV parsing.
- Cover behavior changes in the same PR that introduces them.
- Keep test wiring in `tests/CMakeLists.txt` (including Catch2 setup).

## Run

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Sync requirement

`tests/AGENTS.md` is canonical. `tests/CLAUDE.md` is the mirror.
If one changes, update the other in the same commit.

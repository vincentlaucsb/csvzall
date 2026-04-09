# Test Guidance (Mirror)

This file mirrors [AGENTS.md](AGENTS.md) in `tests/`.

`tests/AGENTS.md` is canonical. Keep both files in sync in the same commit.

## Scope

- Keep test-specific guidance in `tests/` only.
- Keep general project guidance at the repository root.

## Rules

- Prefer integration tests with StringStream I/O.
- Reuse helpers in `common_test_utils.hpp`.
- Keep test target wiring and Catch2 setup in `tests/CMakeLists.txt`.
- Add or update tests whenever behavior changes.

## Run

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Sync requirement

If this file changes, update `tests/AGENTS.md`.
If `tests/AGENTS.md` changes, update this file.

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

## Embedded viewer build regression

`csvzall_viewer_asset_build_test` configures and compiles a small isolated CMake
fixture using the production embedding target. It checks changed asset bytes with
old timestamps, timestamp-only changes, generator changes, missing output recovery,
and no-op builds that must not relink. It does not build the full CLI or download
dependencies. Run it with
`ctest --test-dir build -R csvzall_viewer_asset_build_test --output-on-failure`,
from the compiler environment used for the normal build.

To run it directly, use `node --test tests/viewer_assets_build.test.mjs`.
`CSVZALL_TEST_CMAKE` selects the CMake executable and `CSVZALL_TEST_GENERATOR`
selects the generator (default: Ninja). On Windows, use a Visual Studio developer
shell so MSVC, Ninja, and the SDK are available.

Compiled fixtures must live under the build tree, not the operating system's
temporary directory: MSBuild warns with MSB8029 and can relink unchanged outputs
under a short-path TEMP alias. CTest supplies `CSVZALL_TEST_WORK_DIR`; direct runs
default to `out/build/viewer-asset-tests`. Keep the no-op timestamp assertions,
and retain build output in assertion failures for diagnosing generator issues.

## Sync requirement

`tests/AGENTS.md` is canonical. `tests/CLAUDE.md` is the mirror.
If one changes, update the other in the same commit.

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

If this file changes, update `tests/AGENTS.md`.
If `tests/AGENTS.md` changes, update this file.

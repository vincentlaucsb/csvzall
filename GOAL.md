# Goal: Ship Basic Editing for `csvzall view`

Build the MVP editing path for `csvzall view` so CSV files can work as large,
local, Obsidian-friendly table stores.

This is not an Excel replacement. Do not add formulas, computed cells, workbook
state, column editing, formatting tools, or hidden application state. CSV is the
storage format, `view --edit` is direct data maintenance, SQLite/SQL is for
computation, and Markdown/Obsidian is for presentation.

## Product Shape

- Default `csvzall view file.csv` remains read-only.
- Add `csvzall view file.csv --edit` to enable editing.
- Editing is for local file inputs only, not stdin.
- Support:
  - cell edits
  - row deletion
  - arbitrary row insertion
  - dirty/save state in the browser
  - atomic save back to the source CSV
- Do not support:
  - formulas
  - computed cells
  - column add/rename/delete
  - spreadsheet formatting
  - preserving original byte formatting

## Architecture

Use csv-parser/DataFrame for materialized in-memory editing.

- Refactor materialized `view` mode away from ad hoc
  `std::vector<std::vector<std::string>>` storage and toward DataFrame.
- Keep paged mode backed by `CsvIndexedFile` and row-offset paging for large
  files.
- Add or adapt a common editable table model so the HTTP/UI layer can work
  against either:
  - materialized DataFrame-backed data
  - paged CSV data plus an edit patch overlay
- Keep one save path conceptually: produce final rows, then rewrite the CSV
  atomically.

## csv-parser Stance

Pragmatism first.

csvzall is currently vendoring/patching csv-parser locally. Make the DataFrame
changes needed for the MVP in the vendored/patched path and ship the behavior.
Do not block on upstream API design.

Needed behavior:

- Use existing DataFrame row deletion support where possible.
- Add arbitrary row insertion if DataFrame does not already provide it.
- Inserted rows must match the current schema/header shape.
- Keep edits deterministic and easy to reason about.
- Add notes to `CSV_PARSER_TODOS.md` for anything that should later be
  backported or redesigned upstream.

## Save Semantics

- Save rewrites the CSV through a temporary sibling file, then replaces the
  original only after the write succeeds.
- Preserve headers and CSV data correctness.
- Do not try to preserve original quoting, whitespace, line endings, or byte
  layout.
- Capture source file size and mtime when the viewer opens.
- Before save, compare current source file size and mtime.
- If the file changed externally, refuse to save with a clear error.
- Save endpoint must require the existing random session token.

## Browser UI

- Add an explicit editable mode only when `--edit` is supplied.
- Show clear read-only vs editable state.
- Show dirty state after edits/deletes/inserts.
- Provide a Save control.
- Provide row delete and row insert controls.
- Keep the UI utilitarian and table-focused.
- Do not move CSV editing logic into the Obsidian plugin.

## Obsidian Plugin

The plugin should remain a host/launcher for the local csvzall viewer.

- Keep launching `csvzall view <file> --no-open --startup-json`.
- Do not implement CSV editing logic inside the plugin.
- Update plugin wording only as needed to reflect the viewer capability.
- The binary should carry behavior improvements; the plugin can stay thin.

## Tests

Add or update focused tests for:

- DataFrame-backed materialized `view` mode still serves rows correctly.
- Cell edits persist through save.
- Row deletion persists through save.
- Arbitrary row insertion persists through save.
- Save is token-gated.
- Save refuses when the source file changed externally.
- Read-only mode still rejects mutation endpoints.
- Paged mode preserves row paging and applies edit overlays if edit support is
  implemented there in this pass.
- CLI smoke coverage for `view --edit --startup-json` if practical.

## Verification

Use standalone CMake with the VS 2026 compiler environment.

Run:

```powershell
"C:\Program Files\CMake\bin\cmake.exe" -S . -B out\build\x64-Release-vs2026-ninja -G Ninja -DCSVZALL_CXX_STANDARD=23
"C:\Program Files\CMake\bin\cmake.exe" --build out\build\x64-Release-vs2026-ninja --target csvzall_tests csvzall
"C:\Program Files\CMake\bin\ctest.exe" --test-dir out\build\x64-Release-vs2026-ninja --output-on-failure
```

If configure/build paths need adjustment, keep using standalone CMake and the
VS 2026 compiler environment. Do not use the Visual Studio-bundled CMake.

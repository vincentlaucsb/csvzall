# Goal: Spec-Driven Automated Charting

Build automated chart generation for CSV-backed workflows without turning CSVs
into plugin-owned documents.

The core idea: CSV files remain ordinary data files, generated charts remain
ordinary SVG or Markdown artifacts, and `csvzall` provides the deterministic
bridge between them.

## Spec-Driven CLI Args / JSON Config

Use one source of truth for chart definitions.

Each chart type must define a single internal spec shape. CLI arguments and JSON
config both parse into that same chart spec, and rendering accepts the chart spec
rather than raw CLI parser state.

Example CLI:

```powershell
csvzall heatmap gym_attendance.csv --date attendance_date --start 2025-05-17 --end 2026-05-17 --title "Gym Attendance"
```

Equivalent JSON config:

```json
{
  "charts": [
    {
      "id": "gym-attendance-heatmap",
      "type": "heatmap",
      "input": "Exercise/output/gym_attendance.csv",
      "output": "Exercise/output/gym_attendance_heatmap.svg",
      "options": {
        "date": "attendance_date",
        "start": "2025-05-17",
        "end": "2026-05-17",
        "title": "Gym Attendance"
      },
      "runOnSave": true
    }
  ]
}
```

Both forms should become the same internal object before validation/rendering.
This prevents drift between CLI behavior, config behavior, Obsidian behavior,
help text, and future generated schema/docs.

## Product Shape

- Add project-level chart config, preferably `.csvzall/charts.json`.
- Do not prepend metadata, YAML, comments, or plugin directives to user CSVs.
- Do not require special Markdown syntax or a chart plugin block.
- Generated charts are normal files, especially SVG files, which users can embed
  in Obsidian like any other artifact.
- Keep PowerShell or user scripts responsible for API fetching, authentication,
  pagination, data shaping, and other world-facing orchestration.
- Keep `csvzall` responsible for deterministic CSV-to-chart rendering.

## MVP Order

Implement in this order so each slice is independently useful:

1. Refactor existing direct `heatmap` CLI parsing into a reusable heatmap chart
   spec and shared validator.
2. Add JSON config loading and `csvzall charts run`.
3. Make configured heatmap generation write configured outputs directly.
4. Add focused CLI/config tests.
5. Only after the CLI path works, add Obsidian automation against the same JSON
   config and same `csvzall charts run` command.
6. Add viewer chrome cleanup when touching the viewer for chart UI. Do not block
   the CLI/config MVP on the chart modal.

## CLI Shape

Keep existing direct chart commands, and add config-backed chart commands.

Direct command:

```powershell
csvzall heatmap input.csv --date attendance_date --output output.svg
```

Config command:

```powershell
csvzall charts run
csvzall charts run gym-attendance-heatmap
csvzall charts run --config .csvzall/charts.json
```

Potential later command:

```powershell
csvzall charts watch
```

`charts run` should:

- Load chart objects from JSON config.
- Resolve relative `input` and `output` paths against a chart config root.
  For the default `.csvzall/charts.json`, the config root is the directory that
  contains `.csvzall` (the vault/project root), not the `.csvzall` directory
  itself. For an explicit `--config <path>`, use the config file's parent
  directory unless that parent is named `.csvzall`; in that case use the parent
  of `.csvzall`. Test this rule directly.
- Validate each chart through the same path used by direct CLI commands.
- Render configured outputs.
- Produce clear diagnostics for missing input files, unknown chart types,
  invalid options, missing columns, and write failures.

Direct chart commands should preserve existing stdout behavior. If `--output`
is added or already exists for a chart command, it should be implemented as a
thin output sink around the same rendered artifact. Config-backed chart commands
must require an explicit output path for each chart object.

Default config discovery:

- `csvzall charts run` looks for `.csvzall/charts.json` from the current working
  directory.
- `csvzall charts run --config <path>` uses the supplied config path.
- Missing default config should fail clearly, not silently no-op.

## Obsidian Plugin Shape

The Obsidian plugin should stay thin.

- On CSV save, find chart specs whose `input` matches the saved file and whose
  `runOnSave` is true.
- Run the equivalent `csvzall charts run <id>` or direct binary operation.
- Show success/error status.
- Offer simple commands such as:
  - `CSVZall: Regenerate Charts`
  - `CSVZall: Regenerate Charts for Current CSV`
  - `CSVZall: Open Generated Chart`
- Do not implement chart rendering in TypeScript.
- Do not create live plugin-owned chart embeds.

## Obsidian Chart Automation Mechanics

Use Obsidian's vault modification events to trigger configured chart
regeneration.

Mechanism:

```text
Obsidian vault modify event
  -> if modified file is a configured CSV input
  -> debounce by input path
  -> run csvzall charts run <matching chart ids>
  -> csvzall writes generated SVG/Markdown artifacts directly
  -> Obsidian notices the updated artifact as an ordinary vault file
```

Implementation guidance:

- Register `this.app.vault.on("modify", ...)` through `this.registerEvent(...)`
  so the listener is cleaned up when the plugin unloads.
- Ignore non-CSV files for chart input matching.
- Reload `.csvzall/charts.json` when the config file itself changes.
- Find chart objects whose normalized `input` path matches the modified CSV and
  whose `runOnSave` value is true.
- Debounce per CSV path so clustered save/modify events do not spawn duplicate
  chart runs.
- Queue or lock per input path so a second save cannot overlap a still-running
  chart generation process for the same CSV.
- Ignore generated output paths for trigger purposes. Outputs should not cause
  recursive chart generation.
- Let `csvzall charts run` write configured chart outputs directly. Do not pipe
  SVG through the plugin unless direct writes prove unreliable.
- The plugin should surface concise success/error status, but should not own the
  rendering or artifact format.
- Because spawning `csvzall` uses desktop Node/Electron APIs, chart automation is
  desktop-only.

Sketch:

```ts
this.registerEvent(
  this.app.vault.on("modify", (file) => {
    if (!(file instanceof TFile)) return;
    if (file.extension !== "csv") return;

    this.scheduleChartsForCsv(file.path);
  })
);
```

Then run the binary from the vault root:

```ts
execFile(
  this.settings.csvzallPath,
  ["charts", "run", chart.id, "--config", ".csvzall/charts.json"],
  { cwd: vaultRoot },
  (error, stdout, stderr) => {
    // Show concise status/error in Obsidian.
  }
);
```

## Viewer UI Direction

Prepare the viewer chrome for charting without making it noisier.

- Remove permanent implementation reassurance such as `Local-only session` from
  the lower-left footer. Locality/security belongs in docs, CLI help, and logs,
  not always-visible viewer chrome.
- Remove row/column counts from beside the file name. The file name is the
  primary identity; counts are useful status metadata, not header identity.
- Move row/column counts to the lower-right footer, similar to line/column
  counters in editors such as Visual Studio Code.
- Keep the lower-left footer available for transient status messages such as:
  - `Loading...`
  - `Unsaved changes.`
  - `Saved 89 rows.`
  - error messages
- Reserve the header space next to the file name for an eventual `Add chart`
  button that opens a charting modal.
- Do not add the `Add chart` button until there is at least a minimal chart
  modal/config flow to wire it to.
- Consider shortening the mode label from `Client-side sort/filter` to something
  less visually chunky, such as `Client-side` or `Paged`.

## Chart Modal UI

Use generic validation, but custom-crafted forms.

The chart spec metadata should define valid options, defaults, CLI flags, JSON
keys, validators, and help text. It should not automatically render the entire
chart configuration modal.

Generic forms are useful for simple configuration editors, but chart creation is
a product workflow. A good heatmap dialog should be allowed to have chart-aware
layout, data-dependent defaults, column pickers, date controls, output path
suggestions, preview/open actions, and progressive disclosure that do not map
one-to-one to stored JSON fields.

Principle:

```text
Generic data model and validation; bespoke chart UI.
```

Implementation guidance:

- Build manual modals per chart type, starting with heatmap.
- Reuse small vanilla JS field helpers for common controls such as column
  pickers, date inputs, output paths, toggles, and enum selects.
- Let the modal emit a chart spec object.
- Run that object through the shared chart spec validator before saving config
  or rendering.
- Do not create a universal schema-to-dialog framework unless several chart
  types prove that the repeated UI code is worse than the abstraction.

## Chart Spec Architecture

Introduce an internal model along these lines:

```cpp
struct ChartSpec {
  std::string id;
  std::string type;
  std::filesystem::path input;
  std::optional<std::filesystem::path> output;
  bool run_on_save = false;
  HeatmapSpec heatmap;
};
```

The exact shape can differ, but the layering should be:

```text
CLI args    -> ChartSpec
JSON config -> ChartSpec
Obsidian    -> ChartSpec or chart id
             -> validate
             -> render
```

Prefer chart-type descriptors or shared option metadata so `--help`, JSON
validation, docs, and implementation do not become separate hand-maintained
truths.

## JSON Config Rules

- Use JSON for the MVP, not YAML.
- Keep config outside the CSV, preferably `.csvzall/charts.json`.
- Include a simple top-level version field only if it is immediately useful.
  Do not add migration machinery for the MVP.
- Paths should be relative unless absolute paths are explicitly needed.
- Unknown chart types should be rejected with a clear message.
- Unknown option keys should either be rejected or warned about. Prefer reject
  unless there is a concrete compatibility reason not to.
- Config should allow multiple charts for one CSV.
- Config should allow one chart input to produce one explicit output.
- Config-backed rendering should create parent output directories when practical
  and fail clearly when it cannot.
- Config-backed rendering and viewer-created chart configs should create the
  configured output file immediately if it does not already exist.
- Existing output files may be overwritten by configured chart generation. This
  is expected behavior for deterministic regenerated artifacts.

## Heatmap First

Start with `heatmap`, because it already proves the value:

```powershell
csvzall heatmap $gymCsvPath --start $rangeStartDate.ToString("yyyy-MM-dd") --end $rangeEndDate.ToString("yyyy-MM-dd") --date attendance_date --title "Gym Attendance" |
    Set-Content -LiteralPath $heatmapPath -Encoding utf8
```

The configured version should remove only the repetitive chart invocation, not
the surrounding user-owned data acquisition script.

## Non-Goals

- Do not build an Excel replacement.
- Do not add formulas.
- Do not make CSV files carry hidden chart metadata.
- Do not use CSV comments for csvzall chart instructions.
- Do not require the Obsidian plugin to render or own chart embeds.
- Do not build a generic arbitrary script hook as the primary chart automation
  feature.

## Tests

Add focused tests for:

- Direct heatmap CLI args parse into the same spec as equivalent JSON config.
- `charts run` loads `.csvzall/charts.json`.
- `charts run --config <path>` loads the supplied config.
- Config path resolution is deterministic.
- Config-backed output overwrites an existing generated artifact.
- Viewer `Add chart` save creates the configured chart output immediately.
- Config-backed output creates parent directories or fails clearly.
- Missing input file fails clearly.
- Missing date column fails clearly.
- Unknown chart type fails clearly.
- Unknown/invalid option fails clearly.
- Multiple chart objects render multiple outputs.
- Existing `heatmap` direct CLI behavior remains compatible.
- Obsidian automation debounces repeated modify events for the same CSV.
- Obsidian automation does not trigger on generated output paths.

## Verification

Use standalone CMake with the VS 2026 compiler environment.

Run:

```powershell
"C:\Program Files\CMake\bin\cmake.exe" -S . -B out\build\x64-Release-vs2026-ninja -G Ninja -DCSVZALL_CXX_STANDARD=23
"C:\Program Files\CMake\bin\cmake.exe" --build out\build\x64-Release-vs2026-ninja --target csvzall_tests csvzall
"C:\Program Files\CMake\bin\ctest.exe" --test-dir out\build\x64-Release-vs2026-ninja --output-on-failure
```

If configure/build paths need adjustment, keep using standalone CMake and the
VS 2026 compiler environment.

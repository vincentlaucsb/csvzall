# Chart Guidance

This file is the canonical AI reference for chart config, validation, and rendering code.

`src/charts/CLAUDE.md` must stay in sync with this file in the same commit.

## Scope

- Keep reusable CSV-to-chart config parsing, validation, schema documentation, and rendering helpers in `src/charts/`.
- Keep command-specific orchestration in `src/pipeline/commands/`; chart command code should adapt CLI/viewer requests to reusable chart APIs.
- Keep SVGPlot as the general-purpose chart drawing library. Do not move CLI or viewer assumptions into SVGPlot.

## Schema and Help

- Treat `chart_schema.*` as the source of truth for chart option documentation, valid values, examples, and drill-down reference text.
- Keep `csvzall charts --help` short enough to fit ordinary terminals. Put full option detail in `csvzall charts schema`.
- When adding or changing a chart option, update all relevant places in the same change: schema docs, config parsing, validation, renderer behavior, viewer request mapping when applicable, and focused tests.
- Help text and schema output should name valid values explicitly when the code accepts a finite set, for example `1y`, `stacked`, `grouped`, `sequential`, or `diverging`.

## Config Invariants

- Chart config files use a top-level `charts` array.
- Configured charts require explicit `output` paths. Relative input and output paths resolve against the config file root.
- Unknown chart types and unknown option keys should be reported as validation errors rather than silently ignored.
- `--validate` must check config shape, inputs, columns, dates, numeric values, and output paths without writing outputs.
- Keep generated chart configs deterministic and friendly to humans and LLMs: stable key names, explicit chart ids, and no unnecessary default noise.

## Option Invariants

- `orientation` is a heatmap layout option. It is separate from date range options in docs and UI.
- Heatmap range can be fixed dates (`start`/`end`) or rolling (`lookback`); do not combine `lookback` with `start` or `end`.
- `lookback` accepts a positive number plus days or years, such as `30d`, `90 days`, `1y`, or `2 years`.
- `presentation` is a bar chart option and accepts `stacked` or `grouped`.
- Multi-value `values` can be an array of column names or objects with fields such as `column`, `label`, and `color`.
- Explicit per-value colors always win over generated palettes.
- Multi-value bar and line charts support `colorScheme`: `sequential` or `diverging`. Default to `sequential`.
- Multi-value heatmaps use a diverging generated palette by default and do not expose `colorScheme`.
- Markdown table charts accept either `sql` for a custom SELECT or `columns` for a simple projection; omitting both exports all CSV columns.

## Viewer Invariants

- The chart modal should group layout controls separately from range controls.
- Show `colorScheme` only for multi-value bar and line charts.
- Keep viewer chart UI copy short enough for toolbar buttons and compact modal controls.

## Testing

- Cover chart option changes at the layer where behavior changes: parser/validation tests for accepted config, renderer tests for SVG output, CLI smoke tests for help/schema text, and view tests for viewer request mapping.
- If schema documentation is meant to prevent reanalysis of code, add or update tests that assert the important reference strings are present.

## Sync Requirement

`src/charts/AGENTS.md` is canonical. `src/charts/CLAUDE.md` is the mirror.
If one changes, update the other in the same commit.

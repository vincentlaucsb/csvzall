# Shared viewer shell

Native `csvzall view` and the WASM viewer use `src/viewer/modules/shell.mjs`,
`shell-markup.mjs`, and `src/viewer/viewer.css`. Change the toolbar, paged grid,
basic editing, dialogs, status, or responsive layout there. Neither entry point
implements a second editor. Both use the Alpine grid theme and host theme bridge.

## Composition

- Native `src/viewer/viewer.js` supplies the authenticated HTTP adapter and
  registers `sqlFeature` and `chartsFeature`.
- WASM `src/viewer_wasm/web/src/main.js` supplies the worker adapter, host bridge,
  local file opening, and PWA integration. Its feature list is empty. There are
  no SQL/chart imports, feature markup, or feature styles in that build.

Features are bundled first-party modules, not downloaded plugins. Each has an
`id` and `mount(shell)` method returning a `destroy()` method. Features mount
their own controls and stylesheet. They can add column actions and use the
shell's schema, grid factory, edit settlement, save/reset, status, and result
mode hooks. SQL retains a separate read-only result grid while the editor stays
mounted. Charts use the shared chart form builder and require save/discard
before opening against a dirty document.

## Adapter contract

An adapter exposes `capabilities`, `request(type, payload, transfer?)`,
`save({columns})`, `persist(result, context)`, and optional `destroy()`.
Requests cover `init`, `open`, `readRows`, `editCell`, row/column mutations, and
`reset`, plus `csvOptions` for output/header choices and `readCsvOptions` for
explicit input interpretation. Schemas include `csvOptions` with delimiter, quote,
hasHeaders, and BOM values alongside `columns`, `rowCount`, optional `file`, and `editable`.
Pages contain `offset`, `totalRows`, and arrays of cell values. The shell assigns
absolute source row identities independently of displayed row positions.

See [CSV options](csv-options.md) for dialect preservation, headerless editing,
explicit conversion and input overrides. The source bridge advertises
`csvzall-csv-options-v1` so downstream packaging can require these capabilities.

The native adapter translates operations into same-origin requests with the
session token. SQL/chart HTTP access belongs to its optional feature surface
(`fetchJson`/`postJson`), not the shell. Native saves atomically rewrite the source
and return the refreshed schema; column reordering no longer reloads the page.

The WASM adapter correlates worker replies, transfers input/output buffers, and
waits for a host write acknowledgment or initiates a browser download. It never
opens native endpoints. Engine startup, indexing, mutations, and rewriting stay
in the worker. Lite disables column dragging because the WASM binding does not
support persisting a reordered header; native retains that capability.

Mutation dispatch invalidates the save revision immediately. Saving settles the
active editor and outstanding writes before preparing bytes, and only clears the
saved revision after persistence succeeds. Host theme and Android keyboard
updates keep the existing grid/editor mounted and never resize the shell while
editing.

## Verification and downstream builds

`tests/viewer_adapters.test.mjs` covers authentication, paging identities,
structural schemas, column-order saves, worker failures, transferable buffers,
and acknowledged persistence. It is part of the viewer CTest suite. Native HTTP
tests cover nested feature assets and their MIME types. Existing viewer tests
cover edit settlement, SQL, chart forms, host themes, menus, and dialogs.

Build native assets through CMake, and run `npm test` and `npm run build` in
`src/viewer_wasm/web`. The standalone release keeps split worker/WASM assets;
`npm run build -- --mode embedded` makes the self-contained Obsidian build.
From the Obsidian source repository, use `npm run refresh:wasm-viewer`, then
`npm run sync:mobile-repo` to refresh the Lite distribution and demo vault.
Packaging only inlines the unchanged shared stylesheet; it adds no UI overrides.
Downstream checks require the shared-shell capability and reject native SQL/chart
UI in Lite's JavaScript.

For an optional browser smoke test, install Playwright in the web development
environment and start its Vite server with `npm run dev`. From the repository
root run:

```sh
node scripts/smoke_shared_viewer.cjs --native=<newly-built-csvzall>
```

The default browser is Playwright Chromium. `--browser=msedge` or
`--browser=chrome` uses an installed browser with an isolated temporary profile.
`--wasm-url=<url>` selects another development server. An existing external
Playwright installation can be supplied with `CSVZALL_PLAYWRIGHT_MODULE`.
The script tests both engines, native SQL over unsaved edits, chart mounting,
read-only menus, column-order persistence, and hosted mobile theme/editor/save
behavior. Fixtures and screenshots stay under `out/build/viewer-browser-smoke`;
`--output-dir=<directory>` changes that location.

Updating the desktop plugin's helper requires pinning a committed csvzall source
revision in its native build configuration and running its release preparation.
That pipeline builds or reuses verified helpers before the plugin is tagged;
it does not require a standalone csvzall release. Until the source changes are
committed and pinned, test them through a custom path to the local executable.
Lite receives the shared shell through the embedded viewer refresh.

## Compact layout

The shared shell uses compact mode at portrait widths up to 600 CSS pixels or
landscape heights up to 600 CSS pixels. Hosted compact views omit the duplicate
filename and shorten ordinary load/save status messages. Standalone views keep
their filename. Save remains visible; actions that do not fit move into the
More actions menu, retaining Insert/Delete submenus and current disabled states.
Overflow submenus open on click, tap, or keyboard activation. Hover must not
open a colliding child under the pointer and accidentally execute its action.

`modules/compact-layout.mjs` measures the toolbar without moving its controls or
recreating the grid. It defers changes while a cell editor or toolbar menu is
active. Real and mirrored AG Grid scroll ranges include 100 pixels plus the
bottom safe-area inset after the last row; datasource rows and saved CSV bytes
are unaffected. Keep this padding in sync for both scroll containers.

`tests/compact_layout.test.mjs` covers overflow and edit/menu stability. With a
local Vite server, `node scripts/smoke_compact_viewer.cjs` checks the real WASM
engine, portrait/landscape transitions, nested commands, 40-row saves and trailing
clearance. It accepts the same Playwright module and `--browser` option as the
shared smoke test. Add `--viewer-path=/dist-embedded/index.html` after an embedded
build to exercise the exact self-contained distribution.


## Chart previews

The native chart form places Preview immediately after Cancel and renders current,
unsaved options beside the form only on click. The authenticated
`POST /api/chart-config/preview` endpoint returns either SVG text (`svg`) or
Markdown (`markdown`). It shares chart specification construction with Save,
renders into a unique temporary directory, and removes that directory on success
or failure. It never writes charts.json or the configured output. Hosts can
provide a `previewChart(payload)` callback to the shared form instead of using
the HTTP endpoint. Edits invalidate visible and pending previews; closing the form
ignores late responses.

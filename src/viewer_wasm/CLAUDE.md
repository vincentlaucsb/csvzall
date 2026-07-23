# WASM Viewer Guidance (Mirror)

This file mirrors [AGENTS.md](AGENTS.md).

`src/viewer_wasm/AGENTS.md` is canonical. Keep both files in sync in the same commit.

## Scope

- `src/viewer_wasm/viewer_wasm.cpp` exposes the reusable indexed CSV viewer core to JavaScript through Emscripten embind.
- `src/viewer_wasm/web/` is the Vite web app and release bundle source for the browser/WASM viewer.
- Keep native `csvzall view` UI code in `src/viewer/`; do not move the native viewer to a frontend framework.

## Worker Boundary

- Run the Emscripten module in a module Web Worker, not on the main UI thread.
- Keep WASM startup, CSV indexing/open, row reads, edit mutations, reset, and save/download byte generation inside the worker.
- Keep AG Grid, file input, dialogs, status text, and browser DOM work on the main thread.
- Treat this boundary as a prerequisite for mobile and Obsidian plugin embedding; avoid adding new direct `CsvViewData` calls from `main.js`.

## CSV Viewer Core

- Reuse `src/viewer_core` for shared indexed CSV reading/editing behavior.
- Keep the WASM binding layer thin: adapt embind types and browser-friendly scalar types, then delegate to `csvzall::viewer`.
- The WASM viewer is intentionally scoped to plain local CSV viewing/editing. SQLite, plotting, gzip/zip input, and native save-in-place behavior belong outside this prototype unless deliberately designed.

## Web App

- The Vite app must build from a clean checkout after the Emscripten target has produced `out/build/wasm/csvzall_viewer_wasm.js` and `.wasm`.
- `src/viewer_wasm/web/scripts/copy-wasm.mjs` is the only expected copy step from the CMake WASM build into Vite source.
- Keep `vite.config.js` using a relative base path (`base: './'`) so the built app works when extracted under an Obsidian plugin asset directory or deployed to GitHub Pages.
- Do not commit generated `src/viewer_wasm/web/src/generated/`, `src/viewer_wasm/web/dist/`, or root `dist/` outputs.
- Keep the plain CSV editing surface aligned with native `csvzall view`: cell editing, insert/delete row, insert/rename/delete column, and row move actions should remain available through the same Popright menu patterns unless a browser limitation is documented.
- Use the vendored `vendor/popright` package through the Vite app's local file dependency instead of manually copying menu code into `src/viewer_wasm/web`.
- Keep the Obsidian iframe protocol implemented in source, not by patching built Vite output. The viewer accepts `{ source: "obsidian-csvzall", type: "open-file", name, buffer }`, queues early files until WASM initialization completes, and replies with `ready`, `dirty-state`, and `save-file` messages from source `"csvzall-wasm-viewer"`.
- `host-harness.html` is a source-only development harness for exercising iframe host mode; do not include it in the canonical release zip.

## Release Artifact

- The canonical release asset is staged by `scripts/package_wasm_viewer.mjs`.
- The release zip must be rooted at:

```text
index.html
manifest.webmanifest
sw.js
icon-192.png
icon-512.png
assets/
  *.js
  *.css
  *.wasm
csvzall-wasm-viewer.json
```

- `csvzall-wasm-viewer.json` must include source repository, commit, ref, version, build timestamp, build target, and source dist path.
- The web app is installable as a PWA when served over HTTPS. Keep the service worker build-generated so its precache list matches the hashed Vite assets, and keep browser file handling as a progressive enhancement.
- Release workflows must publish `csvzall-wasm-viewer-<version>.zip` and a SHA-256 checksum beside existing native release assets without changing native asset names or packaging.

## Theme Contract

- Keep the WASM app on the same `--csvzall-*` design tokens as the native viewer (`--csvzall-background-primary`, `--csvzall-background-secondary`, `--csvzall-background-hover`, `--csvzall-border`, `--csvzall-text-normal`, `--csvzall-text-muted`, `--csvzall-accent`, `--csvzall-font-family`).
- AG Grid must bridge through those tokens with `--ag-*` variables so the grid surface follows light/dark mode with the shell.
- Prefer AG Grid auto-dark theme classes, for example `ag-theme-quartz-auto-dark`, when using the npm AG Grid build.
- If token rules need to be shared more broadly, extract a small shared theme file deliberately and update both the native embedding path and WASM/Vite import path in the same change.

## Validation

- Run `node --check src/viewer_wasm/web/src/main.js` and `node --check src/viewer_wasm/web/src/csv-worker.js` after JavaScript changes.
- Run `npm test` in `src/viewer_wasm/web` after host bridge or protocol changes.
- Run `npm run build` in `src/viewer_wasm/web` after app, CSS, Worker, Vite, or release-artifact changes.
- For release packaging changes, run `npm run artifact` from `src/viewer_wasm/web` and inspect the staged metadata and relative `./assets/...` paths.

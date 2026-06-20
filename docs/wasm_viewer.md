# WASM CSV Viewer Prototype

The experimental WASM target exposes the reusable indexed CSV viewer core to a
browser through Emscripten embind. It is intentionally scoped to plain local CSV
viewing/editing:

- upload a CSV into Emscripten's virtual filesystem;
- open it with `CsvViewData.open(path)`;
- page rows into AG Grid's infinite row model;
- apply visible cell edits to the in-memory overlay;
- save the rewritten CSV in the virtual filesystem and download it.

The Vite demo runs the Emscripten module inside a module Web Worker. The main
thread owns AG Grid and the loading dialog, while the worker owns WASM startup,
CSV indexing, row reads, edit mutations, reset, and save/download bytes.

SQLite, plotting, gzip/zip input, and native save-in-place behavior are outside
this prototype.

## Build

Configure with Emscripten. The CMake build detects Emscripten and configures
only the experimental `csvzall_viewer_wasm` target, so the native CLI/server
dependencies are skipped.

```sh
emcmake cmake -G Ninja -S . -B out/build/wasm -DBUILD_TESTING=OFF
cmake --build out/build/wasm --target csvzall_viewer_wasm
```

On Windows, if Ninja is installed with Visual Studio but not on `PATH`, pass it
explicitly:

```powershell
emcmake 'C:\Program Files\CMake\bin\cmake.exe' -G Ninja -S . -B out\build\wasm `
  -DCMAKE_MAKE_PROGRAM='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' `
  -DBUILD_TESTING=OFF
cmake --build out\build\wasm --target csvzall_viewer_wasm
```

The target produces `csvzall_viewer_wasm.js` and a `.wasm` sidecar in the WASM
build directory.

## Web App

The browser demo is a small Vite app under `src/viewer_wasm/web`. Its build
script copies the generated Emscripten `.js` and `.wasm` outputs from
`out/build/wasm` into an ignored `src/generated` directory before bundling.

```sh
cd src/viewer_wasm/web
npm install
npm run build
```

For local iteration:

```sh
cd src/viewer_wasm/web
npm run dev
```

The Vite build uses a relative base path, so the generated `dist` directory can
be deployed directly to GitHub Pages. The web app uses npm AG Grid Community
instead of csvzall's native-viewer vendored AG Grid assets.

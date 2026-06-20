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

## Release Artifact

Build and stage the canonical release payload locally with:

```sh
emcmake cmake -G Ninja -S . -B out/build/wasm -DBUILD_TESTING=OFF
cmake --build out/build/wasm --target csvzall_viewer_wasm
cd src/viewer_wasm/web
npm install
npm run artifact
```

`npm run artifact` rebuilds the Vite app and stages:

```text
dist/csvzall-wasm-viewer/
  index.html
  assets/
    *.js
    *.css
    *.wasm
  csvzall-wasm-viewer.json
```

The metadata file records the source repository, commit, ref, version, build
timestamp, CMake target, and source dist path. To inspect the staged artifact:

```sh
node -e "console.log(require('fs').readdirSync('dist/csvzall-wasm-viewer'))"
cat dist/csvzall-wasm-viewer/csvzall-wasm-viewer.json
```

To create a zip and checksum locally from the staged directory:

```sh
version="$(node -e 'const fs=require("fs"); const m=fs.readFileSync("CMakeLists.txt","utf8").match(/project\\s*\\(\\s*csvzall\\s+VERSION\\s+([^\\s)]+)/); console.log(m[1])')"
(cd dist/csvzall-wasm-viewer && zip -r "../csvzall-wasm-viewer-${version}.zip" .)
sha256sum "dist/csvzall-wasm-viewer-${version}.zip" > "dist/csvzall-wasm-viewer-${version}.zip.sha256"
```

PowerShell:

```powershell
$version = node -e "const fs=require('fs'); const m=fs.readFileSync('CMakeLists.txt','utf8').match(/project\s*\(\s*csvzall\s+VERSION\s+([^\s)]+)/); console.log(m[1])"
$asset = "dist\csvzall-wasm-viewer-$version.zip"
Compress-Archive -Path "dist\csvzall-wasm-viewer\*" -DestinationPath $asset -Force
$hash = (Get-FileHash $asset -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path $asset -Leaf)" | Set-Content "$asset.sha256"
```

On GitHub releases, `.github/workflows/build-binaries.yml` uploads
`csvzall-wasm-viewer-<release-tag>.zip` and a matching `.sha256` file alongside
the existing native release assets. The zip is rooted at `index.html`, so it can
be extracted directly into an Obsidian plugin static asset directory.

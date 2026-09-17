import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, statSync, utimesSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cmake = process.env.CSVZALL_TEST_CMAKE || 'cmake';
const generator = process.env.CSVZALL_TEST_GENERATOR || 'Ninja';
const cmakePath = value => value.replaceAll('\\', '/');

// Uses the production target and generator, a real compiler, and a tiny asset set.
// Run from the compiler's developer environment (also required for normal builds).
test('embedded assets track content rather than archive timestamps', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'csvzall-assets-'));
  const source = path.join(root, 'source');
  const build = path.join(root, 'build');
  const run = (exe, args) => {
    const result = spawnSync(exe, args, { encoding: 'utf8' });
    assert.equal(result.status, 0, `${exe} ${args.join(' ')}\n${result.error || ''}\n${result.stdout}\n${result.stderr}`);
    return result.stdout;
  };
  try {
    mkdirSync(path.join(source, 'cmake'), { recursive: true });
    for (const name of ['viewer_assets.cmake', 'embed_viewer_assets.cmake']) {
      writeFileSync(path.join(source, 'cmake', name), readFileSync(path.join(repository, 'cmake', name)));
    }
    const script = readFileSync(path.join(source, 'cmake/embed_viewer_assets.cmake'), 'utf8');
    const roots = { VIEWER_SOURCE_DIR: 'src/viewer', AG_GRID_SOURCE_DIR: 'vendor/ag-grid', POPRIGHT_SOURCE_DIR: 'vendor/popright/dist' };
    for (const match of script.matchAll(/\|\$\{(VIEWER_SOURCE_DIR|AG_GRID_SOURCE_DIR|POPRIGHT_SOURCE_DIR)\}\/([^|]+)\|/g)) {
      const file = path.join(source, roots[match[1]], match[2]);
      mkdirSync(path.dirname(file), { recursive: true });
      writeFileSync(file, `initial ${match[2]}`);
    }
    writeFileSync(path.join(source, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.25)
project(asset_regression LANGUAGES CXX)
include(cmake/viewer_assets.cmake)
csvzall_add_viewer_assets(assets "\${CMAKE_CURRENT_SOURCE_DIR}" "\${CMAKE_CURRENT_BINARY_DIR}/generated")
add_executable(probe probe.cpp "\${CMAKE_CURRENT_BINARY_DIR}/generated/viewer_assets.cpp")
add_dependencies(probe assets)
target_include_directories(probe PRIVATE "\${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(probe PRIVATE cxx_std_17)
file(GENERATE OUTPUT "\${CMAKE_CURRENT_BINARY_DIR}/probe-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:probe>")
`);
    writeFileSync(path.join(source, 'probe.cpp'), `#include "viewer_assets.hpp"
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  const auto* asset = csvzall::pipeline::commands::FindEmbeddedViewerAsset(argv[1]);
  if (!asset) return 2;
  std::cout << csvzall::pipeline::commands::EmbeddedViewerAssetText(*asset);
}
`);
    run(cmake, ['-S', cmakePath(source), '-B', cmakePath(build), '-G', generator, '-DCMAKE_BUILD_TYPE=Release']);
    const rebuild = () => run(cmake, ['--build', build, '--config', 'Release']);
    rebuild();
    const exe = readFileSync(path.join(build, 'probe-Release.txt'), 'utf8');
    const cpp = path.join(build, 'generated/viewer_assets.cpp');
    const hpp = path.join(build, 'generated/viewer_assets.hpp');
    const snapshot = () => [cpp, hpp, exe].map(file => statSync(file).mtimeMs);
    const noOp = () => { const before = snapshot(); rebuild(); assert.deepEqual(snapshot(), before); };
    noOp();
    for (const [file, route] of [
      ['vendor/popright/dist/ContextMenu.js', '/assets/popright/ContextMenu.js'],
      ['src/viewer/viewer.js', '/assets/viewer.js'],
      ['vendor/ag-grid/ag-grid.css', '/assets/ag-grid.css'],
    ]) {
      const changed = path.join(source, file);
      const originalTime = statSync(changed).mtime;
      const headerTime = statSync(hpp).mtimeMs;
      const content = `changed ${file} @PRESERVE_THIS_LITERAL@`;
      writeFileSync(changed, content);
      utimesSync(changed, new Date(0), new Date(0));
      rebuild();
      assert.equal(run(exe, [route]), content);
      assert.equal(statSync(hpp).mtimeMs, headerTime);
      noOp();
      // A timestamp-only change must not regenerate or relink.
      utimesSync(changed, originalTime, originalTime);
      noOp();
    }
    // Generator/route changes invalidate the cache even with an old timestamp.
    const scriptPath = path.join(source, 'cmake/embed_viewer_assets.cmake');
    writeFileSync(scriptPath, script.replace('/assets/viewer.js|', '/assets/renamed.js|'));
    utimesSync(scriptPath, new Date(0), new Date(0));
    rebuild();
    assert.match(run(exe, ['/assets/renamed.js']), /^changed src\/viewer\/viewer.js/);
    noOp();
    // Clean or partially deleted generated files recover without touching inputs.
    rmSync(hpp);
    rebuild();
    assert.match(run(exe, ['/assets/renamed.js']), /^changed/);
    noOp();
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

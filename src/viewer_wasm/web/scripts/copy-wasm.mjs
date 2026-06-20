import { copyFile, mkdir } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(here, '..');
const repoRoot = resolve(webRoot, '../../..');
const buildDir = resolve(repoRoot, 'out/build/wasm');
const targetDir = resolve(webRoot, 'src/generated');

await mkdir(targetDir, { recursive: true });
await copyFile(
  resolve(buildDir, 'csvzall_viewer_wasm.js'),
  resolve(targetDir, 'csvzall_viewer_wasm.js'));
await copyFile(
  resolve(buildDir, 'csvzall_viewer_wasm.wasm'),
  resolve(targetDir, 'csvzall_viewer_wasm.wasm'));

console.log(`Copied WASM viewer artifacts from ${buildDir}`);

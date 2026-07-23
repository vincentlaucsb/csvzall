#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { cp, mkdir, readdir, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const webRootFiles = [
  'index.html',
  'manifest.webmanifest',
  'sw.js',
  'icon-192.png',
  'icon-512.png',
];

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (!arg.startsWith('--')) {
      throw new Error(`Unexpected argument: ${arg}`);
    }
    const key = arg.slice(2);
    const value = argv[i + 1];
    if (!value || value.startsWith('--')) {
      throw new Error(`Missing value for --${key}`);
    }
    args[key] = value;
    i += 1;
  }
  return args;
}

function git(args, fallback = '') {
  try {
    return execFileSync('git', args, {
      cwd: repoRoot,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore'],
    }).trim();
  } catch {
    return fallback;
  }
}

function firstNonEmpty(...values) {
  return values.find((value) => typeof value === 'string' && value.trim().length > 0) ?? '';
}

async function pathExists(path) {
  try {
    await stat(path);
    return true;
  } catch {
    return false;
  }
}

async function csvzallVersion() {
  const cmakeLists = await readFile(resolve(repoRoot, 'CMakeLists.txt'), 'utf8');
  const match = cmakeLists.match(/project\s*\(\s*csvzall\s+VERSION\s+([^\s)]+)/);
  if (!match) {
    throw new Error('Could not read csvzall version from CMakeLists.txt');
  }
  return match[1];
}

async function validateDist(distDir) {
  const assetsDir = resolve(distDir, 'assets');
  for (const file of webRootFiles) {
    const path = resolve(distDir, file);
    if (!(await pathExists(path))) {
      throw new Error(`Missing ${path}`);
    }
  }
  if (!(await pathExists(assetsDir))) {
    throw new Error(`Missing ${assetsDir}`);
  }

  const assetNames = await readdir(assetsDir);
  for (const extension of ['.js', '.css', '.wasm']) {
    if (!assetNames.some((name) => name.endsWith(extension))) {
      throw new Error(`Missing ${extension} asset in ${assetsDir}`);
    }
  }
}

const args = parseArgs(process.argv.slice(2));
const distDir = resolve(repoRoot, args['dist-dir'] ?? 'src/viewer_wasm/web/dist');
const outDir = resolve(repoRoot, args['out-dir'] ?? 'dist/csvzall-wasm-viewer');
const sourceCommit = args['source-commit'] ?? process.env.GITHUB_SHA ?? git(['rev-parse', 'HEAD']);
const sourceRef = firstNonEmpty(
  args['source-ref'],
  process.env.GITHUB_REF_NAME,
  git(['describe', '--tags', '--exact-match']),
  git(['branch', '--show-current']),
  sourceCommit);
const version = args.version ?? process.env.RELEASE_TAG ?? await csvzallVersion();
const builtAt = args['built-at'] ?? new Date().toISOString();

await validateDist(distDir);
await rm(outDir, { recursive: true, force: true });
await mkdir(outDir, { recursive: true });
for (const file of webRootFiles) {
  await cp(resolve(distDir, file), resolve(outDir, file));
}
await cp(resolve(distDir, 'assets'), resolve(outDir, 'assets'), { recursive: true });

const metadata = {
  sourceRepo: 'vincentlaucsb/csvzall',
  sourceCommit,
  sourceRef,
  version,
  builtAt,
  buildTarget: 'csvzall_viewer_wasm',
  sourcePath: 'src/viewer_wasm/web/dist',
};

await writeFile(
  resolve(outDir, 'csvzall-wasm-viewer.json'),
  `${JSON.stringify(metadata, null, 2)}\n`);

console.log(`Packaged WASM viewer staging directory: ${outDir}`);

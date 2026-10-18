#!/usr/bin/env node
// Checks or bumps every csvzall version marker in one place.
//
//   node scripts/version.mjs check            all markers agree
//   node scripts/version.mjs check --tag 0.6.1  ...and match a release tag
//   node scripts/version.mjs bump 0.6.1       rewrite every marker, then check
//
// When adding a new version-bearing file, add it to `markers` below.
import { readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

const markers = [
  { file: "CMakeLists.txt", pattern: /(project\(csvzall VERSION )([^\s)]+)/g, count: 1 },
  { file: "Doxyfile", pattern: /^(PROJECT_NUMBER\s*=\s*)(\S+)/gm, count: 1 },
  { file: "src/main.cpp", pattern: /(argparse::ArgumentParser program\("csvzall", ")([^"]+)/g, count: 1 },
];

const semver = /^\d+\.\d+\.\d+$/;

function normalizeTag(tag) {
  return tag.replace(/^refs\/tags\//, "").replace(/^v/, "");
}

function readMarkers() {
  const found = [];
  for (const marker of markers) {
    const text = readFileSync(path.join(root, marker.file), "utf8");
    const matches = [...text.matchAll(marker.pattern)];
    if (matches.length !== marker.count) {
      throw new Error(
        `${marker.file}: expected ${marker.count} version marker(s), found ${matches.length}`);
    }
    for (const match of matches) {
      found.push({ file: marker.file, version: match[2] });
    }
  }
  return found;
}

function check(tag) {
  const found = readMarkers();
  const versions = new Set(found.map((entry) => entry.version));
  const problems = [];
  if (versions.size !== 1) {
    problems.push(`version markers disagree (${[...versions].join(", ")})`);
  }
  const [version] = versions;
  if (versions.size === 1 && !semver.test(version)) {
    problems.push(`version ${version} is not X.Y.Z`);
  }
  if (tag !== undefined && versions.size === 1 && normalizeTag(tag) !== version) {
    problems.push(`release tag ${tag} does not match source version ${version}`);
  }

  for (const entry of found) {
    console.log(`${entry.version.padEnd(10)} ${entry.file}`);
  }
  if (problems.length > 0) {
    for (const problem of problems) {
      console.error(`error: ${problem}`);
    }
    process.exit(1);
  }
  console.log(`ok: csvzall ${version}`);
}

function bump(version) {
  if (!semver.test(version ?? "")) {
    console.error("usage: node scripts/version.mjs bump X.Y.Z");
    process.exit(2);
  }
  readMarkers(); // Fail before writing anything if a marker is missing.
  for (const marker of markers) {
    const file = path.join(root, marker.file);
    const text = readFileSync(file, "utf8");
    writeFileSync(file, text.replace(marker.pattern, (_, prefix) => `${prefix}${version}`));
  }
  check();
}

const [command, ...args] = process.argv.slice(2);
if (command === "check") {
  const tagIndex = args.indexOf("--tag");
  if (tagIndex >= 0 && !args[tagIndex + 1]) {
    console.error("error: --tag requires a value");
    process.exit(2);
  }
  check(tagIndex >= 0 ? args[tagIndex + 1] : undefined);
} else if (command === "bump") {
  bump(args[0]);
} else {
  console.error("usage: node scripts/version.mjs check [--tag <tag>] | bump X.Y.Z");
  process.exit(2);
}

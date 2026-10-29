import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const parent = join(root, "out", "obsidian-profile-tests");
mkdirSync(parent, { recursive: true });
const work = mkdtempSync(join(parent, "profile-"));
const cmake = process.env.CSVZALL_TEST_CMAKE || "cmake";
const compiler = process.env.CSVZALL_TEST_PREPROCESSOR || process.env.CXX || (process.platform === "win32" ? "cl" : "c++");
const msvc = /(?:^|[/\\])cl(?:\.exe)?$/i.test(compiler);
const definitions = profile => ["CSVZALL_HAVE_SVGPLOT=1", ...(profile === "obsidian"
  ? ["CSVZALL_OBSIDIAN_BUILD=1"] : ["CSVZALL_HAVE_POSTGRESQL=1"])];

function preprocess(file, profile) {
  // Exercise the real compiler's conditional processing without compiling or
  // needing dependency headers. These tests validate profile wiring, not types.
  const source = readFileSync(join(root, file), "utf8").replace(/^\s*#include[^\n]*$/gm, "");
  const input = join(work, `${profile}-${file.replace(/[/\\]/g, "-")}`);
  writeFileSync(input, source);
  const flags = msvc ? ["/nologo", "/EP", "/TP", "/utf-8", "/Zc:preprocessor", ...definitions(profile).map(flag => `/D${flag}`)]
    : ["-E", "-P", "-x", "c++", ...definitions(profile).map(flag => `-D${flag}`)];
  return execFileSync(compiler, [...flags, input], { encoding: "utf8", windowsHide: true, maxBuffer: 8 * 1024 * 1024 });
}

function cmakeProfile(profile) {
  const output = join(work, `${profile}-sources.txt`);
  const script = join(work, `${profile}.cmake`);
  writeFileSync(script, `set(CMAKE_CURRENT_SOURCE_DIR "${root.replace(/\\/g, "/")}")
set(CSVZALL_OBSIDIAN_BUILD ${profile === "obsidian" ? "ON" : "OFF"})
set(CSVZALL_HAVE_SVGPLOT ON)
include("\${CMAKE_CURRENT_SOURCE_DIR}/cmake/core_sources.cmake")
file(WRITE "${output.replace(/\\/g, "/")}" "\${CSVZALL_CORE_SOURCES}")
# Stub dependency operations: no configure, compiler probes, or downloads.
function(FetchContent_Declare name)
  set(DECLARED "\${name}" PARENT_SCOPE)
endfunction()
function(FetchContent_MakeAvailable name)
endfunction()
function(FetchContent_GetProperties name key variable)
  set(\${variable} "" PARENT_SCOPE)
endfunction()
include("\${CMAKE_CURRENT_SOURCE_DIR}/cmake/terminal_progress.cmake")
file(APPEND "${output.replace(/\\/g, "/")}" "\n\${CSVZALL_INDICATORS_TARGET}\n\${DECLARED}")
`);
  execFileSync(cmake, ["-P", script], { stdio: "pipe", windowsHide: true });
  return readFileSync(output, "utf8");
}

test("Obsidian preprocessing retains only viewer/chart/SQL commands and no terminal progress dependency", () => {
  const source = preprocess("src/main.cpp", "obsidian");
  const commands = [...source.matchAll(/argparse::ArgumentParser \w+\("([^"]+)"/g)].map(match => match[1]);
  assert.deepEqual(commands.sort(), ["charts", "csvzall", "sql", "view"]);
  for (const removed of ["RunDerive", "RunFilter", "RunSummarize", "RunTimeseries", "RunJsonExtract", "RunMax", "RunMin", "RunAppend", "RunMerge", "RunSqlExport", "CredentialManager", "indicators::", "csvzall::head::"]) {
    assert.ok(!source.includes(removed), `${removed} must not survive Obsidian preprocessing`);
  }
  assert.ok(!source.includes('add_argument("--dest")'));
  assert.ok(!source.includes('add_argument("--add-table")'));
  for (const required of ["RunView", "RunCharts", "RunSqlQueryCsv", "RunSqlQueryCsvTables", '"--obsidian"', '"--edit"', '"--validate"']) assert.ok(source.includes(required), required);
});

test("the pipeline forwarding object cannot retain removed command implementations", () => {
  const source = preprocess("src/transform_pipeline.cpp", "obsidian");
  const header = preprocess("src/transform_pipeline.hpp", "obsidian");
  for (const required of ["RunView", "RunCharts", "RunHeatmap", "RunSqlQueryCsv", "RunSqlQueryDb"]) assert.ok(source.includes(required), required);
  for (const removed of ["RunDerive", "RunFilter", "RunSummarize", "RunTimeseries", "RunJsonExtract", "RunMax", "RunMin", "RunAppend", "RunMerge", "RunSqlExport"]) { assert.ok(!source.includes(removed), removed); assert.ok(!header.includes(removed), removed); }
});

test("standalone preprocessing retains all commands, SQL loading, and progress UI", () => {
  const source = preprocess("src/main.cpp", "standalone");
  const commands = [...source.matchAll(/argparse::ArgumentParser \w+\("([^"]+)"/g)].map(match => match[1]);
  assert.deepEqual(commands.sort(), ["append", "calendar", "charts", "csvzall", "derive", "filter", "forget", "head", "heatmap", "infer", "json", "max", "merge", "min", "postgres", "sql", "summarize", "timeseries", "view"]);
  for (const retained of ["RunSqlExport", 'add_argument("--dest")', "indicators::ProgressBar", "CredentialManager"]) assert.ok(source.includes(retained), retained);
});

test("CMake excludes twelve unused objects and indicators only for the Obsidian profile", () => {
  const helper = cmakeProfile("obsidian"), standalone = cmakeProfile("standalone");
  for (const path of ["credentials.cpp", "src/util.cpp", "common/json_path.cpp", "common/markdown_calendar.cpp", "commands/append.cpp", "commands/derive.cpp", "commands/filter.cpp", "commands/json_extract.cpp", "commands/max.cpp", "commands/sql_export.cpp", "commands/summarize.cpp", "commands/timeseries.cpp"]) {
    assert.ok(!helper.includes(path), path);
    assert.ok(standalone.includes(path), path);
  }
  for (const path of ["commands/view.cpp", "commands/sql_query.cpp", "commands/chart.cpp", "commands/heatmap.cpp", "commands/bar.cpp", "commands/line.cpp", "commands/CsvTransformCommand.cpp", "charts/chart_spec.cpp", "sqlite/sql_functions.cpp"]) assert.ok(helper.includes(path), path);
  assert.ok(!helper.includes("indicators"));
  assert.ok(standalone.includes("indicators"));
  const cmakeSource = readFileSync(join(root, "CMakeLists.txt"), "utf8");
  assert.match(cmakeSource, /target_compile_definitions\(\$\{target_name\} PUBLIC CSVZALL_OBSIDIAN_BUILD=1\)/);
});

#include "view.hpp"

#include "../../util.hpp"
#include "../common/gzip_stream.hpp"

#include "commands.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "../../../vendor/httplib/httplib.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace csvzall::pipeline::commands {

namespace {

constexpr std::uint64_t kDefaultRowsPerPage = 500;
constexpr std::uint64_t kMaxRowsPerPage = 5000;
constexpr std::uint64_t kBytesPerMiB = 1024 * 1024;

constexpr std::string_view kViewerHtml = R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>csvzall view</title>
  <link rel="stylesheet" href="/assets/ag-grid.css">
  <link rel="stylesheet" href="/assets/ag-theme-alpine.css">
  <link rel="stylesheet" href="/assets/viewer.css">
</head>
<body>
  <div class="app-shell">
    <header class="topbar">
      <div class="file-meta">
        <h1 id="file-name">Loading…</h1>
        <span id="summary" class="summary">Preparing read-only table view.</span>
      </div>
      <label class="search">
        <span id="mode-label">Loading rows</span>
        <input id="quick-filter" type="search" placeholder="Filter loaded table">
      </label>
      <div id="edit-toolbar" class="edit-toolbar" hidden>
        <button id="insert-row" type="button">Insert row</button>
        <button id="delete-row" type="button">Delete row</button>
        <button id="save" type="button" disabled>Save</button>
      </div>
    </header>
    <main class="grid-wrap">
      <div id="grid" class="ag-theme-alpine-auto-dark"></div>
    </main>
    <footer class="footer">
      <span>Local-only session.</span>
      <span id="status">Connecting…</span>
    </footer>
  </div>
  <script src="/assets/ag-grid-community.min.js"></script>
  <script src="/assets/viewer.js"></script>
</body>
</html>
)";

constexpr std::string_view kViewerCss = R"(html, body {
  height: 100%;
  margin: 0;
  font-family: var(--csvzall-font-family);
  background: var(--csvzall-background-primary);
  color: var(--csvzall-text-normal);
}

* {
  box-sizing: border-box;
  font-family: inherit;
}

:root {
  color-scheme: light;
  --csvzall-font-family: var(--font-interface, ui-sans-serif), system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  --csvzall-background-primary: var(--background-primary, #ffffff);
  --csvzall-background-secondary: var(--background-secondary, #f6f7f8);
  --csvzall-background-hover: var(--background-modifier-hover, #f2f3f5);
  --csvzall-border: var(--background-modifier-border, #d6d6d6);
  --csvzall-text-normal: var(--text-normal, #2e3338);
  --csvzall-text-muted: var(--text-muted, #6f7680);
  --csvzall-accent: var(--interactive-accent, #7c3aed);
}

@media (prefers-color-scheme: dark) {
  :root {
    color-scheme: dark;
    --csvzall-background-primary: var(--background-primary, #111317);
    --csvzall-background-secondary: var(--background-secondary, #191c22);
    --csvzall-background-hover: var(--background-modifier-hover, #252a33);
    --csvzall-border: var(--background-modifier-border, #343a46);
    --csvzall-text-normal: var(--text-normal, #e6e9ef);
    --csvzall-text-muted: var(--text-muted, #a6adbb);
    --csvzall-accent: var(--interactive-accent, #9b8cff);
  }
}

.app-shell {
  height: 100%;
  display: grid;
  grid-template-rows: auto 1fr auto;
  background: var(--csvzall-background-primary);
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 0.75rem;
  min-height: 48px;
  padding: 0.5rem 0.75rem;
  border-bottom: 1px solid var(--csvzall-border);
  background: var(--csvzall-background-primary);
}

.file-meta {
  min-width: 0;
  display: flex;
  align-items: baseline;
  gap: 0.75rem;
}

.topbar h1 {
  margin: 0;
  overflow: hidden;
  color: var(--csvzall-text-normal);
  font-size: 0.95rem;
  font-weight: 600;
  line-height: 1.4;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.summary, .footer {
  color: var(--csvzall-text-muted);
  font-size: 0.78rem;
}

.search {
  display: flex;
  align-items: center;
  gap: 0.45rem;
  flex: 0 1 20rem;
  color: var(--csvzall-text-muted);
  font-size: 0.78rem;
}

.search input {
  width: 100%;
  height: 28px;
  padding: 0 0.55rem;
  border: 1px solid var(--csvzall-border);
  border-radius: var(--radius-s, 4px);
  background: var(--csvzall-background-primary);
  color: var(--csvzall-text-normal);
  font: inherit;
}

.search input:focus {
  border-color: var(--csvzall-accent);
  box-shadow: 0 0 0 2px color-mix(in srgb, var(--csvzall-accent) 18%, transparent);
  outline: none;
}

.edit-toolbar {
  display: flex;
  align-items: center;
  gap: 0.35rem;
}

.edit-toolbar button {
  height: 28px;
  padding: 0 0.6rem;
  border: 1px solid var(--csvzall-border);
  border-radius: var(--radius-s, 4px);
  background: var(--csvzall-background-secondary);
  color: var(--csvzall-text-normal);
  font: inherit;
}

.edit-toolbar button:disabled {
  opacity: 0.55;
}

.grid-wrap {
  min-height: 0;
  padding: 0;
}

#grid {
  height: 100%;
  width: 100%;
  min-height: 12rem;
  border: 0;
  overflow: hidden;
}

.ag-theme-alpine,
.ag-theme-alpine-auto-dark {
  --ag-font-family: var(--csvzall-font-family);
  --ag-font-size: 13px;
  --ag-background-color: var(--csvzall-background-primary);
  --ag-foreground-color: var(--csvzall-text-normal);
  --ag-data-color: var(--csvzall-text-normal);
  --ag-header-foreground-color: var(--csvzall-text-normal);
  --ag-secondary-foreground-color: var(--csvzall-text-muted);
  --ag-header-background-color: var(--csvzall-background-secondary);
  --ag-odd-row-background-color: var(--csvzall-background-primary);
  --ag-row-hover-color: var(--csvzall-background-hover);
  --ag-selected-row-background-color: color-mix(in srgb, var(--csvzall-accent) 14%, transparent);
  --ag-border-color: var(--csvzall-border);
  --ag-row-border-color: var(--csvzall-border);
  --ag-header-column-separator-color: var(--csvzall-border);
  --ag-input-border-color: var(--csvzall-border);
  --ag-input-focus-border-color: var(--csvzall-accent);
  --ag-control-panel-background-color: var(--csvzall-background-secondary);
  --ag-menu-background-color: var(--csvzall-background-primary);
  --ag-modal-overlay-background-color: color-mix(in srgb, var(--csvzall-background-primary) 70%, transparent);
}

.ag-theme-alpine .ag-root-wrapper,
.ag-theme-alpine .ag-header,
.ag-theme-alpine .ag-row,
.ag-theme-alpine .ag-center-cols-viewport,
.ag-theme-alpine .ag-center-cols-container,
.ag-theme-alpine-auto-dark .ag-root-wrapper,
.ag-theme-alpine-auto-dark .ag-header,
.ag-theme-alpine-auto-dark .ag-row,
.ag-theme-alpine-auto-dark .ag-center-cols-viewport,
.ag-theme-alpine-auto-dark .ag-center-cols-container {
  background-color: var(--ag-background-color);
  color: var(--ag-foreground-color);
  font-family: var(--ag-font-family);
}

.ag-theme-alpine .ag-header,
.ag-theme-alpine-auto-dark .ag-header {
  background-color: var(--ag-header-background-color);
}

.footer {
  display: flex;
  justify-content: space-between;
  gap: 1rem;
  min-height: 30px;
  padding: 0.4rem 0.75rem;
  border-top: 1px solid var(--csvzall-border);
  background: var(--csvzall-background-secondary);
}

@media (max-width: 820px) {
  .topbar, .file-meta, .footer {
    align-items: stretch;
    flex-direction: column;
  }

  .summary {
    display: block;
  }

  .search {
    flex-basis: auto;
  }
}
)";

constexpr std::string_view kViewerJs = R"(async function csvzallViewBootstrap() {
  const statusNode = document.getElementById('status');
  const summaryNode = document.getElementById('summary');
  const fileNode = document.getElementById('file-name');
  const modeNode = document.getElementById('mode-label');
  const quickFilterNode = document.getElementById('quick-filter');
  const token = new URLSearchParams(window.location.search).get('token');

  if (!token) {
    statusNode.textContent = 'Missing session token.';
    summaryNode.textContent = 'Open the viewer from the csvzall command output.';
    return;
  }

  const headers = { 'X-Session-Token': token };

  async function fetchJson(path, params = {}) {
    const url = new URL(path, window.location.origin);
    for (const [key, value] of Object.entries(params)) {
      url.searchParams.set(key, String(value));
    }
    const response = await fetch(url, { headers });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
  }

  async function postJson(path, body = {}) {
    const response = await fetch(new URL(path, window.location.origin), {
      method: 'POST',
      headers: { ...headers, 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const text = await response.text();
    if (!response.ok) {
      throw new Error(text.trim() || `HTTP ${response.status}`);
    }
    return text ? JSON.parse(text) : {};
  }

  function setDatasource(api, datasource) {
    if (api.setGridOption) {
      api.setGridOption('datasource', datasource);
      return;
    }
    api.setDatasource(datasource);
  }

  function setQuickFilter(api, value) {
    if (api.setGridOption) {
      api.setGridOption('quickFilterText', value);
      return;
    }
    api.setQuickFilter(value);
  }

  function rowsToObjects(columns, rows) {
    return rows.map((values, index) => {
      const row = { _csvzallRowId: index };
      columns.forEach((column, index) => {
        row[column] = values[index] ?? '';
      });
      return row;
    });
  }

  function renumberRows(rows) {
    rows.forEach((row, index) => {
      row._csvzallRowId = index;
    });
  }

  try {
    const schema = await fetchJson('/api/schema');
    fileNode.textContent = schema.file;
    summaryNode.textContent = `${schema.totalRows.toLocaleString()} rows across ${schema.columns.length.toLocaleString()} columns.`;
    const materialized = schema.mode === 'materialized';
    const editable = schema.editable === true;
    const editToolbar = document.getElementById('edit-toolbar');
    const insertButton = document.getElementById('insert-row');
    const deleteButton = document.getElementById('delete-row');
    const saveButton = document.getElementById('save');
    modeNode.textContent = materialized ? 'Client-side sort/filter' : 'Server-paged rows';
    quickFilterNode.hidden = !materialized;
    quickFilterNode.disabled = !materialized;
    editToolbar.hidden = !editable;

    const columnDefs = schema.columns.map((column) => ({
      headerName: column,
      field: column,
      sortable: materialized,
      filter: materialized,
      editable,
      resizable: true,
      minWidth: 140,
      flex: 1,
    }));

    const gridOptions = {
      columnDefs,
      defaultColDef: {
        sortable: materialized,
        filter: materialized,
        editable,
        resizable: true,
      },
      animateRows: false,
      suppressColumnVirtualisation: false,
      rowSelection: 'single',
    };

    if (!materialized) {
      gridOptions.rowModelType = 'infinite';
      gridOptions.cacheBlockSize = 500;
      gridOptions.maxBlocksInCache = 8;
      gridOptions.blockLoadDebounceMillis = 40;
    }

    const gridElement = document.getElementById('grid');
    const api = agGrid.createGrid
      ? agGrid.createGrid(gridElement, gridOptions)
      : (() => {
          new agGrid.Grid(gridElement, gridOptions);
          return gridOptions.api;
        })();

    if (materialized) {
      statusNode.textContent = `Loading ${schema.totalRows.toLocaleString()} rows for client-side sort/filter…`;
      let dirty = false;
      const allRows = schema.totalRows === 0
        ? []
        : rowsToObjects(schema.columns, (await fetchJson('/api/rows', { offset: 0, limit: schema.totalRows })).rows);
      const setDirty = (value) => {
        dirty = value;
        saveButton.disabled = !dirty;
        statusNode.textContent = dirty
          ? 'Unsaved changes.'
          : `Loaded ${allRows.length.toLocaleString()} rows for ${editable ? 'editing' : 'client-side sort/filter'}.`;
      };
      if (api.setGridOption) {
        api.setGridOption('rowData', allRows);
      } else {
        api.setRowData(allRows);
      }
      quickFilterNode.addEventListener('input', () => setQuickFilter(api, quickFilterNode.value));
      if (editable) {
        const refreshRows = () => {
          if (api.setGridOption) {
            api.setGridOption('rowData', allRows);
          } else {
            api.setRowData(allRows);
          }
        };
        const selectedSourceRow = () => {
          const selected = api.getSelectedRows ? api.getSelectedRows() : [];
          return selected.length > 0 ? selected[0]._csvzallRowId : allRows.length;
        };
        gridOptions.onCellValueChanged = async (event) => {
          if (!event.colDef.field || event.colDef.field === '_csvzallRowId') {
            return;
          }
          try {
            await postJson('/api/edit-cell', {
              row: event.data._csvzallRowId,
              column: event.colDef.field,
              value: event.newValue ?? '',
            });
            setDirty(true);
          } catch (error) {
            event.node.setDataValue(event.colDef.field, event.oldValue ?? '');
            statusNode.textContent = error instanceof Error ? error.message : 'Edit failed';
          }
        };
        if (api.setGridOption) {
          api.setGridOption('onCellValueChanged', gridOptions.onCellValueChanged);
        }
        deleteButton.addEventListener('click', async () => {
          const row = selectedSourceRow();
          if (row >= allRows.length) {
            statusNode.textContent = 'Select a row to delete.';
            return;
          }
          try {
            await postJson('/api/delete-row', { row });
            allRows.splice(row, 1);
            renumberRows(allRows);
            refreshRows();
            setDirty(true);
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Delete failed';
          }
        });
        insertButton.addEventListener('click', async () => {
          const row = selectedSourceRow();
          const values = schema.columns.map(() => '');
          try {
            await postJson('/api/insert-row', { row, values });
            const inserted = { _csvzallRowId: row };
            schema.columns.forEach((column) => {
              inserted[column] = '';
            });
            allRows.splice(row, 0, inserted);
            renumberRows(allRows);
            refreshRows();
            setDirty(true);
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Insert failed';
          }
        });
        saveButton.addEventListener('click', async () => {
          try {
            saveButton.disabled = true;
            statusNode.textContent = 'Saving…';
            await postJson('/api/save');
            setDirty(false);
            statusNode.textContent = `Saved ${allRows.length.toLocaleString()} rows.`;
          } catch (error) {
            saveButton.disabled = !dirty;
            statusNode.textContent = error instanceof Error ? error.message : 'Save failed';
          }
        });
      }
      setDirty(false);
    } else {
      setDatasource(api, {
        async getRows(params) {
          const offset = params.startRow;
          const limit = Math.max(params.endRow - params.startRow, 1);
          statusNode.textContent = `Loading rows ${offset + 1}-${Math.min(params.endRow, schema.totalRows)} of ${schema.totalRows}…`;
          try {
            const page = await fetchJson('/api/rows', { offset, limit });
            const rowData = rowsToObjects(schema.columns, page.rows);
            const loadedThrough = page.offset + page.rows.length;
            const lastRow = loadedThrough >= page.totalRows ? page.totalRows : undefined;
            params.successCallback(rowData, lastRow);
            statusNode.textContent = `Loaded rows ${page.offset + 1}-${loadedThrough} of ${page.totalRows}.`;
          } catch (error) {
            params.failCallback();
            statusNode.textContent = error instanceof Error ? error.message : 'Row load failed';
          }
        },
      });
      statusNode.textContent = `Ready: ${schema.totalRows.toLocaleString()} rows indexed.`;
    }

    if (api.sizeColumnsToFit) {
      api.sizeColumnsToFit();
    }
  } catch (error) {
    summaryNode.textContent = 'The viewer could not load table data.';
    statusNode.textContent = error instanceof Error ? error.message : 'Unknown error';
  }
}

csvzallViewBootstrap();
)";

csv::CSVFormat MakeViewFormat(const RunOptions& options) {
  csv::CSVFormat format;
  if (options.delimiter) {
    format.delimiter(*options.delimiter);
  } else {
    format.delimiter({',', '|', '\t', ';', '^'});
  }
  format.quote('"').header_row(0);
  return format;
}

void AppendJsonString(std::string& output, std::string_view value) {
  output.push_back('"');
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (ch < 0x20) {
          static constexpr char hex[] = "0123456789abcdef";
          output += "\\u00";
          output.push_back(hex[(ch >> 4) & 0x0f]);
          output.push_back(hex[ch & 0x0f]);
        } else {
          output.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  output.push_back('"');
}

std::string BuildSchemaJson(const CsvViewData& data, bool editable) {
  std::string json;
  json.reserve(256 + data.headers().size() * 32);
  json += "{\"file\":";
  AppendJsonString(json, data.file_name());
  json += ",\"columns\":[";
  for (std::size_t i = 0; i < data.headers().size(); ++i) {
    if (i > 0) {
      json.push_back(',');
    }
    AppendJsonString(json, data.headers()[i]);
  }
  json += "],\"readOnly\":";
  json += editable ? "false" : "true";
  json += ",\"editable\":";
  json += editable ? "true" : "false";
  json += ",\"mode\":";
  AppendJsonString(json, data.mode_name());
  json += ",\"totalRows\":";
  json += std::to_string(data.row_count());
  json += "}";
  return json;
}

std::string BuildRowsJson(const CsvViewData& data,
                          std::uint64_t offset,
                          std::uint64_t limit,
                          const std::vector<std::vector<std::string>>& rows) {
  std::string json;
  json += "{\"offset\":";
  json += std::to_string(offset);
  json += ",\"limit\":";
  json += std::to_string(limit);
  json += ",\"totalRows\":";
  json += std::to_string(data.row_count());
  json += ",\"rows\":[";

  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    if (row_index > 0) {
      json.push_back(',');
    }
    json.push_back('[');
    const auto& row = rows[row_index];
    for (std::size_t col_index = 0; col_index < data.headers().size(); ++col_index) {
      if (col_index > 0) {
        json.push_back(',');
      }
      if (col_index < row.size()) {
        AppendJsonString(json, row[col_index]);
      } else {
        AppendJsonString(json, "");
      }
    }
    json.push_back(']');
  }

  json += "]}";
  return json;
}

std::string BuildHealthJson() {
  return "{\"status\":\"ok\",\"readOnly\":true}";
}

std::string BuildStartupJson(const std::string& url) {
  std::string json = "{\"url\":";
  AppendJsonString(json, url);
  json += "}";
  return json;
}

std::string GenerateSessionToken() {
  std::random_device rd;
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  static constexpr char hex[] = "0123456789abcdef";

  std::string token;
  token.reserve(32);
  for (int i = 0; i < 16; ++i) {
    const auto byte = dist(rd);
    token.push_back(hex[(byte >> 4) & 0x0f]);
    token.push_back(hex[byte & 0x0f]);
  }
  return token;
}

bool OpenBrowserUrl(const std::string& url) {
#ifdef _WIN32
  const auto rc = reinterpret_cast<std::uintptr_t>(
      ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return rc > 32;
#elif defined(__APPLE__)
  const auto command = "open \"" + url + "\"";
  return std::system(command.c_str()) == 0;
#else
  const auto command = "xdg-open \"" + url + "\" >/dev/null 2>&1";
  return std::system(command.c_str()) == 0;
#endif
}

std::string ReadFileText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open asset file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::filesystem::path ResolveVendorAssetRoot() {
  std::vector<std::filesystem::path> candidates;
  try {
    candidates.push_back(util::executable_path().parent_path() / "assets" / "viewer" / "vendor");
  } catch (const std::exception&) {
  }
  candidates.emplace_back(std::filesystem::path(CSVZALL_SOURCE_DIR) / "vendor" / "ag-grid");

  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate / "ag-grid-community.min.js") &&
        std::filesystem::exists(candidate / "ag-grid.css") &&
        std::filesystem::exists(candidate / "ag-theme-alpine.css")) {
      return candidate;
    }
  }

  throw std::runtime_error(
      "viewer assets were not found next to the executable or in the source tree");
}

bool ParseUint64Param(const httplib::Request& request,
                      std::string_view name,
                      std::uint64_t default_value,
                      std::uint64_t& output) {
  const auto raw = request.get_param_value(std::string(name));
  if (raw.empty()) {
    output = default_value;
    return true;
  }
  if (!std::all_of(raw.begin(), raw.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    return false;
  }
  try {
    output = static_cast<std::uint64_t>(std::stoull(raw));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void BadRequest(httplib::Response& response, std::string_view message) {
  response.status = 400;
  response.set_content(std::string(message) + "\n", "text/plain; charset=utf-8");
}

void SkipJsonWs(std::string_view text, std::size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
    ++pos;
  }
}

std::string ParseJsonString(std::string_view text, std::size_t& pos) {
  SkipJsonWs(text, pos);
  if (pos >= text.size() || text[pos] != '"') {
    throw std::runtime_error("expected JSON string");
  }
  ++pos;
  std::string value;
  while (pos < text.size()) {
    const char ch = text[pos++];
    if (ch == '"') {
      return value;
    }
    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }
    if (pos >= text.size()) {
      throw std::runtime_error("unterminated JSON escape");
    }
    const char escaped = text[pos++];
    switch (escaped) {
      case '"': value.push_back('"'); break;
      case '\\': value.push_back('\\'); break;
      case '/': value.push_back('/'); break;
      case 'b': value.push_back('\b'); break;
      case 'f': value.push_back('\f'); break;
      case 'n': value.push_back('\n'); break;
      case 'r': value.push_back('\r'); break;
      case 't': value.push_back('\t'); break;
      default:
        throw std::runtime_error("unsupported JSON escape");
    }
  }
  throw std::runtime_error("unterminated JSON string");
}

void SkipJsonValue(std::string_view text, std::size_t& pos) {
  SkipJsonWs(text, pos);
  if (pos >= text.size()) {
    throw std::runtime_error("expected JSON value");
  }
  if (text[pos] == '"') {
    (void)ParseJsonString(text, pos);
    return;
  }
  if (text[pos] == '{' || text[pos] == '[') {
    const char open = text[pos++];
    const char close = open == '{' ? '}' : ']';
    int depth = 1;
    while (pos < text.size() && depth > 0) {
      if (text[pos] == '"') {
        (void)ParseJsonString(text, pos);
      } else if (text[pos] == open) {
        ++depth;
        ++pos;
      } else if (text[pos] == close) {
        --depth;
        ++pos;
      } else {
        ++pos;
      }
    }
    if (depth != 0) {
      throw std::runtime_error("unterminated JSON value");
    }
    return;
  }
  while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']') {
    ++pos;
  }
}

template <typename Parser>
auto ParseJsonField(std::string_view body, std::string_view field, Parser parser) {
  std::size_t pos = 0;
  SkipJsonWs(body, pos);
  if (pos >= body.size() || body[pos++] != '{') {
    throw std::runtime_error("expected JSON object");
  }
  while (true) {
    SkipJsonWs(body, pos);
    if (pos < body.size() && body[pos] == '}') {
      break;
    }
    const auto key = ParseJsonString(body, pos);
    SkipJsonWs(body, pos);
    if (pos >= body.size() || body[pos++] != ':') {
      throw std::runtime_error("expected JSON object separator");
    }
    if (key == field) {
      return parser(body, pos);
    }
    SkipJsonValue(body, pos);
    SkipJsonWs(body, pos);
    if (pos < body.size() && body[pos] == ',') {
      ++pos;
      continue;
    }
    if (pos < body.size() && body[pos] == '}') {
      break;
    }
  }
  throw std::runtime_error("missing JSON field: " + std::string(field));
}

std::uint64_t JsonUintField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    SkipJsonWs(text, pos);
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
      throw std::runtime_error("expected JSON integer");
    }
    std::uint64_t value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      value = value * 10 + static_cast<std::uint64_t>(text[pos++] - '0');
    }
    return value;
  });
}

std::string JsonStringField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    return ParseJsonString(text, pos);
  });
}

std::vector<std::string> JsonStringArrayField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    SkipJsonWs(text, pos);
    if (pos >= text.size() || text[pos++] != '[') {
      throw std::runtime_error("expected JSON array");
    }
    std::vector<std::string> values;
    while (true) {
      SkipJsonWs(text, pos);
      if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return values;
      }
      values.push_back(ParseJsonString(text, pos));
      SkipJsonWs(text, pos);
      if (pos < text.size() && text[pos] == ',') {
        ++pos;
        continue;
      }
      if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return values;
      }
      throw std::runtime_error("expected JSON array separator");
    }
  });
}

void ValidatePlainLocalViewInput(const std::string& input_path, const RunOptions& options) {
  if (input_path.empty() || input_path == "-") {
    throw std::runtime_error("stdin is not supported; pass a plain local CSV file path");
  }
  if (!options.zip_entry.empty() || common::IsZipPath(input_path) || common::IsGzipPath(input_path)) {
    throw std::runtime_error("view currently supports plain local CSV files only");
  }
}

std::uint64_t GetFileSize(const std::string& input_path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(input_path, ec);
  if (ec) {
    throw std::runtime_error("unable to stat input file: " + input_path);
  }
  return static_cast<std::uint64_t>(size);
}

std::filesystem::file_time_type GetFileMtime(const std::string& input_path) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(input_path, ec);
  if (ec) {
    throw std::runtime_error("unable to stat input file mtime: " + input_path);
  }
  return mtime;
}

std::uint64_t ThresholdBytes(std::size_t threshold_mb) {
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  if (threshold_mb > max / kBytesPerMiB) {
    return max;
  }
  return static_cast<std::uint64_t>(threshold_mb) * kBytesPerMiB;
}

CsvMaterializedFile OpenMaterializedFile(const std::string& input_path,
                                         const RunOptions& options,
                                         const LoggerCallbacks& logger,
                                         RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);

  CsvMaterializedFile materialized;
  materialized.input_path = input_path;
  materialized.file_name = std::filesystem::path(input_path).filename().string();
  const auto file_size = GetFileSize(input_path);
  materialized.source_size = file_size;
  materialized.source_mtime = GetFileMtime(input_path);

  auto format = MakeViewFormat(options);
  csv::CSVReader reader(input_path, format);
  materialized.frame = std::make_shared<csv::DataFrame<>>(reader);
  materialized.headers = materialized.frame->columns();
  if (materialized.headers.empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  for (const auto& row : *materialized.frame) {
    materialized.rows.emplace_back(std::vector<std::string>(row));
  }

  stats.rows_processed = static_cast<std::uint64_t>(materialized.rows.size());
  stats.bytes_processed = file_size;
  if (logger.verbose) {
    logger.verbose("view: materialized " + std::to_string(materialized.rows.size()) +
                   " row(s) from " + input_path);
  }
  return materialized;
}

}  // namespace

CsvIndexedFile CsvIndexedFile::Open(const std::string& input_path,
                                    const RunOptions& options,
                                    const LoggerCallbacks& logger,
                                    RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);

  CsvIndexedFile indexed;
  indexed.input_path_ = input_path;
  indexed.file_name_ = std::filesystem::path(input_path).filename().string();

  indexed.file_size_ = GetFileSize(input_path);

  auto format = MakeViewFormat(options);
  csv::CSVReader reader(input_path, format);
  indexed.format_ = reader.get_format();
  indexed.headers_ = reader.get_col_names();
  if (indexed.headers_.empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  for (auto& row : reader) {
    indexed.index_.push_back({static_cast<std::uint64_t>(row.byte_offset())});
  }

  stats.rows_processed = static_cast<std::uint64_t>(indexed.index_.size());
  stats.bytes_processed = indexed.file_size_;
  if (logger.verbose) {
    logger.verbose("view: indexed " + std::to_string(indexed.index_.size()) +
                   " row offset(s) from " + input_path);
  }
  return indexed;
}

const std::string& CsvIndexedFile::input_path() const {
  return input_path_;
}

const std::string& CsvIndexedFile::file_name() const {
  return file_name_;
}

const std::vector<std::string>& CsvIndexedFile::headers() const {
  return headers_;
}

std::uint64_t CsvIndexedFile::row_count() const {
  return static_cast<std::uint64_t>(index_.size());
}

std::vector<std::vector<std::string>> CsvIndexedFile::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  if (offset >= row_count() || limit == 0) {
    return {};
  }

  const auto count = std::min<std::uint64_t>(limit, row_count() - offset);
  const auto start = index_[static_cast<std::size_t>(offset)].byte_offset;
  const auto after_last_row = offset + count;
  const auto end = after_last_row < row_count()
      ? index_[static_cast<std::size_t>(after_last_row)].byte_offset
      : file_size_;
  if (end < start) {
    throw std::runtime_error("row index is not monotonic");
  }

  const auto length = end - start;
  if (length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("requested row page is too large to materialize");
  }

  std::ifstream input(input_path_, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open input file: " + input_path_);
  }
  input.seekg(static_cast<std::streamoff>(start), std::ios::beg);

  std::string buffer(static_cast<std::size_t>(length), '\0');
  input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  buffer.resize(static_cast<std::size_t>(input.gcount()));

  auto page_format = format_;
  page_format.no_header().variable_columns(csv::VariableColumnPolicy::KEEP);
  std::stringstream page_stream(buffer);
  csv::CSVReader page_reader(page_stream, page_format);

  std::vector<std::vector<std::string>> rows;
  rows.reserve(static_cast<std::size_t>(count));
  for (auto& row : page_reader) {
    rows.emplace_back(std::vector<std::string>(row));
    if (rows.size() == count) {
      break;
    }
  }
  return rows;
}

CsvViewData CsvViewData::Open(const std::string& input_path,
                              const RunOptions& options,
                              const LoggerCallbacks& logger,
                              RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);
  const auto file_size = GetFileSize(input_path);
  const auto materialize = [&]() {
    if (options.view_edit) {
      return true;
    }
    switch (options.view_mode) {
      case ViewModeSelection::Materialized:
        return true;
      case ViewModeSelection::Paged:
        return false;
      case ViewModeSelection::Auto:
        return file_size <= ThresholdBytes(options.view_materialize_threshold_mb);
    }
    return false;
  }();

  if (materialize) {
    return CsvViewData(OpenMaterializedFile(input_path, options, logger, stats));
  }
  return CsvViewData(CsvIndexedFile::Open(input_path, options, logger, stats));
}

CsvViewData::CsvViewData(CsvMaterializedFile materialized)
    : data_(std::move(materialized)) {}

CsvViewData::CsvViewData(CsvIndexedFile indexed)
    : data_(std::move(indexed)) {}

CsvViewDataMode CsvViewData::mode() const {
  return std::holds_alternative<CsvMaterializedFile>(data_)
      ? CsvViewDataMode::Materialized
      : CsvViewDataMode::Paged;
}

std::string_view CsvViewData::mode_name() const {
  return mode() == CsvViewDataMode::Materialized ? "materialized" : "paged";
}

const std::string& CsvViewData::input_path() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->input_path;
  }
  return std::get<CsvIndexedFile>(data_).input_path();
}

const std::string& CsvViewData::file_name() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->file_name;
  }
  return std::get<CsvIndexedFile>(data_).file_name();
}

const std::vector<std::string>& CsvViewData::headers() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->headers;
  }
  return std::get<CsvIndexedFile>(data_).headers();
}

std::uint64_t CsvViewData::row_count() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return static_cast<std::uint64_t>(materialized->rows.size());
  }
  return std::get<CsvIndexedFile>(data_).row_count();
}

std::vector<std::vector<std::string>> CsvViewData::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    if (offset >= row_count() || limit == 0) {
      return {};
    }
    const auto count = std::min<std::uint64_t>(limit, row_count() - offset);
    const auto begin = materialized->rows.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = begin + static_cast<std::ptrdiff_t>(count);
    return {begin, end};
  }
  return std::get<CsvIndexedFile>(data_).read_rows(offset, limit);
}

void CsvViewData::edit_cell(const std::uint64_t row,
                            const std::string& column,
                            const std::string& value) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  const auto col = std::find(materialized->headers.begin(), materialized->headers.end(), column);
  if (col == materialized->headers.end()) {
    throw std::runtime_error("unknown column: " + column);
  }
  if (row >= materialized->rows.size()) {
    throw std::out_of_range("row index out of bounds");
  }
  materialized->rows[static_cast<std::size_t>(row)][
      static_cast<std::size_t>(std::distance(materialized->headers.begin(), col))] = value;
}

void CsvViewData::delete_row(const std::uint64_t row) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (row >= materialized->rows.size()) {
    throw std::out_of_range("row index out of bounds");
  }
  materialized->rows.erase(materialized->rows.begin() + static_cast<std::ptrdiff_t>(row));
}

void CsvViewData::insert_row(const std::uint64_t row, const std::vector<std::string>& values) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (values.size() != materialized->headers.size()) {
    throw std::runtime_error("inserted row must match header shape");
  }
  if (row > materialized->rows.size()) {
    throw std::out_of_range("row index out of bounds");
  }
  materialized->rows.insert(
      materialized->rows.begin() + static_cast<std::ptrdiff_t>(row), values);
}

void CsvViewData::save() {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("saving requires materialized view mode");
  }

  if (GetFileSize(materialized->input_path) != materialized->source_size ||
      GetFileMtime(materialized->input_path) != materialized->source_mtime) {
    throw std::runtime_error("source file changed externally; reload before saving");
  }

  const auto target = std::filesystem::path(materialized->input_path);
  const auto temp = target.parent_path() /
      (target.filename().string() + ".csvzall-save-" + GenerateSessionToken() + ".tmp");
  try {
    {
      std::ofstream output(temp, std::ios::binary);
      if (!output) {
        throw std::runtime_error("unable to open temporary save file: " + temp.string());
      }
      auto writer = csv::make_csv_writer(output).set_auto_flush(false);
      writer << materialized->headers;
      for (const auto& row : materialized->rows) {
        writer << row;
      }
      writer.flush();
      output.close();
      if (!output) {
        throw std::runtime_error("failed to write temporary save file: " + temp.string());
      }
    }

#ifdef _WIN32
    if (!MoveFileExW(temp.wstring().c_str(),
                     target.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      throw std::system_error(
          static_cast<int>(GetLastError()), std::system_category(), "failed to replace CSV file");
    }
#else
    std::filesystem::rename(temp, target);
#endif
    materialized->source_size = GetFileSize(materialized->input_path);
    materialized->source_mtime = GetFileMtime(materialized->input_path);
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    throw;
  }
}

struct ViewServer::Impl {
  explicit Impl(const CsvViewData& source_data, const LoggerCallbacks& source_logger)
      : data(source_data), logger(source_logger) {}

  CsvViewData data;
  LoggerCallbacks logger;
  httplib::Server server;
  std::thread thread;
  std::atomic<bool> running{false};
  std::atomic<bool> stop_requested{false};
  int port = -1;
  bool serve_once = false;
  bool editable = false;
  std::string token;
  std::filesystem::path vendor_asset_root;
  std::string ag_grid_js;
  std::string ag_grid_css;
  std::string ag_theme_css;

  bool HasValidToken(const httplib::Request& request) const {
    const auto header = request.get_header_value("X-Session-Token");
    if (!header.empty()) {
      return header == token;
    }
    const auto param = request.get_param_value("token");
    return !param.empty() && param == token;
  }

  void MaybeStopAfterRequest() {
    if (!serve_once || stop_requested.exchange(true)) {
      return;
    }
    std::thread([this]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      server.stop();
    }).detach();
  }

  void RejectUnauthorized(httplib::Response& response) {
    response.status = 403;
    response.set_content("forbidden\n", "text/plain; charset=utf-8");
  }

  bool RequireEditable(httplib::Response& response) const {
    if (editable && data.mode() == CsvViewDataMode::Materialized) {
      return true;
    }
    response.status = 405;
    response.set_content("viewer is read-only\n", "text/plain; charset=utf-8");
    return false;
  }
};

ViewServer::ViewServer(const CsvViewData& data, const LoggerCallbacks& logger)
    : impl_(std::make_unique<Impl>(data, logger)) {}

ViewServer::~ViewServer() {
  Stop();
}

int ViewServer::Start(const ViewServerOptions& options) {
  try {
    impl_->vendor_asset_root = ResolveVendorAssetRoot();
    impl_->ag_grid_js = ReadFileText(impl_->vendor_asset_root / "ag-grid-community.min.js");
    impl_->ag_grid_css = ReadFileText(impl_->vendor_asset_root / "ag-grid.css");
    impl_->ag_theme_css = ReadFileText(impl_->vendor_asset_root / "ag-theme-alpine.css");
  } catch (const std::exception& ex) {
    if (impl_->logger.error) {
      impl_->logger.error(std::string("view: ") + ex.what());
    }
    return 1;
  }

  impl_->serve_once = options.serve_once;
  impl_->editable = options.editable;
  impl_->stop_requested = false;
  impl_->token = options.session_token.empty() ? GenerateSessionToken() : options.session_token;

  impl_->server.Get("/", [this](const httplib::Request& request, httplib::Response& response) {
    if (!impl_->HasValidToken(request)) {
      impl_->RejectUnauthorized(response);
      return;
    }
    response.set_content(std::string(kViewerHtml), "text/html; charset=utf-8");
    impl_->MaybeStopAfterRequest();
  });

  impl_->server.Get("/assets/viewer.css",
                    [this](const httplib::Request&, httplib::Response& response) {
                      response.set_content(std::string(kViewerCss), "text/css; charset=utf-8");
                    });
  impl_->server.Get("/assets/viewer.js",
                    [this](const httplib::Request&, httplib::Response& response) {
                      response.set_content(std::string(kViewerJs),
                                           "application/javascript; charset=utf-8");
                    });
  impl_->server.Get("/assets/ag-grid-community.min.js",
                    [this](const httplib::Request&, httplib::Response& response) {
                      response.set_content(impl_->ag_grid_js,
                                           "application/javascript; charset=utf-8");
                    });
  impl_->server.Get("/assets/ag-grid.css",
                    [this](const httplib::Request&, httplib::Response& response) {
                      response.set_content(impl_->ag_grid_css, "text/css; charset=utf-8");
                    });
  impl_->server.Get("/assets/ag-theme-alpine.css",
                    [this](const httplib::Request&, httplib::Response& response) {
                      response.set_content(impl_->ag_theme_css, "text/css; charset=utf-8");
                    });

  impl_->server.Get("/api/schema",
                    [this](const httplib::Request& request, httplib::Response& response) {
                      if (!impl_->HasValidToken(request)) {
                        impl_->RejectUnauthorized(response);
                        return;
                      }
                      response.set_content(BuildSchemaJson(impl_->data, impl_->editable),
                                           "application/json; charset=utf-8");
                      impl_->MaybeStopAfterRequest();
                    });

  impl_->server.Get("/api/rows",
                    [this](const httplib::Request& request, httplib::Response& response) {
                      if (!impl_->HasValidToken(request)) {
                        impl_->RejectUnauthorized(response);
                        return;
                      }

                      std::uint64_t offset = 0;
                      std::uint64_t limit = kDefaultRowsPerPage;
                      if (!ParseUint64Param(request, "offset", 0, offset) ||
                          !ParseUint64Param(request, "limit", kDefaultRowsPerPage, limit) ||
                          limit == 0) {
                        BadRequest(response, "offset and limit must be non-negative integers; limit must be greater than 0");
                        return;
                      }
                      if (impl_->data.mode() == CsvViewDataMode::Paged) {
                        limit = std::min<std::uint64_t>(limit, kMaxRowsPerPage);
                      } else if (offset < impl_->data.row_count()) {
                        limit = std::min<std::uint64_t>(limit, impl_->data.row_count() - offset);
                      }

                      try {
                        const auto rows = impl_->data.read_rows(offset, limit);
                        response.set_content(BuildRowsJson(impl_->data, offset, limit, rows),
                                             "application/json; charset=utf-8");
                      } catch (const std::exception& ex) {
                        if (impl_->logger.error) {
                          impl_->logger.error(std::string("view: ") + ex.what());
                        }
                        response.status = 500;
                        response.set_content("failed to read rows\n", "text/plain; charset=utf-8");
                      }
                      impl_->MaybeStopAfterRequest();
                    });

  impl_->server.Post("/api/edit-cell",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.edit_cell(
                             JsonUintField(request.body, "row"),
                             JsonStringField(request.body, "column"),
                             JsonStringField(request.body, "value"));
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Post("/api/delete-row",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.delete_row(JsonUintField(request.body, "row"));
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Post("/api/insert-row",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.insert_row(
                             JsonUintField(request.body, "row"),
                             JsonStringArrayField(request.body, "values"));
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Post("/api/save",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.save();
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         response.status = 409;
                         response.set_content(std::string(ex.what()) + "\n",
                                              "text/plain; charset=utf-8");
                       }
                     });

  impl_->server.Get("/api/health",
                    [this](const httplib::Request& request, httplib::Response& response) {
                      if (!impl_->HasValidToken(request)) {
                        impl_->RejectUnauthorized(response);
                        return;
                      }
                      response.set_content(BuildHealthJson(),
                                           "application/json; charset=utf-8");
                      impl_->MaybeStopAfterRequest();
                    });

  if (options.requested_port == 0) {
    impl_->port = impl_->server.bind_to_any_port("127.0.0.1");
  } else if (impl_->server.bind_to_port("127.0.0.1", options.requested_port)) {
    impl_->port = options.requested_port;
  } else {
    impl_->port = -1;
  }

  if (impl_->port < 0) {
    if (impl_->logger.error) {
      impl_->logger.error("view: failed to bind a local HTTP port on 127.0.0.1");
    }
    return 1;
  }

  impl_->running = true;
  impl_->thread = std::thread([this]() {
    impl_->server.listen_after_bind();
    impl_->running = false;
  });
  impl_->server.wait_until_ready();
  return 0;
}

void ViewServer::Stop() {
  if (!impl_) {
    return;
  }
  if (impl_->running) {
    impl_->server.stop();
    impl_->running = false;
  }
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
}

int ViewServer::Wait() {
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  impl_->running = false;
  return 0;
}

int ViewServer::bound_port() const {
  return impl_->port;
}

const std::string& ViewServer::session_token() const {
  return impl_->token;
}

std::string ViewServer::viewer_url() const {
  return "http://127.0.0.1:" + std::to_string(impl_->port) + "/?token=" + impl_->token;
}

std::string FormatViewStartupOutput(const std::string& url, const bool startup_json) {
  if (startup_json) {
    return BuildStartupJson(url);
  }
  return url;
}

int RunView(const std::string& input_path,
            std::ostream& output,
            const RunOptions& options,
            const LoggerCallbacks& logger,
            RunStats& stats,
            int requested_port,
            bool open_browser,
            bool serve_once,
            bool startup_json) {
  std::unique_ptr<CsvViewData> data;
  try {
    data = std::make_unique<CsvViewData>(CsvViewData::Open(input_path, options, logger, stats));
  } catch (const std::exception& ex) {
    if (logger.error) {
      logger.error(std::string("view: ") + ex.what());
    }
    return 1;
  }

  ViewServer server(*data, logger);
  if (const auto rc = server.Start({requested_port, serve_once, options.view_edit, {}}); rc != 0) {
    return rc;
  }

  const auto url = server.viewer_url();
  output << FormatViewStartupOutput(url, startup_json) << '\n';
  output.flush();

  if (logger.info) {
    logger.info("view: local-only read-only viewer on 127.0.0.1");
    logger.info("view: " + std::to_string(data->row_count()) + " row(s) loaded in " +
                std::string(data->mode_name()) + " mode from " + input_path);
  }

  if (open_browser && !OpenBrowserUrl(url) && logger.info) {
    logger.info("view: could not open a browser automatically; open the printed URL manually");
  }

  return server.Wait();
}

}  // namespace csvzall::pipeline::commands

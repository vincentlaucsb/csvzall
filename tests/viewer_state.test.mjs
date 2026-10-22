import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  createDirtyStateEmitter,
  dirtyStateMessage,
  handleUnsavedBeforeUnload,
  installUnsavedChangesBeforeUnload,
  postDirtyState,
} from '../src/viewer/modules/dirty-state.mjs';
import { currentGridColumns, installColumnContextMenu, rowInsertionIndex, rowsToObjects } from '../src/viewer/modules/grid.mjs';
import { rowsToMarkdownTable } from '../src/viewer/modules/markdown.mjs';

test('Markdown cells keep HTML comments, backslashes and formatting characters literal', () => {
  const table = rowsToMarkdownTable(['value'], [['<!-- hidden --> & *bold* `code` [x] \\ |']]);
  assert.ok(table.includes('&lt;!-- hidden --&gt; &amp; \\*bold\\* \\`code\\` \\[x\\] \\\\ \\|'));
});
import { buildSavePayload, saveViewerState, installHostSaveRequests, createMutationTracker } from '../src/viewer/modules/save.mjs';
import { createViewStateStore } from '../src/viewer/modules/state.mjs';
import { columnSqlQuery, defaultSqlQuery, installSqlViewer, waitForViewerEdits } from '../src/viewer/modules/sql.mjs';

const internals = {
  buildSavePayload,
  createDirtyStateEmitter,
  createViewStateStore,
  currentGridColumns,
  defaultSqlQuery,
  dirtyStateMessage,
  handleUnsavedBeforeUnload,
  installUnsavedChangesBeforeUnload,
  postDirtyState,
  rowsToMarkdownTable,
  rowsToObjects,
  rowInsertionIndex,
  saveViewerState,
};

test('host Save requests only accept the parent and acknowledge after persistence', async () => {
  const messages = [];
  let listener, finish;
  const parent = { postMessage: message => messages.push(message) };
  const win = { parent, addEventListener: (_type, fn) => { listener = fn; }, removeEventListener() {} };
  const dispose = installHostSaveRequests({ win, save: () => new Promise(resolve => { finish = resolve; }), getDirty: () => false });
  const data = { source: 'obsidian-csvzall', type: 'save-request', version: 1, requestId: 9 };
  await listener({ source: {}, data });
  assert.equal(finish, undefined);
  const pending = listener({ source: parent, data });
  assert.equal(messages.length, 0);
  finish(true); await pending;
  assert.deepEqual(messages[0], { source: 'csvzall-viewer', type: 'save-response', version: 1, requestId: 9, success: true, dirty: false });
  dispose();
});

test('native save retains edits arriving during persistence', async () => {
  const viewState = createViewStateStore();
  viewState.markColumnOrderDirty();
  let finish;
  const pending = saveViewerState({ viewState, postJson: () => new Promise(resolve => { finish = resolve; }), getColumns: () => ['a'] });
  viewState.markDataDirty(); finish({});
  const saved = await pending;
  assert.equal(viewState.dirty, true);
  assert.equal(saved.reloadAfterSave, false);
});

test('another move of an already-dirty column order cannot be cleared by an older save', async () => {
  const viewState = createViewStateStore();
  viewState.markColumnOrderDirty();
  let finish;
  const pending = saveViewerState({ viewState, postJson: () => new Promise(resolve => { finish = resolve; }), getColumns: () => ['b', 'a'] });
  viewState.setColumnOrderDirty(true);
  finish({}); const result = await pending;
  assert.equal(viewState.dirty, true);
  assert.equal(result.reloadAfterSave, false);
});

test('dispatch of pending cell or structural mutations invalidates an in-flight native save', async () => {
  for (const operation of ['cell', 'insert', 'delete', 'move', 'rename', 'reset']) {
    const viewState = createViewStateStore();
    viewState.markDataDirty();
    const mutations = createMutationTracker(() => viewState.markDataDirty());
    let finishSave, finishMutation;
    const saving = saveViewerState({ viewState, postJson: () => new Promise(resolve => { finishSave = resolve; }), getColumns: () => ['a'] });
    const mutation = mutations.run(() => new Promise(resolve => { finishMutation = resolve; }));
    await Promise.resolve();
    finishSave({}); await saving;
    assert.equal(viewState.dirty, true, operation);
    let settled = false;
    const waiting = mutations.settle().then(() => { settled = true; });
    await Promise.resolve(); assert.equal(settled, false);
    finishMutation(); await mutation; await waiting;
    assert.equal(settled, true);
  }
});

function sameJson(value) {
  return JSON.parse(JSON.stringify(value));
}

function sqlViewerHarness(overrides = {}) {
  const elements = Object.fromEntries([
    'panel', 'exitButton', 'openButton', 'input', 'clearButton', 'documentationLink',
    'error', 'runButton', 'copyButton', 'tableGrid', 'resultGrid',
    'editToolbar', 'chartButton', 'status', 'recordCount',
  ].map((name) => [name, {
    hidden: false, disabled: false, textContent: '', value: '', style: {}, scrollHeight: 70,
    attributes: {}, listeners: {}, classList: { toggle() {} },
    setAttribute(key, value) { this.attributes[key] = value; },
    addEventListener(type, listener) { this.listeners[type] = listener; },
    removeEventListener(type) { delete this.listeners[type]; },
    focus() { this.focused = true; },
    setSelectionRange(start, end) { this.selectionStart = start; this.selectionEnd = end; },
    fire(type, event = {}) { return this.listeners[type](event); },
  }]));
  const requests = [], grids = [], copied = [];
  if (overrides.noteExport) elements.insertButton = overrides.noteExport.button;
  let restored = 0;
  const viewer = installSqlViewer({
    elements, editable: true,
    createGrid(element, options) {
      grids.push({ element, options });
      return { setGridOption(key, value) { options[key] = value; } };
    },
    postJson: async (path, body) => {
      requests.push({ path, body });
      return { columns: ['name'], rows: [['alice']] };
    },
    beforeQuery: async () => {},
    restoreTable: () => { ++restored; },
    writeClipboard: async (value) => { copied.push(value); },
    ...overrides,
  });
  return { elements, viewer, requests, grids, copied, get restored() { return restored; } };
}

const settleSql = () => new Promise((resolve) => setImmediate(resolve));

test('note insertion is disabled for edited or failed query drafts and ignores late result input changes', async () => {
  const listeners = new Map();
  const parent = { postMessage() {} };
  const button = { addEventListener() {}, removeEventListener() {} };
  let finish;
  const f = sqlViewerHarness({ noteExport: { button }, windowRef: { parent, addEventListener: (type, fn) => listeners.set(type, fn), removeEventListener() {} },
    postJson: () => new Promise(resolve => { finish = resolve; }) });
  listeners.get('message')({ source: parent, data: { source: 'obsidian-csvzall', type: 'note-export-capabilities', version: 1, enabled: true } });
  f.elements.openButton.fire('click');
  f.elements.runButton.fire('click'); await settleSql();
  finish({ columns: ['name'], rows: [['Ada']] }); await settleSql();
  assert.equal(button.disabled, false);
  f.elements.input.value = 'SELECT changed FROM data'; f.elements.input.fire('input');
  assert.equal(button.disabled, true);
  f.elements.runButton.fire('click'); await settleSql();
  f.elements.input.value = 'SELECT newer FROM data'; f.elements.input.fire('input');
  finish({ columns: ['name'], rows: [['Old']] }); await settleSql();
  assert.equal(button.disabled, true);
  f.viewer.destroy();
});

test('SQLite reference uses normal links standalone and a fixed request in hosted viewers', () => {
  const standalone = {};
  standalone.parent = standalone;
  const h = sqlViewerHarness({ windowRef: standalone });
  h.elements.documentationLink.fire('click', { preventDefault() { assert.fail('standalone links should open normally'); } });
  const messages = [];
  const hosted = sqlViewerHarness({ windowRef: { parent: { postMessage: (...args) => messages.push(args) } } });
  let prevented = false;
  hosted.elements.documentationLink.fire('click', { preventDefault() { prevented = true; } });
  assert.equal(prevented, true);
  assert.deepEqual(messages, [[{ source: 'csvzall-viewer', type: 'open-documentation', version: 1, page: 'sqlite-capabilities' }, '*']]);
  assert.equal(hosted.requests.length, 0);
  assert.equal(hosted.restored, 1);
});

test('Right Arrow accepts the empty SQLite editor suggestion without executing', async () => {
  const h = sqlViewerHarness({ tableName: 'loaded' });
  h.elements.openButton.fire('click');
  h.elements.clearButton.fire('click');
  const suggestion = defaultSqlQuery('loaded');
  assert.equal(h.elements.input.placeholder, suggestion);
  let prevented = false;
  h.elements.input.fire('keydown', { key: 'ArrowRight', preventDefault() { prevented = true; } });
  assert.equal(prevented, true);
  assert.equal(h.elements.input.value, suggestion);
  assert.equal(h.elements.input.selectionStart, suggestion.length);
  assert.equal(h.elements.input.selectionEnd, suggestion.length);
  await settleSql();
  assert.equal(h.requests.length, 0);
  for (const value of ['SELECT name FROM loaded', ' ']) {
    h.elements.input.value = value;
    h.elements.input.fire('keydown', { key: 'ArrowRight', preventDefault() { assert.fail('normal navigation must remain available'); } });
    assert.equal(h.elements.input.value, value);
  }
  for (const modifier of ['ctrlKey', 'metaKey', 'altKey', 'shiftKey', 'isComposing']) {
    h.elements.input.value = '';
    h.elements.input.fire('keydown', { key: 'ArrowRight', [modifier]: true, preventDefault() { assert.fail('modified keys must remain available'); } });
    assert.equal(h.elements.input.value, '');
  }
});

test('SQLite query opens its toolbar as a draft and keeps Charts available', async () => {
  let finishedEditing = 0;
  const h = sqlViewerHarness({ finishEditing: () => { ++finishedEditing; } });
  assert.equal(h.elements.panel.hidden, true);
  assert.equal(h.elements.openButton.attributes['aria-expanded'], 'false');
  h.elements.openButton.fire('click');
  await settleSql();
  assert.equal(h.elements.panel.hidden, false);
  assert.equal(h.elements.openButton.attributes['aria-expanded'], 'true');
  assert.equal(h.elements.input.focused, true);
  assert.equal(h.elements.chartButton.hidden, false);
  assert.equal(h.elements.editToolbar.hidden, false);
  assert.equal(h.elements.editToolbar.inert, true);
  assert.equal(h.elements.editToolbar.attributes['aria-hidden'], 'true');
  assert.equal(finishedEditing, 1);
  assert.equal(h.requests.length, 0);
  h.elements.input.value = 'SELECT name FROM data';
  h.elements.exitButton.fire('click');
  assert.equal(h.elements.panel.hidden, true);
  assert.equal(h.elements.tableGrid.hidden, false);
  assert.equal(h.elements.editToolbar.inert, false);
  assert.equal(h.elements.editToolbar.attributes['aria-hidden'], 'false');
  assert.equal(h.elements.openButton.focused, true);
  h.elements.openButton.fire('click');
  await settleSql();
  assert.equal(h.elements.input.value, 'SELECT name FROM data');
  assert.equal(h.requests.length, 0);
});

test('reopening an active SQLite toolbar preserves a pending query', async () => {
  let finish;
  let requests = 0;
  const h = sqlViewerHarness({ postJson: () => {
    ++requests;
    return new Promise((resolve) => { finish = resolve; });
  } });
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  await settleSql();
  h.elements.openButton.fire('click');
  assert.equal(h.elements.runButton.disabled, true);
  finish({ columns: ['name'], rows: [['alice']] });
  await settleSql();
  assert.equal(requests, 1);
  assert.equal(h.elements.resultGrid.hidden, false);
  assert.equal(h.elements.chartButton.hidden, false);
});

test('column SQL safely quotes spaces, reserved words, quotes and embedded backticks', () => {
  for (const column of ['Coffee Drank (Cups)', 'select', 'a.b', 'a"b', 'a`b', '`', 'x` FROM data; --']) {
    assert.equal(columnSqlQuery(column), 'SELECT `' + column.replaceAll('`', '``') + '`\nFROM data\nLIMIT 100;');
  }
  assert.equal(columnSqlQuery('a`b', 'loaded'), 'SELECT `a``b`\nFROM loaded\nLIMIT 100;');
});

test('Query column opens a focused SQL draft without running or touching unsaved edits', async () => {
  const h = sqlViewerHarness();
  h.elements.tableGrid.unsavedEdit = '25';
  h.viewer.queryColumn('Coffee Drank (Cups)');
  await settleSql();
  assert.equal(h.viewer.active, true);
  assert.equal(h.elements.input.value, 'SELECT `Coffee Drank (Cups)`\nFROM data\nLIMIT 100;');
  assert.equal(h.elements.input.focused, true);
  assert.equal(h.elements.tableGrid.unsavedEdit, '25');
  assert.equal(h.elements.tableGrid.hidden, true);
  assert.equal(h.elements.resultGrid.hidden, true);
  assert.equal(h.elements.copyButton.disabled, true);
  assert.equal(h.elements.runButton.disabled, false);
  assert.equal(h.requests.length, 0);
  await h.elements.runButton.fire('click');
  await settleSql();
  assert.deepEqual(h.requests, [{ path: '/api/sql-query', body: { sql: columnSqlQuery('Coffee Drank (Cups)') } }]);
  assert.equal(h.elements.resultGrid.hidden, false);
  h.elements.exitButton.fire('click');
  assert.equal(h.elements.tableGrid.unsavedEdit, '25');
});

test('a new column draft hides old results and ignores an earlier query response', async () => {
  let finishQuery;
  const h = sqlViewerHarness({ postJson: () => new Promise((resolve) => { finishQuery = resolve; }) });
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  await settleSql();
  h.viewer.queryColumn('a`b');
  finishQuery({ columns: ['old'], rows: [['old data']] });
  await settleSql();
  assert.equal(h.elements.input.value, columnSqlQuery('a`b'));
  assert.equal(h.elements.resultGrid.hidden, true);
  assert.equal(h.elements.copyButton.disabled, true);
  assert.equal(h.elements.runButton.disabled, false);
  assert.equal(h.grids.length, 0);
  assert.match(h.elements.status.textContent, /^Query ready/);
});

test('Query column clears Copy Markdown for the previous completed result', async () => {
  const h = sqlViewerHarness();
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  await settleSql();
  assert.equal(h.elements.copyButton.disabled, false);
  h.viewer.queryColumn('other');
  await h.elements.copyButton.fire('click');
  assert.equal(h.elements.copyButton.disabled, true);
  assert.equal(h.elements.resultGrid.hidden, true);
  assert.deepEqual(h.copied, []);
  assert.equal(h.requests.length, 1);
});

test('column header menus use the clicked column and close before focusing SQL', () => {
  let options, onContextMenu, opened;
  let columns = ['coffee', 'other'];
  const calls = [];
  installColumnContextMenu({
    createContextMenu: (value) => { options = value; return { open: (input) => { opened = input; } }; },
    gridElement: { addEventListener: (_type, listener) => { onContextMenu = listener; } },
    getColumns: () => columns,
    queryColumn: (column) => calls.push(['query', column]),
    renameColumn: (column) => calls.push(['rename', column]),
    deleteColumn: (column) => calls.push(['delete', column]),
    createIcon: () => {},
  });
  assert.deepEqual(options.items.filter((item) => item.id).map((item) => item.id), ['query-column', 'rename-column', 'delete-column']);
  const header = { getAttribute: () => 'coffee' };
  const event = { target: { closest: () => header }, clientX: 10, clientY: 20, preventDefault() { this.prevented = true; } };
  onContextMenu(event);
  assert.equal(event.prevented, true);
  assert.deepEqual(opened.context, { column: 'coffee' });
  options.onSelect({ id: 'query-column', context: { data: opened.context }, close: () => calls.push(['close']) });
  assert.deepEqual(calls, [['close'], ['query', 'coffee']]);
  options.onSelect({ id: 'rename-column', context: { data: { column: 'other' } } });
  options.onSelect({ id: 'delete-column', context: { data: { column: 'other' } } });
  assert.deepEqual(calls.slice(-2), [['rename', 'other'], ['delete', 'other']]);
  columns = ['other'];
  options.onSelect({ id: 'query-column', context: { data: { column: 'coffee' } } });
  assert.equal(calls.length, 4);
  opened = undefined;
  onContextMenu({ target: { closest: () => null } });
  assert.equal(opened, undefined);
});

test('read-only column menus offer Query column without edit actions', async () => {
  const h = sqlViewerHarness({ editable: false });
  let options;
  installColumnContextMenu({
    createContextMenu: (value) => { options = value; return { open() {} }; },
    gridElement: { addEventListener() {} },
    getColumns: () => ['value'], queryColumn: (column) => h.viewer.queryColumn(column), createIcon: () => {},
  });
  assert.deepEqual(options.items.map((item) => item.label), ['Query column']);
  options.onSelect({ id: 'query-column', context: { data: { column: 'value' } }, close() {} });
  await settleSql();
  assert.equal(h.requests.length, 0);
  assert.equal(h.elements.editToolbar.inert, true);
  assert.equal(h.elements.input.value, columnSqlQuery('value'));
});

test('SQL waits for asynchronously dispatched AG Grid edits and backend writes', async () => {
  const pendingEdits = new Set();
  let finishWrite;
  let ready = false;
  const api = { stopEditing() {
    setTimeout(() => pendingEdits.add(new Promise((resolve) => { finishWrite = resolve; })), 0);
  } };
  const settled = waitForViewerEdits(api, pendingEdits).then(() => { ready = true; });
  await new Promise((resolve) => setTimeout(resolve, 5));
  assert.equal(ready, false);
  assert.equal(pendingEdits.size, 1);
  finishWrite();
  await settled;
  assert.equal(ready, true);
});

test('SQL viewer uses a separate read-only result grid and preserves the paged editor', async () => {
  const h = sqlViewerHarness();
  assert.equal(h.elements.panel.hidden, true);
  h.elements.tableGrid.unsavedEdit = '25';
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  await settleSql();
  assert.equal(h.viewer.active, true);
  assert.equal(h.elements.tableGrid.hidden, true);
  assert.equal(h.elements.editToolbar.inert, true);
  assert.equal(h.elements.chartButton.hidden, false);
  assert.deepEqual(h.requests, [{ path: '/api/sql-query', body: { sql: defaultSqlQuery() } }]);
  assert.equal(h.grids[0].element, h.elements.resultGrid);
  assert.equal(h.grids[0].options.columnDefs[0].editable, false);
  assert.equal(h.grids[0].options.columnDefs[0].valueGetter({ data: ['alice'] }), 'alice');
  await h.elements.copyButton.fire('click');
  assert.deepEqual(h.copied, ['| name |\n| --- |\n| alice |']);
  h.elements.input.value = 'SELECT name FROM data';
  h.elements.exitButton.fire('click');
  assert.equal(h.viewer.active, false);
  assert.equal(h.elements.tableGrid.hidden, false);
  assert.equal(h.elements.tableGrid.unsavedEdit, '25');
  assert.equal(h.elements.editToolbar.hidden, false);
  assert.equal(h.elements.resultGrid.hidden, true);
  assert.equal(h.restored, 2);
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  await settleSql();
  assert.equal(h.requests[1].body.sql, 'SELECT name FROM data');
  assert.equal(h.grids.length, 1);
});

test('SQL waits for edits and ignores late results after returning to the table', async () => {
  let finishEdit, finishQuery;
  let requests = 0;
  const h = sqlViewerHarness({
    beforeQuery: () => new Promise((resolve) => { finishEdit = resolve; }),
    postJson: () => {
      ++requests;
      return new Promise((resolve) => { finishQuery = resolve; });
    },
  });
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  assert.equal(h.elements.runButton.disabled, true);
  assert.equal(requests, 0);
  finishEdit();
  await settleSql();
  assert.equal(requests, 1);
  h.elements.exitButton.fire('click');
  h.elements.status.textContent = 'Unsaved changes.';
  finishQuery({ columns: ['stale'], rows: [['old']] });
  await settleSql();
  assert.equal(h.elements.status.textContent, 'Unsaved changes.');
  assert.equal(h.grids.length, 0);
  assert.equal(h.elements.tableGrid.hidden, false);
});

test('SQL shows errors, handles empty results and supports keyboard reruns', async () => {
  let fail = true;
  const h = sqlViewerHarness({ postJson: async () => {
    if (fail) throw new Error('no such column: missing');
    return { columns: ['a.b', 'a.b'], rows: [] };
  } });
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  await settleSql();
  assert.equal(h.elements.error.textContent, 'no such column: missing');
  assert.equal(h.elements.error.hidden, false);
  assert.equal(h.elements.runButton.disabled, false);
  assert.equal(h.elements.copyButton.disabled, true);
  fail = false;
  let prevented = false;
  h.elements.input.fire('keydown', { key: 'Enter', ctrlKey: true, preventDefault() { prevented = true; } });
  await settleSql();
  assert.equal(prevented, true);
  assert.equal(h.elements.error.hidden, true);
  assert.match(h.elements.recordCount.textContent, /^0 SQLite result rows/);
  assert.notEqual(h.grids[0].options.columnDefs[0].colId, h.grids[0].options.columnDefs[1].colId);
  await h.elements.copyButton.fire('click');
  assert.deepEqual(h.copied, ['| a.b | a.b |\n| --- | --- |']);
  h.elements.clearButton.fire('click');
  assert.equal(h.elements.input.value, '');
});

test('SQL keeps read-only sessions read-only when returning to the table', async () => {
  const h = sqlViewerHarness({ editable: false });
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  await settleSql();
  h.elements.exitButton.fire('click');
  assert.equal(h.elements.editToolbar.hidden, true);
  assert.equal(h.elements.chartButton.hidden, false);
});

test('SQL does not query when returning to Table before edits finish', async () => {
  let finishEdit;
  const h = sqlViewerHarness({ beforeQuery: () => new Promise((resolve) => { finishEdit = resolve; }) });
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  h.elements.exitButton.fire('click');
  finishEdit();
  await settleSql();
  assert.equal(h.requests.length, 0);
});

test('SQL does not query when a pending edit fails', async () => {
  const h = sqlViewerHarness({ beforeQuery: async () => { throw new Error('Edit failed'); } });
  h.elements.openButton.fire('click');
  h.elements.runButton.fire('click');
  await settleSql();
  assert.equal(h.requests.length, 0);
  assert.equal(h.elements.error.textContent, 'Edit failed');
});

test('Markdown SQL export preserves duplicate aliases, dotted names and escaped values', () => {
  assert.equal(rowsToMarkdownTable(['a.b', 'a.b'], [['x|y', 'z\nq']]),
    '| a.b | a.b |\n| --- | --- |\n| x\\|y | z<br>q |');
});

test('viewer state store tracks data and column-order dirty flags independently', () => {
  const store = internals.createViewStateStore();

  assert.equal(store.dirty, false);
  assert.equal(store.dataDirty, false);
  assert.equal(store.columnOrderDirty, false);

  store.markDataDirty();
  assert.equal(store.dirty, true);
  assert.equal(store.dataDirty, true);
  assert.equal(store.columnOrderDirty, false);

  store.markColumnOrderDirty();
  assert.equal(store.dirty, true);
  assert.equal(store.dataDirty, true);
  assert.equal(store.columnOrderDirty, true);

  store.setColumnOrderDirty(false);
  assert.equal(store.dirty, true);
  assert.equal(store.dataDirty, true);
  assert.equal(store.columnOrderDirty, false);

  store.markClean();
  assert.equal(store.dirty, false);
  assert.equal(store.dataDirty, false);
  assert.equal(store.columnOrderDirty, false);
});

test('currentGridColumns reads displayed AG Grid columns and ignores non-CSV columns', () => {
  const api = {
    getAllDisplayedColumns() {
      return [
        { getColId: () => '_csvzallRowId' },
        { getColId: () => 'value' },
        { getColId: () => 'name' },
        { getColId: () => 'unknown' },
      ];
    },
  };

  assert.deepEqual(
    internals.currentGridColumns(api, ['name', 'value']),
    ['value', 'name']);
});

test('currentGridColumns falls back to column state for older AG Grid APIs', () => {
  const api = {
    getColumnState() {
      return [
        { colId: 'note' },
        { colId: '_csvzallRowId' },
        { colId: 'value' },
      ];
    },
  };

  assert.deepEqual(
    internals.currentGridColumns(api, ['value', 'note']),
    ['note', 'value']);
});

test('rowInsertionIndex preserves before-first-row insertion', () => {
  assert.equal(internals.rowInsertionIndex(0, 3, 0), 0);
  assert.equal(internals.rowInsertionIndex(0, 3, 1), 1);
  assert.equal(internals.rowInsertionIndex(2, 3, 1), 3);
  assert.equal(internals.rowInsertionIndex(undefined, 3, 0), 3);
});

test('rowsToObjects assigns absolute source row ids for paged rows', () => {
  assert.deepEqual(internals.rowsToObjects(['name', 'value'], [
    ['Ada', '10'],
    ['Ben'],
  ], 500), [
    { _csvzallRowId: 500, name: 'Ada', value: '10' },
    { _csvzallRowId: 501, name: 'Ben', value: '' },
  ]);
});

test('default SQL query uses the viewer table name', () => {
  assert.equal(internals.defaultSqlQuery(), 'SELECT *\nFROM data\nLIMIT 100;');
  assert.equal(internals.defaultSqlQuery('loaded'), 'SELECT *\nFROM loaded\nLIMIT 100;');
});

test('rowsToMarkdownTable escapes table cell separators and line breaks', () => {
  assert.equal(
    internals.rowsToMarkdownTable(
      ['name', 'note'],
      [{ name: 'Alice|Bob', note: 'one\ntwo' }]),
    '| name | note |\n| --- | --- |\n| Alice\\|Bob | one<br>two |');
});

test('saveViewerState sends an empty save payload for data-only edits', async () => {
  const store = internals.createViewStateStore();
  store.markDataDirty();
  const calls = [];

  const saved = await internals.saveViewerState({
    viewState: store,
    getColumns: () => ['value', 'name'],
    postJson(path, body) {
      calls.push({ path, body });
      return Promise.resolve({ ok: true, chartsGenerated: 0 });
    },
  });

  assert.deepEqual(sameJson(calls), [{ path: '/api/save', body: {} }]);
  assert.equal(saved.reloadAfterSave, false);
  assert.equal(store.dirty, false);
});

test('saveViewerState sends current column order and requests reload for column moves', async () => {
  const store = internals.createViewStateStore();
  store.markDataDirty();
  store.markColumnOrderDirty();
  const calls = [];

  const saved = await internals.saveViewerState({
    viewState: store,
    getColumns: () => ['value', 'name', 'note'],
    postJson(path, body) {
      calls.push({ path, body });
      return Promise.resolve({ ok: true, chartsGenerated: 0 });
    },
  });

  assert.deepEqual(sameJson(calls), [
    { path: '/api/save', body: { columns: ['value', 'name', 'note'] } },
  ]);
  assert.equal(saved.reloadAfterSave, true);
  assert.equal(store.dirty, false);
});

test('dirty state messages use the iframe host contract', () => {
  assert.deepEqual(sameJson(internals.dirtyStateMessage(true)), {
    source: 'csvzall-viewer',
    type: 'dirty-state',
    dirty: true,
  });
  assert.deepEqual(sameJson(internals.dirtyStateMessage(false)), {
    source: 'csvzall-viewer',
    type: 'dirty-state',
    dirty: false,
  });
});

test('postDirtyState sends dirty state to a target window', () => {
  const messages = [];
  const targetWindow = {
    postMessage(message, origin) {
      messages.push({ message, origin });
    },
  };

  assert.equal(internals.postDirtyState(true, targetWindow, 'app://obsidian.md'), true);
  assert.deepEqual(sameJson(messages), [{
    message: {
      source: 'csvzall-viewer',
      type: 'dirty-state',
      dirty: true,
    },
    origin: 'app://obsidian.md',
  }]);
  assert.equal(internals.postDirtyState(false, null), false);
});

test('dirty state emitter posts only when dirty state changes', () => {
  const store = internals.createViewStateStore();
  const messages = [];
  const targetWindow = {
    postMessage(message, origin) {
      messages.push({ message, origin });
    },
  };
  const emit = internals.createDirtyStateEmitter(store, targetWindow, '*');

  assert.equal(emit(), true);
  assert.equal(emit(), false);

  store.markDataDirty();
  assert.equal(emit(), true);
  assert.equal(emit(), false);

  store.markClean();
  assert.equal(emit(), true);

  assert.deepEqual(sameJson(messages), [
    {
      message: {
        source: 'csvzall-viewer',
        type: 'dirty-state',
        dirty: false,
      },
      origin: '*',
    },
    {
      message: {
        source: 'csvzall-viewer',
        type: 'dirty-state',
        dirty: true,
      },
      origin: '*',
    },
    {
      message: {
        source: 'csvzall-viewer',
        type: 'dirty-state',
        dirty: false,
      },
      origin: '*',
    },
  ]);
});

test('unsaved changes beforeunload guard only blocks while dirty', () => {
  const store = internals.createViewStateStore();
  let prevented = false;
  const cleanEvent = {
    preventDefault() {
      prevented = true;
    },
  };

  assert.equal(internals.handleUnsavedBeforeUnload(store, cleanEvent), undefined);
  assert.equal(prevented, false);
  assert.equal('returnValue' in cleanEvent, false);

  store.markDataDirty();
  const dirtyEvent = {
    preventDefault() {
      prevented = true;
    },
  };
  assert.equal(internals.handleUnsavedBeforeUnload(store, dirtyEvent), '');
  assert.equal(prevented, true);
  assert.equal(dirtyEvent.returnValue, '');
});

test('unsaved changes beforeunload guard can be removed', () => {
  const store = internals.createViewStateStore();
  const listeners = [];
  const fakeWindow = {
    addEventListener(type, listener) {
      listeners.push({ type, listener });
    },
    removeEventListener(type, listener) {
      const index = listeners.findIndex((entry) => entry.type === type && entry.listener === listener);
      if (index >= 0) {
        listeners.splice(index, 1);
      }
    },
  };

  const removeGuard = internals.installUnsavedChangesBeforeUnload(store, fakeWindow);
  assert.equal(listeners.length, 1);
  assert.equal(listeners[0].type, 'beforeunload');

  removeGuard();
  assert.equal(listeners.length, 0);
});

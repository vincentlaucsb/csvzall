import test from 'node:test';
import assert from 'node:assert/strict';
import { createViewerShell } from '../../../viewer/modules/shell.mjs';
import { entryRow, extendEntryRowBlock, sourceRowId } from '../../../viewer/modules/grid.mjs';

// Let the real shell mount its toolbar and menus. The grid spy below records the
// API calls that can rebuild headers, reset column state or replace a datasource.
function mount(t, initialSchema, request, backendOverrides = {}) {
  const document = new EventTarget();
  const nodes = new Map();
  function element() {
    return Object.assign(new EventTarget(), {
      ownerDocument: document, textContent: '', dataset: {}, content: { cloneNode() {} },
      classList: { add() {} }, append() {}, replaceChildren() {}, setAttribute() {},
      toggleAttribute() {}, querySelectorAll: () => [],
      showModal() { this.open = true; }, close() { this.open = false; },
    });
  }
  document.createElement = document.createElementNS = element;
  document.documentElement = element();
  document.body = element();
  document.querySelector = document.getElementById = id => {
    if (!nodes.has(id)) nodes.set(id, element());
    return nodes.get(id);
  };
  const window = Object.assign(new EventTarget(), {
    innerWidth: 1024, innerHeight: 768,
    MutationObserver: class { observe() {} disconnect() {} },
    requestAnimationFrame: () => 1, cancelAnimationFrame() {}, clearTimeout, setTimeout,
  });
  const frames = [];
  Object.assign(globalThis, { document, window, requestAnimationFrame: fn => frames.push(fn) });
  t.after(() => {
    shell.destroy();
    delete globalThis.document;
    delete globalThis.window;
    delete globalThis.requestAnimationFrame;
  });

  const created = [], assigned = [], columnStates = [];
  let gridOptions;
  let editing = false;
  let finishCount = 0;
  const column = { getColId: () => initialSchema.columns[0] };
  const grid = {
    getColumnState: () => [], applyColumnState: state => columnStates.push(state),
    setGridOption: (key, value) => assigned.push([key, value]),
    getCacheBlockState: () => ({ 1: {} }), destroy() {},
    getAllDisplayedColumns: () => [column],
    getEditingCells: () => editing ? [{}] : [],
    getFocusedCell: () => null, getFirstDisplayedRowIndex: () => 0,
    ensureIndexVisible() {},
    stopEditing() { editing = false; finishCount += 1; },
    refreshCells() {}, purgeInfiniteCache() {},
  };
  const shell = createViewerShell({
    backend: {
      capabilities: { columnOrder: true },
      request: (type, payload) => type === 'init' ? Promise.resolve(initialSchema) : request(type, payload),
      ...backendOverrides,
    },
    createGrid(element, options) { gridOptions = options; created.push(options.columnDefs.map(c => c.field)); return grid; },
    createContextMenu: () => ({ update() {}, destroy() {} }),
  });
  return { shell, created, columnStates, frames, document, window, get finishCount() { return finishCount; },
    setEditing(next) { editing = next; },
    edit(value) {
      const data = { [sourceRowId]: 0 };
      gridOptions.onCellEditRequest({ data, colDef: { field: column.getColId() }, column, node: { data }, newValue: value });
    },
    options: key => assigned.filter(([name]) => name === key).map(([, value]) => value) };
}

test('paging reuses the grid, column definitions and datasource and writes an unchanged count only once', async t => {
  const initial = { file: 'first.csv', columns: ['Task.name', 'Status'], rowCount: 1001 };
  const f = mount(t, initial, async (type, payload) => {
    assert.equal(type, 'readRows');
    return { totalRows: initial.rowCount, offset: payload.offset, rows: [['Write tests', 'Done']] };
  });
  const count = f.document.getElementById('record-count');
  const writes = [];
  Object.defineProperty(count, 'textContent', { get: () => writes.at(-1) ?? '', set: value => writes.push(value) });
  await f.shell.start();
  const initialStateUpdates = f.columnStates.length;
  const [datasource] = f.options('datasource');
  function read(startRow) {
    return new Promise((resolve, reject) => datasource.getRows({
      startRow, endRow: startRow + 500, sortModel: [], successCallback: resolve, failCallback: reject,
    }));
  }
  const [row] = await read(0);
  assert.equal(row['Task.name'], 'Write tests');
  assert.equal(row.Status, 'Done');
  assert.deepEqual(writes, []);
  while (f.frames.length) f.frames.shift()();
  assert.equal(count.textContent, `${(1001).toLocaleString()} rows, 2 columns`);
  await read(500);
  assert.equal(writes.length, 1);
  assert.equal(f.created.length, 1);
  assert.deepEqual(f.options('columnDefs').map(defs => defs.map(c => c.field)), [initial.columns]);
  assert.equal(f.columnStates.length, initialStateUpdates);
  assert.equal(f.options('datasource').length, 1);
});

test('a newly opened file replaces the datasource and updates the columns of the existing grid', async t => {
  const initial = { file: 'first.csv', columns: ['name'], rowCount: 1 };
  const replacement = { columns: ['total', 'date'], rowCount: 2 };
  const f = mount(t, initial, async type => { assert.equal(type, 'open'); return replacement; });
  await f.shell.start();
  await f.shell.openBuffer('replacement.csv', new ArrayBuffer(0));
  assert.equal(f.created.length, 1);
  assert.deepEqual(f.options('columnDefs').map(defs => defs.map(c => c.field)), [initial.columns, replacement.columns]);
  const sources = f.options('datasource');
  assert.equal(sources.length, 2);
  assert.notEqual(sources[0], sources[1]);
  assert.deepEqual(f.shell.schema.columns, replacement.columns);
});

test('the entry row appears in the next block when the CSV fills an entire block', async t => {
  const initial = { file: 'paged.csv', columns: ['name'], rowCount: 500 };
  const f = mount(t, initial, async (type, { offset, limit }) => {
    assert.equal(type, 'readRows');
    const rows = Array.from({ length: Math.min(limit, initial.rowCount - offset) }, (_, i) => [String(offset + i)]);
    return { totalRows: initial.rowCount, offset, rows };
  });
  await f.shell.start();
  const [datasource] = f.options('datasource');
  function read(startRow) {
    return new Promise((resolve, reject) => datasource.getRows({
      startRow, endRow: startRow + 500, sortModel: [],
      successCallback: (rows, lastRow) => resolve({ rows, lastRow }), failCallback: reject,
    }));
  }
  const first = await read(0);
  const last = await read(500);
  assert.equal(first.rows.length, 500);
  assert.equal(first.rows.at(-1)[sourceRowId], 499);
  assert.equal(first.lastRow, 501);
  assert.equal(last.lastRow, 501);
  assert.equal(last.rows.length, 1);
  assert.equal(last.rows[0][entryRow], true);
  assert.equal(last.rows[0][sourceRowId], undefined);
  assert.equal(f.shell.schema.totalRows, 500);
});

test('growing the entry row updates only the terminal block and lets a new block load on demand', () => {
  const calls = [];
  const data = { [sourceRowId]: 498 };
  const api = {
    setRowCount: (...args) => calls.push(['count', ...args]),
    getDisplayedRowAtIndex: index => ({
      setDataAndId: (row, id) => calls.push(['entry', index, row[entryRow], id]),
    }),
    refreshInfiniteCache: () => assert.fail('Appending to a cached entry must not re-read the grid'),
  };
  const node = { rowIndex: 498, data, updateData: () => calls.push(['update']) };
  extendEntryRowBlock(api, node, data, 499, 500, ['name']);
  assert.deepEqual(calls, [['update'], ['count', 500, true], ['entry', 499, true, '499']]);
  calls.length = 0;
  node.rowIndex = 499;
  extendEntryRowBlock(api, node, data, 500, 500, ['name']);
  assert.deepEqual(calls, [['update'], ['count', 501, true]]);
});

test('a native external change alerts during editing and preserves edits in a host recovery copy', async t => {
  const schema = { file: 'sample.csv', columns: ['name'], rowCount: 1 };
  const calls = [];
  const f = mount(t, schema, async type => {
    calls.push(type);
    return { edited: true };
  }, {
    externalChanged: async () => true,
    reload: async () => { calls.push('reload'); return schema; },
  });
  const recoveries = [];
  f.shell.connectHost({ isHostMode: () => true, emitDirtyState() {} });
  f.shell.connectRecovery(edits => recoveries.push(JSON.parse(edits)));
  await f.shell.start();
  f.edit('value to keep');
  await new Promise(resolve => setTimeout(resolve, 0));
  f.setEditing(true);
  f.shell.externalChange({});
  await new Promise(resolve => setTimeout(resolve, 0));
  const notice = f.document.getElementById('csv-conflict');
  assert.equal(notice.hidden, false);
  assert.match(notice.textContent, /changed on disk/);
  assert.equal(f.finishCount, 0);
  f.shell.setStatus('Unrelated status');
  assert.match(notice.textContent, /changed on disk/);
  assert.deepEqual(calls, ['editCell']);
  f.document.getElementById('download-recovery').dispatchEvent(new Event('click'));
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.deepEqual(recoveries, [{ file: 'sample.csv', columnOrder: ['name'],
    edits: [{ type: 'editCell', rowIndex: 0, column: 'name', value: 'value to keep' }] }]);
  f.shell.recoveryResult({ success: true, path: 'sample.edits.json' });
  assert.match(notice.textContent, /sample\.edits\.json/);
  assert.equal(f.shell.dirty, true);
});

test('auto-save waits for a committed edit and leaves the next editor open until a manual save', async t => {
  const schema = { file: 'sample.csv', columns: ['name'], rowCount: 1 };
  const writes = [];
  const editValues = [];
  const f = mount(t, schema, async (_, payload) => {
    editValues.push(payload.value);
    return { edited: true };
  }, {
    async save() { writes.push(editValues.slice()); return {}; },
    persist: () => new Promise(resolve => { acknowledge = resolve; }),
  });
  let acknowledge;
  let timer;
  f.window.setTimeout = (callback, delay) => {
    if (delay === 800) { timer = callback; return 0; }
    return setTimeout(callback, delay);
  };
  f.shell.connectHost({ isHostMode: () => true, emitDirtyState() {} });
  await f.shell.start();
  f.shell.setAutoSave({ visible: true, enabled: true });
  const toggle = f.document.getElementById('autosave');
  toggle.checked = false;
  toggle.dispatchEvent(new Event('change'));
  f.shell.setAutoSave({ visible: true, enabled: true });
  assert.equal(toggle.checked, false);
  f.shell.setAutoSave({ visible: false, enabled: true });
  assert.equal(f.document.getElementById('autosave-control').hidden, true);
  f.shell.setAutoSave({ visible: true, enabled: true });
  assert.equal(toggle.checked, true);
  f.edit('first');
  await new Promise(resolve => setTimeout(resolve, 0));
  f.setEditing(true);
  timer();
  assert.equal(f.finishCount, 0);
  assert.deepEqual(writes, []);
  f.setEditing(false);
  timer();
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.deepEqual(writes, [['first']]);
  assert.equal(f.finishCount, 0);
  assert.equal(f.document.body.inert, undefined);
  f.setEditing(true);
  f.edit('second');
  await Promise.resolve();
  assert.deepEqual(editValues, ['first']);
  acknowledge(true);
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.deepEqual(editValues, ['first', 'second']);
  assert.equal(f.shell.dirty, true);
  assert.equal(f.finishCount, 0);
  f.shell.setAutoSave({ visible: true, enabled: false });
  const saved = f.shell.save();
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(f.finishCount, 1);
  acknowledge(true);
  await saved;
  assert.equal(f.shell.dirty, false);
});

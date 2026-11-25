import test from 'node:test';
import assert from 'node:assert/strict';
import { createViewerShell } from '../../../viewer/modules/shell.mjs';

// Let the real shell mount its toolbar and menus. The grid spy below records the
// API calls that can rebuild headers, reset column state or replace a datasource.
function mount(t, initialSchema, request) {
  const document = new EventTarget();
  const nodes = new Map();
  function element() {
    return Object.assign(new EventTarget(), {
      ownerDocument: document, textContent: '', dataset: {}, content: { cloneNode() {} },
      classList: { add() {} }, append() {}, replaceChildren() {}, setAttribute() {},
      toggleAttribute() {}, querySelectorAll: () => [],
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
    requestAnimationFrame: () => 1, cancelAnimationFrame() {}, clearTimeout,
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
  const grid = {
    getColumnState: () => [], applyColumnState: state => columnStates.push(state),
    setGridOption: (key, value) => assigned.push([key, value]),
    getCacheBlockState: () => ({ 1: {} }), destroy() {},
  };
  const shell = createViewerShell({
    backend: {
      capabilities: { columnOrder: true },
      request: (type, payload) => type === 'init' ? Promise.resolve(initialSchema) : request(type, payload),
    },
    createGrid(element, options) { created.push(options.columnDefs.map(c => c.field)); return grid; },
    createContextMenu: () => ({ update() {}, destroy() {} }),
  });
  return { shell, created, columnStates, frames, document,
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

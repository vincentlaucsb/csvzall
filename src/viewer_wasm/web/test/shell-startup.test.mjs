import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { parseAst } from 'rollup/parseAst';

// Exercise the actual shared startup method without mocking the entire grid DOM.
const source = readFileSync(new URL('../../../viewer/modules/shell.mjs', import.meta.url), 'utf8');
const factory = parseAst(source).body.find(node => node.type === 'ExportNamedDeclaration' && node.declaration?.id?.name === 'createViewerShell').declaration;
const shell = factory.body.body.flatMap(node => node.type === 'VariableDeclaration' ? node.declarations : []).find(node => node.id.name === 'shell');
const start = shell.init.properties.find(node => node.key.name === 'start');

function fixture() {
  const dialogs = [];
  const statuses = [];
  const views = [];
  let resolve, reject;
  const ready = new Promise((yes, no) => { resolve = yes; reject = no; });
  const context = {
    backend: { capabilities: { localFiles: true }, request(type) { assert.equal(type, 'init'); return ready; } },
    engineReady: false, pendingOpenFile: null, viewOpen: false, hostBridge: { isHostMode: () => false },
    fileInput: { disabled: true }, features: [], featureDisposers: [], shell: {},
    document: { getElementById: () => ({ textContent: '' }) },
    setStatus: text => statuses.push(text), scheduleExternal() {},
    loadView: (schema, name) => { context.viewOpen = true; views.push({ schema, name }); },
    showLoading: (...args) => dialogs.push(['show', ...args]),
    hideLoading: () => dialogs.push(['hide']),
  };
  const launch = runInNewContext(`({${source.slice(start.start, start.end)}}).start`, context);
  return { launch, context, dialogs, statuses, views, resolve, reject };
}

test('engine startup stays non-modal while pending and after schema arrival', async () => {
  const f = fixture();
  const pending = f.launch();
  assert.deepEqual(f.dialogs, []);
  assert.equal(f.context.fileInput.disabled, true);
  f.resolve({ file: 'data.csv', columns: ['name'] });
  await pending;
  assert.deepEqual(f.dialogs, []);
  assert.equal(f.views[0].name, 'data.csv');
  assert.equal(f.context.fileInput.disabled, false);
  assert.equal(f.context.engineReady, true);
});

test('startup errors remain visible without opening or dismissing an operation dialog', async () => {
  const f = fixture();
  const pending = f.launch();
  f.reject(new Error('Engine failed'));
  await assert.rejects(pending, /Engine failed/);
  assert.deepEqual(f.statuses, ['Engine failed']);
  assert.deepEqual(f.dialogs, []);
  assert.equal(f.context.fileInput.disabled, true);
});

function countFixture(rowCount) {
  const frames = [];
  const node = { textContent: '' };
  const context = {
    recordCountNode: node, resultMode: false, viewGeneration: 0, rowCount: 0, rowRevision: 0, activeColumns: [],
    document: { getElementById: () => ({}) }, gridApi: { applyColumnState() {} },
    pendingCellEdits: new Set(), pendingEntryCount: 0, requestAnimationFrame: fn => frames.push(fn),
    setDatasource(value) { context.datasource = value; },
    request: async () => ({ totalRows: rowCount, offset: 0, rows: [] }),
    rowsToObjects: () => [], makeEntryRow: () => ({}), setGridColumnDefs() {}, setDirty() {}, setStatus() {}, hostBridge: null,
  };
  const functions = factory.body.body.filter(n => n.type === 'FunctionDeclaration' &&
    ['loadView', 'applySchema', 'updateRecordCount', 'displayedRowCount'].includes(n.id.name));
  runInNewContext(functions.map(n => source.slice(n.start, n.end)).join('\n'), context);
  const schema = { columns: ['id'], rowCount };
  context.loadView(schema, 'sample.csv', { deferInitialCount: true });
  const read = () => context.datasource.getRows({
    startRow: 0, endRow: 500, sortModel: [], successCallback() {}, failCallback() {},
  });
  return { context, node, frames, read };
}

test('initial table count waits through a paint opportunity, including toolbar restoration', async () => {
  const { context, node, frames, read } = countFixture(1000);
  context.updateRecordCount(); // SQL toolbar restores the table as it mounts.
  assert.equal(node.textContent, '');
  await read();
  frames.shift()();
  assert.equal(node.textContent, '');
  frames.shift()();
  assert.equal(node.textContent, `${(1000).toLocaleString()} rows, 1 columns`);
  context.applySchema({ columns: ['id'], rowCount: 999 });
  assert.equal(node.textContent, '999 rows, 1 columns');
});

test('empty tables show their count immediately and an old frame cannot change a replacement view', async () => {
  assert.equal(countFixture(0).node.textContent, '0 rows, 1 columns');
  const { context, node, frames, read } = countFixture(2);
  await read();
  context.loadView({ columns: ['new'], rowCount: 3 }, 'other.csv', { deferInitialCount: true });
  frames.shift()();
  frames.shift()();
  assert.equal(node.textContent, '');
});

test('a query opened during startup keeps its result count and a failed first page shows the known CSV count', async () => {
  const sql = countFixture(5);
  await sql.read();
  sql.context.resultMode = true;
  sql.node.textContent = '1 SQLite result row';
  sql.frames.shift()();
  sql.frames.shift()();
  assert.equal(sql.node.textContent, '1 SQLite result row');

  const failed = countFixture(5);
  failed.context.request = async () => { throw new Error('Row load failed'); };
  await failed.read();
  assert.equal(failed.node.textContent, '5 rows, 1 columns');
});

test('Save sees a failed edit that starts and finishes during stopEditing', async () => {
  const finish = factory.body.body.find(node => node.type === 'FunctionDeclaration' && node.id.name === 'finishEditing');
  const failure = new Error('Insert rejected');
  const pendingCellEdits = new Set();
  const context = {
    Error, pendingCellEdits, saveEditDrain: null,
    window: { setTimeout },
    gridApi: {
      stopEditing() {
        const edit = Promise.resolve(failure);
        pendingCellEdits.add(edit);
        context.saveEditDrain?.add(edit);
        void edit.then(() => pendingCellEdits.delete(edit));
      },
    },
  };
  runInNewContext(source.slice(finish.start, finish.end), context);
  await assert.rejects(context.finishEditing({ requireSuccess: true }), /Insert rejected/);
  assert.equal(context.saveEditDrain, null);
});

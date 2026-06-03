import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

function loadViewerInternals() {
  const viewerPath = fileURLToPath(new URL('../src/viewer/viewer.js', import.meta.url));
  const context = vm.createContext({ console, URL, URLSearchParams });
  vm.runInContext(readFileSync(viewerPath, 'utf8'), context, { filename: viewerPath });
  return context.csvzallViewerInternals;
}

const internals = loadViewerInternals();

function sameJson(value) {
  return JSON.parse(JSON.stringify(value));
}

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

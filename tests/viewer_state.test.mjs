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

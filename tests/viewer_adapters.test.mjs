import test from 'node:test';
import assert from 'node:assert/strict';
import { createNativeAdapter } from '../src/viewer/modules/adapters/native.mjs';
import { createWasmAdapter } from '../src/viewer_wasm/web/src/wasm-adapter.js';

test('native adapter preserves authentication, source row identity and structural schemas', async () => {
  const calls = [];
  let columns = ['id', 'name'], totalRows = 2;
  const adapter = createNativeAdapter({ token: 'test-token', windowRef: { location: { origin: 'http://127.0.0.1:1234' } },
    fetchRef: async (url, options) => {
      calls.push({ url, options });
      let value = {};
      if (url.pathname === '/api/insert-row') value = { ok: true, totalRows: ++totalRows };
      if (url.pathname === '/api/rename-column') columns = ['id', 'title'];
      if (url.pathname === '/api/save' && JSON.parse(options.body).columns) columns = JSON.parse(options.body).columns;
      if (url.pathname === '/api/schema') value = { file: 'tasks.csv', columns, totalRows, editable: true };
      if (url.pathname === '/api/rows') value = { offset: 500, totalRows, rows: [['501', 'task']] };
      return { ok: true, text: async () => JSON.stringify(value) };
    },
  });
  assert.equal((await adapter.request('init')).rowCount, 2);
  const page = await adapter.request('readRows', { offset: 500, limit: 20 });
  assert.equal(page.offset, 500);
  await adapter.request('editCell', { rowIndex: 500, column: 'name', value: 'updated' });
  assert.deepEqual(JSON.parse(calls.find(call => call.url.pathname === '/api/edit-cell').options.body), { row: 500, column: 'name', value: 'updated' });
  assert.equal((await adapter.request('insertRow', { row: 0, values: ['', ''] })).rowCount, 3);
  assert.equal(calls.filter(call => call.url.pathname === '/api/schema').length, 1);
  assert.deepEqual((await adapter.request('renameColumn', { column: 'name', name: 'title' })).columns, ['id', 'title']);
  const saved = await adapter.save({ columns: ['title', 'id'] });
  assert.deepEqual(saved.schema.columns, ['title', 'id']);
  assert.ok(calls.every(call => call.options.headers['X-Session-Token'] === 'test-token'));
  assert.equal(calls.find(call => call.url.pathname === '/api/rows').url.searchParams.get('offset'), '500');
  await assert.rejects(adapter.request('unsupported'), /Unsupported native operation/);
});

function prefetchFixture(onRows) {
  const queries = [];
  const adapter = createNativeAdapter({
    token: 'test-token',
    windowRef: { location: { origin: 'http://127.0.0.1' } },
    async fetchRef(url) {
      let value = { columns: [], totalRows: 0 };
      if (url.pathname === '/api/rows') {
        queries.push(Object.fromEntries(url.searchParams));
        value = await onRows(queries.length);
      }
      return { ok: true, text: async () => JSON.stringify(value) };
    },
  });
  return { adapter, queries };
}

test('native grid consumes the in-flight first page once across schema initialization', async () => {
  let finish;
  const pending = new Promise(resolve => { finish = resolve; });
  const { adapter, queries } = prefetchFixture(count => count === 1 ? pending : { rows: [['updated']] });
  const page = { offset: 0, limit: 500 };
  adapter.prefetchFirstPage(500);
  assert.deepEqual(queries, [{ offset: '0', limit: '500' }]);
  await adapter.request('init');
  const read = adapter.request('readRows', page);
  finish({ rows: [['original']] });
  assert.deepEqual(await read, { rows: [['original']] });
  assert.deepEqual(await adapter.request('readRows', page), { rows: [['updated']] });
  assert.equal(queries.length, 2);
});

test('native grid discards the prefetched response on a different block, sort or edit', async () => {
  const page = { offset: 0, limit: 500 };
  for (const payload of [{ ...page, offset: 500 }, { ...page, limit: 10 }, { ...page, sortColumn: 'id', sortDirection: 'desc' }]) {
    const { adapter, queries } = prefetchFixture(count => ({ count }));
    adapter.prefetchFirstPage(500);
    assert.deepEqual(await adapter.request('readRows', payload), { count: 2 });
    assert.deepEqual(queries[1], Object.fromEntries(Object.entries(payload).map(([key, value]) => [key, String(value)])));
    assert.deepEqual(await adapter.request('readRows', page), { count: 3 });
  }
  const { adapter } = prefetchFixture(count => ({ count }));
  adapter.prefetchFirstPage(500);
  await adapter.request('editCell', { rowIndex: 0, column: 'a', value: 'updated' });
  assert.deepEqual(await adapter.request('readRows', page), { count: 2 });
});

test('native grid retries a failed prefetch through its normal row request', async () => {
  const { adapter } = prefetchFixture(count => {
    if (count === 1) throw new Error('connection closed');
    return { rows: [['available']] };
  });
  adapter.prefetchFirstPage(500);
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(await adapter.request('readRows', { offset: 0, limit: 500 }), { rows: [['available']] });
});

test('native adapter refuses missing tokens and surfaces server persistence failures', async () => {
  const windowRef = { location: { origin: 'http://127.0.0.1:1234', search: '' } };
  const missing = createNativeAdapter({ windowRef, fetchRef: () => assert.fail('must not make an unauthenticated request') });
  await assert.rejects(missing.request('init'), /Missing session token/);
  const failed = createNativeAdapter({ windowRef, token: 'token', fetchRef: async () => ({ ok: false, status: 409, text: async () => 'File changed externally' }) });
  await assert.rejects(failed.request('reset'), /File changed externally/);
});

function workerFixture() {
  const listeners = new Map(), posts = [];
  const worker = { addEventListener: (type, handler) => listeners.set(type, handler), removeEventListener: type => listeners.delete(type),
    postMessage: (message, transfer) => posts.push({ message, transfer }), terminate() { this.terminated = true; } };
  return { worker, listeners, posts, reply: message => listeners.get('message')({ data: message }) };
}

test('WASM adapter matches concurrent replies by id, transfers bytes and waits for host persistence', async () => {
  const f = workerFixture(), adapter = createWasmAdapter(f.worker);
  const buffer = new ArrayBuffer(4);
  const open = adapter.request('open', { name: 'x.csv', buffer }, [buffer]);
  const rows = adapter.request('readRows', { offset: 500, limit: 10 });
  f.reply({ id: 2, ok: true, result: { offset: 500, rows: [] } });
  f.reply({ id: 1, ok: true, result: { columns: ['a'], rowCount: 0 } });
  assert.equal((await rows).offset, 500); assert.equal((await open).rowCount, 0);
  assert.equal(f.posts[0].transfer[0], buffer);
  const preparing = adapter.save();
  assert.deepEqual(f.posts[2].message, { id: 3, type: 'prepareSave', payload: {} });
  f.reply({ id: 3, ok: true, result: { buffer, saveId: 17 } });
  const prepared = await preparing;
  let acknowledge, downloaded = false, settled = false;
  const persistence = adapter.persist(prepared, { name: 'x.csv', hostBridge: { saveFile: () => new Promise(resolve => { acknowledge = resolve; }) }, downloadBytes: () => { downloaded = true; } });
  void persistence.then(() => { settled = true; });
  assert.equal(f.posts.length, 3);
  acknowledge(true);
  await Promise.resolve();
  assert.deepEqual(f.posts[3].message, { id: 4, type: 'finishSave', payload: { saveId: 17, commit: true } });
  assert.equal(settled, false);
  f.reply({ id: 4, ok: true, result: { columns: ['a'], rowCount: 0 } });
  assert.equal(await persistence, true);
  assert.deepEqual(prepared.schema, { columns: ['a'], rowCount: 0 });
  assert.equal(downloaded, false);
  adapter.destroy(); assert.equal(f.worker.terminated, true);
});

test('WASM adapter discards a rejected host save and preserves the host error', async () => {
  const f = workerFixture(), adapter = createWasmAdapter(f.worker);
  const persistence = adapter.persist({ buffer: new ArrayBuffer(2), saveId: 42 }, {
    name: 'x.csv',
    hostBridge: { saveFile: () => Promise.reject(new Error('Host rejected write')) },
    downloadBytes: () => assert.fail('A rejected host save must not download'),
  });
  await Promise.resolve();
  assert.deepEqual(f.posts[0].message, { id: 1, type: 'finishSave', payload: { saveId: 42, commit: false } });
  f.reply({ id: 1, ok: true, result: null });
  await assert.rejects(persistence, /Host rejected write/);
  adapter.destroy();
});

test('WASM adapter rejects pending and future work after worker failure or disposal', async () => {
  const f = workerFixture(), adapter = createWasmAdapter(f.worker);
  const pending = adapter.request('init');
  f.listeners.get('error')({ message: 'Engine failed' });
  await assert.rejects(pending, /Engine failed/);
  await assert.rejects(adapter.request('open'), /Engine failed/);
  adapter.destroy();
  const g = workerFixture(), other = createWasmAdapter(g.worker);
  const save = other.save(); other.destroy();
  await assert.rejects(save, /Viewer closed/);
});

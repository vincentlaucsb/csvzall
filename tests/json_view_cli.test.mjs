import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { chmod, mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';
import { JsonDocument } from '../src/viewer/modules/json-document.mjs';
import { openJsonEditor } from '../src/viewer/modules/json-editor.mjs';

test('a host change during the initial JSON read refreshes the mounted document', async () => {
  let releaseRead;
  const firstRead = new Promise(resolve => { releaseRead = resolve; });
  const response = value => ({
    ok: true, json: () => ({ text: JSON.stringify(value), revision: String(value), name: 'file.json', editable: true }),
  });
  let reads = 0;
  const win = Object.assign(new EventTarget(), {
    parent: { postMessage() {} }, location: { search: '?token=viewer' },
    document: { querySelector: () => new EventTarget() },
    fetch: () => ++reads === 1 ? firstRead : response(2),
  });
  const mounted = [];
  const opened = openJsonEditor(win, (_parent, format) => {
    mounted.push(format.display);
    const doc = { eq: other => doc === other };
    return { state: { doc }, destroy() {} };
  });
  const modified = () => win.dispatchEvent(Object.assign(new Event('message'), {
    source: win.parent, data: { source: 'obsidian-csvzall', type: 'json-file-modified', version: 1 },
  }));
  modified();
  modified();
  assert.equal(reads, 1);
  releaseRead(response(1));
  await opened;
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(mounted, ['1', '2']);
  assert.equal(reads, 2);
});

test('Reset restores the last successful save, then follows an external edit without writing', async () => {
  let disk = '1', writes = 0;
  let pendingRead;
  const nodes = new Map();
  const document = { querySelector(selector) {
    if (!nodes.has(selector)) nodes.set(selector, new EventTarget());
    return nodes.get(selector);
  } };
  const win = Object.assign(new EventTarget(), {
    document, parent: { postMessage() {} }, location: { search: '' },
    fetch: async (_url, options) => {
      if (options.method === 'POST') { disk = options.body; writes++; }
      else if (pendingRead) await pendingRead;
      return { ok: true, json: () => ({ text: disk, revision: disk, name: 'example.json', editable: true }) };
    },
  });
  const text = value => ({ eq: other => other.toString() === value, toString: () => value, length: value.length });
  let editor;
  await openJsonEditor(win, (_parent, format, _editable, update) => {
    editor = { state: { doc: text(format.display) }, destroy() {}, focus() {},
      dispatch({ changes }) { this.state.doc = changes.insert; update(this.state.doc); } };
    return editor;
  });
  const reset = nodes.get('#reset'), save = nodes.get('#save');
  const edit = value => editor.dispatch({ changes: { insert: text(value) } });
  const settle = () => new Promise(resolve => setImmediate(resolve));
  assert.ok(reset.disabled && save.disabled);
  let releaseRead;
  pendingRead = new Promise(resolve => { releaseRead = resolve; });
  win.dispatchEvent(new Event('focus'));
  await settle();
  edit('2');
  assert.ok(!reset.disabled && !save.disabled);
  save.dispatchEvent(new Event('click'));
  assert.ok(reset.disabled && save.disabled, 'save queued behind a refresh');
  reset.dispatchEvent(new Event('click'));
  releaseRead();
  await settle();
  edit('3');
  reset.dispatchEvent(new Event('click'));
  await settle();
  assert.equal(editor.state.doc.toString(), '2');
  edit('4');
  disk = '5';
  reset.dispatchEvent(new Event('click'));
  await settle();
  assert.equal(editor.state.doc.toString(), '5');
  assert.equal(writes, 1);
  assert.ok(reset.disabled && save.disabled);
});

test('hosted JSON debounces AutoSave and uses the same revision check as manual Save', async () => {
  let disk = '1', writes = 0;
  const nodes = new Map();
  const posted = [];
  const timers = new Map();
  let nextTimer = 0;
  const document = { querySelector(selector) {
    if (!nodes.has(selector)) nodes.set(selector, new EventTarget());
    return nodes.get(selector);
  } };
  const win = Object.assign(new EventTarget(), {
    document, parent: { postMessage(message) { posted.push(message); } }, location: { search: '' },
    setTimeout(callback, delay) {
      assert.equal(delay, 800);
      timers.set(++nextTimer, callback);
      return nextTimer;
    },
    clearTimeout(id) { timers.delete(id); },
    fetch: async (_url, options) => {
      if (options.method === 'POST') {
        writes++;
        if (options.headers['If-Match'] !== disk) return { ok: false, text: async () => 'The JSON file changed on disk.' };
        disk = options.body;
      }
      return { ok: true, json: () => ({ text: disk, revision: disk, name: 'example.json', editable: true }) };
    },
  });
  const text = value => ({ eq: other => other.toString() === value, toString: () => value, length: value.length });
  let edit;
  await openJsonEditor(win, (_parent, format, _editable, update) => {
    const editor = { state: { doc: text(format.display) }, destroy() {} };
    edit = value => { editor.state.doc = text(value); update(editor.state.doc); };
    return editor;
  });
  const sendPolicy = (enabled, visible) => win.dispatchEvent(Object.assign(new Event('message'), {
    source: win.parent,
    data: { source: 'obsidian-csvzall', type: 'json-autosave-settings', version: 1, enabled, visible },
  }));
  const runTimer = async () => {
    assert.equal(timers.size, 1);
    const [callback] = timers.values();
    timers.clear();
    callback();
    await new Promise(resolve => setImmediate(resolve));
  };
  const toggle = nodes.get('#autosave');
  assert.equal(posted[0].type, 'json-autosave-ready');
  sendPolicy(true, true);
  edit('2');
  edit('3');
  assert.equal(timers.size, 1);
  await runTimer();
  assert.equal(disk, '3');
  assert.equal(writes, 1);
  assert.equal(timers.size, 0);

  edit('4');
  disk = '5';
  await runTimer();
  assert.equal(disk, '5');
  assert.match(nodes.get('#status').textContent, /changed on disk/);
  assert.equal(timers.size, 0, 'conflicts do not trigger an automatic retry');
  assert.equal(nodes.get('#save').disabled, false);
  nodes.get('#save').dispatchEvent(new Event('click'));
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(disk, '5');
  assert.equal(writes, 3);
  assert.equal(timers.size, 0);

  toggle.checked = false;
  toggle.dispatchEvent(new Event('change'));
  edit('6');
  sendPolicy(true, true);
  assert.equal(timers.size, 0, 'a visible local override persists across settings updates');
  sendPolicy(true, false);
  assert.equal(nodes.get('#autosave-control').hidden, true);
  sendPolicy(true, true);
  assert.equal(timers.size, 1, 'hiding clears the prior local override');
});

test('view opens JSON without CSV indexing and preserves the disk format', { timeout: 20000 }, async t => {
  const directory = await mkdtemp(join(tmpdir(), 'csvzall JSON '));
  const path = join(directory, 'settings.JSON');
  const original = '\uFEFF{"id":9007199254740993,"v":1.00,"v":"\\u0041"}\r\n';
  await writeFile(path, original);
  if (process.platform !== 'win32') await chmod(path, 0o640);
  const before = await stat(path);
  const child = spawn(process.env.CSVZALL_TEST_BINARY, ['view', path, '--no-open', '--startup-json', '--obsidian']);
  const exit = once(child, 'close');
  t.after(async () => { child.kill(); await exit; await rm(directory, { recursive: true, force: true }); });
  const lines = createInterface({ input: child.stdout });
  const [startup] = await Promise.race([once(lines, 'line'), exit.then(() => { throw new Error('viewer exited before startup'); })]);
  const { url, viewer } = JSON.parse(startup);
  assert.equal(viewer, 'json');
  const base = new URL(url);
  const headers = { 'X-Session-Token': base.searchParams.get('token') };
  assert.match(await (await fetch(url)).text(), /json-editor\.mjs/);
  assert.equal((await fetch(new URL('/api/rows', base), { headers })).status, 404);
  const endpoint = new URL('/api/document', base);
  assert.equal((await fetch(endpoint)).status, 403);
  const load = () => fetch(endpoint, { headers }).then(r => r.json());
  const { text, editable, revision } = await load();
  assert.equal(editable, true);
  assert.equal(text, original);
  const doc = new JsonDocument(text);
  assert.match(doc.display, /\n  "id": 9007199254740993/);
  assert.equal(doc.serialize(doc.display), original);
  assert.equal((await stat(path)).mtimeMs, before.mtimeMs);
  const save = (rev, body) => fetch(endpoint, { method: 'POST', headers: { ...headers, 'If-Match': rev }, body });
  const result = await save(revision, doc.serialize(doc.display.replace('1.00', '2.00')));
  assert.equal(result.status, 200);
  assert.equal(await readFile(path, 'utf8'), original.replace('1.00', '2.00'));
  if (process.platform !== 'win32') {
    const after = await stat(path);
    assert.deepEqual([after.uid, after.gid, after.mode], [before.uid, before.gid, before.mode]);
  }
  assert.equal((await save(revision, '{}')).status, 409);
  await writeFile(path, '{"external":true}');
  assert.equal((await save((await result.json()).revision, '{}')).status, 409);
  assert.equal((await load()).text, '{"external":true}');
});

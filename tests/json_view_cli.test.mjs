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
    parent: {}, location: { search: '?token=viewer' },
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

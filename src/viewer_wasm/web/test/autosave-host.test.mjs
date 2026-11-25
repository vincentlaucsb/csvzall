import test from 'node:test';
import assert from 'node:assert/strict';
import { installHostCsvUpdates } from '../../../viewer/modules/save.mjs';

test('host CSV updates accept only the parent, supported version, and valid WASM snapshots', () => {
  const posts = [];
  const changes = [];
  const settings = [];
  const results = [];
  let recover;
  let receive;
  const parent = { postMessage(message) { posts.push(message); } };
  const win = { parent, addEventListener(_, listener) { receive = listener; }, removeEventListener() {} };
  installHostCsvUpdates({
    win, source: 'csvzall-wasm-viewer',
    shell: {
      setAutoSave: value => settings.push(value),
      externalChange: value => changes.push(value),
      connectRecovery: callback => { recover = callback; },
      recoveryResult: value => results.push(value),
    },
  });
  assert.equal(posts[0].type, 'csv-autosave-ready');
  function send(data, source = parent) {
    receive({ source, data: { source: 'obsidian-csvzall', version: 1, ...data } });
  }
  const setting = { type: 'csv-autosave-settings', enabled: true, visible: true };
  send(setting, {});
  send({ ...setting, version: 2 });
  send({ ...setting, visible: 'yes' });
  send(setting);
  assert.equal(settings.length, 1);
  const snapshot = { type: 'csv-file-modified', buffer: new ArrayBuffer(5), revision: 2 };
  send({ ...snapshot, revision: null });
  send(snapshot);
  assert.equal(changes.length, 1);
  assert.equal(changes[0].buffer, snapshot.buffer);

  recover('{"edits":[]}');
  assert.equal(posts[1].type, 'csv-recovery-request');
  assert.equal(posts[1].edits, '{"edits":[]}');
  const reply = { type: 'csv-recovery-result', requestId: posts[1].requestId, success: true, path: 'a.edits.json' };
  send({ ...reply, requestId: 9 });
  send(reply);
  assert.equal(results.length, 1);
  assert.equal(results[0].path, reply.path);
});

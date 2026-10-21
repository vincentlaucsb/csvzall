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
    setStatus: text => statuses.push(text),
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

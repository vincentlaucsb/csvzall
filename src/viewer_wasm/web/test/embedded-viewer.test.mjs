import test from 'node:test';
import assert from 'node:assert/strict';
import { build } from 'vite';
import { fileURLToPath } from 'node:url';
import { parseAst } from 'rollup/parseAst';
import { runInNewContext } from 'node:vm';
import { existsSync } from 'node:fs';

test('embedded build contains its worker and engine while the standalone release keeps split assets', {
  skip: !existsSync(new URL('../src/generated/csvzall_viewer_wasm.wasm', import.meta.url))
    ? 'Build the WASM core and run npm run copy:wasm for this integration check.' : false,
}, async () => {
  for (const mode of ['embedded', 'production']) {
    const result = await build({
      configFile: fileURLToPath(new URL('../vite.config.js', import.meta.url)),
      root: fileURLToPath(new URL('../', import.meta.url)), mode, logLevel: 'silent',
      build: { write: false },
    });
    const outputs = result.output;
    const main = outputs.find(file => file.type === 'chunk' && file.isEntry).code;
    if (mode === 'embedded') {
      assert.equal(outputs.some(file => file.fileName.endsWith('.wasm')), false);
      assert.equal(outputs.some(file => /csv-worker.*\.js$/.test(file.fileName)), false);
      assert.match(main, /new Worker/);
      assert.match(main, /createObjectURL/);
      const engine = main.match(/data:application\/wasm;base64,([A-Za-z0-9+/=]+)/);
      assert.ok(engine, 'the inline worker must carry its real WASM engine');
      assert.ok(WebAssembly.validate(Buffer.from(engine[1], 'base64')));
      const workerSource = parseAst(main).body
        .flatMap(statement => statement.type === 'VariableDeclaration' ? statement.declarations : [])
        .map(declaration => declaration.init?.value)
        .find(value => typeof value === 'string' && value.includes('data:application/wasm;base64,'));
      assert.ok(workerSource, 'inline worker source must be available to the browser');
      let receive;
      const pending = new Map();
      const self = {
        location: { href: 'blob:app://obsidian.md/worker' },
        addEventListener(type, listener) { if (type === 'message') receive = listener; },
        postMessage(message) { pending.get(message.id)(message); },
      };
      // Match Electron workers which expose process even though this is a
      // browser-only runtime. Exercise the real compiled engine and CSV I/O.
      runInNewContext(workerSource, {
        self, location: self.location, process: { versions: { node: '24.0.0' } },
        WorkerGlobalScope: class {}, navigator: { userAgent: 'Chrome/140.0.0.0' },
        URL, fetch, console, performance, TextDecoder, TextEncoder, atob,
        WebAssembly, ArrayBuffer, Uint8Array, setTimeout, clearTimeout,
      });
      let id = 0;
      const request = (type, payload = {}) => new Promise(resolve => {
        pending.set(++id, resolve);
        receive({ data: { id, type, payload } });
      });
      const initialized = await request('init');
      assert.equal(initialized.ok, true, initialized.error);
      const opened = await request('open', { name: 'test.csv', buffer: new TextEncoder().encode('id,title\n1,hello\n').buffer });
      assert.equal(opened.ok, true, opened.error);
      assert.equal(opened.result.rowCount, 1);
      const rows = await request('readRows', { offset: 0, limit: 1 });
      assert.equal(rows.result.rows[0][1], 'hello');
    } else {
      assert.equal(outputs.filter(file => file.fileName.endsWith('.wasm')).length, 1);
      assert.equal(outputs.filter(file => /csv-worker.*\.js$/.test(file.fileName)).length, 1);
      assert.doesNotMatch(main, /data:application\/wasm;base64,/);
    }
  }
});

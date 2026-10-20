import test from 'node:test';
import assert from 'node:assert/strict';
import { build } from 'vite';
import { fileURLToPath } from 'node:url';
import { parseAst } from 'rollup/parseAst';
import { runInNewContext } from 'node:vm';
import { existsSync, readFileSync } from 'node:fs';
import { webcrypto } from 'node:crypto';

test('failed WASM saves release temporary files and streams and keep original bytes', {
  skip: !existsSync(new URL('../src/generated/csvzall_viewer_wasm.wasm', import.meta.url)),
}, async () => {
  const scriptUrl = new URL('../src/generated/csvzall_viewer_wasm.js', import.meta.url);
  const source = readFileSync(scriptUrl, 'utf8').replaceAll('import.meta.url', JSON.stringify(scriptUrl.href))
    .replace('export default createCsvzallViewerModule;', 'createCsvzallViewerModule;');
  const factory = runInNewContext(source, {
    window: {}, URL, fetch, console, performance, TextDecoder, TextEncoder, atob,
    WebAssembly, ArrayBuffer, Uint8Array, setTimeout, clearTimeout, crypto: webcrypto,
  });
  const engine = await factory({ wasmBinary: readFileSync(new URL('../src/generated/csvzall_viewer_wasm.wasm', import.meta.url)) });
  const original = 'name,note\na,text\n';
  engine.FS.writeFile('/sample.csv', original);
  const view = engine.CsvViewData.open('/sample.csv');
  try {
    view.setCsvOptions(',', '', true);
    view.editCell(0, 'note', 'comma,value');
    let rejected = false;
    try { view.save(); }
    catch (error) {
      rejected = true;
      try { assert.match(engine.getExceptionMessage(error)[1], /requires quoting/); }
      finally { engine.decrementExceptionRefcount(error); }
    }
    assert.equal(rejected, true);
    assert.deepEqual(Array.from(engine.FS.readdir('/')).filter(name => name.includes('.csvzall-save-')), []);
    assert.equal(engine.FS.streams.filter(stream => stream?.path?.includes('.csvzall-save-')).length, 0);
    assert.equal(engine.FS.readFile('/sample.csv', { encoding: 'utf8' }), original);
    view.setCsvOptions(',', '"', true);
    view.save();
    assert.match(engine.FS.readFile('/sample.csv', { encoding: 'utf8' }), /"comma,value"/);
  } finally { view.delete(); }
});

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
        WebAssembly, ArrayBuffer, Uint8Array, setTimeout, clearTimeout, crypto: webcrypto,
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
      const check = async (type, payload) => {
        const reply = await request(type, payload);
        assert.equal(reply.ok, true, reply.error);
        return reply.result;
      };
      const savedText = async () => {
        const saved = await check('save');
        return new TextDecoder('utf-8', { ignoreBOM: true }).decode(new Uint8Array(saved.buffer, saved.byteOffset, saved.byteLength));
      };
      for (const value of ["'hello'", "'semi;colon'", "'pipe|value'", "'it''s'", "O'Brien"]) {
        const csv = `name,note\na,${value}\n`;
        const opened = await check('open', { name: 'literal.csv', buffer: new TextEncoder().encode(csv).buffer });
        assert.equal(opened.csvOptions.quote, '"');
        assert.equal((await check('readRows', { offset: 0, limit: 1 })).rows[0][1], value);
        assert.equal(await savedText(), csv);
      }
      for (const quote of ["'", '~']) {
        await check('open', { name: 'ambiguous.csv', buffer: new TextEncoder().encode("name,note\na,O'Brien~literal\n").buffer });
        await check('csvOptions', { delimiter: ',', quote, hasHeaders: true });
        const saved = await check('save');
        const savedBytes = new Uint8Array(saved.buffer, saved.byteOffset, saved.byteLength);
        await check('open', { name: 'ambiguous.csv', buffer: savedBytes.slice().buffer });
        await check('readCsvOptions', { delimiter: ',', quote, hasHeaders: true });
        assert.equal((await check('readRows', { offset: 0, limit: 1 })).rows[0][1], "O'Brien~literal");
        assert.equal(await savedText(), new TextDecoder().decode(savedBytes));
        await check('editCell', { rowIndex: 0, column: 'name', value: 'changed' });
        const rejected = await request('readCsvOptions', { delimiter: ';', quote: '"', hasHeaders: false });
        assert.equal(rejected.ok, false);
        assert.match(rejected.error, /Save or reset changes/);
        assert.equal((await check('readRows', { offset: 0, limit: 1 })).rows[0][0], 'changed');
        await check('reset');
        const invalid = await request('csvOptions', { delimiter: ',', quote: ',', hasHeaders: true });
        assert.equal(invalid.ok, false);
        assert.match(invalid.error, /must differ/);
        assert.equal((await check('readRows', { offset: 0, limit: 1 })).rows[0][0], 'a');
      }
      for (const delimiter of [',', ';', '\t', '|']) {
        for (const quote of ['"', "'"]) {
          for (const bom of ['', '\ufeff']) {
            const csv = `${bom}name${delimiter}note\r\nalice${delimiter}${quote}two${delimiter}values${quote}\r\n`;
            const opened = await check('open', { name: 'dialect.csv', buffer: new TextEncoder().encode(csv).buffer });
            assert.equal(opened.csvOptions.delimiter, delimiter);
            assert.equal(opened.csvOptions.quote, quote);
            assert.equal(opened.csvOptions.bom, !!bom);
            assert.equal(await savedText(), csv);
            await check('editCell', { rowIndex: 0, column: 'name', value: 'edited' });
            const headerless = await check('csvOptions', { delimiter, quote, hasHeaders: false });
            assert.equal(headerless.columns.join(','), 'A,B');
            assert.equal(headerless.rowCount, 2);
            assert.equal((await check('readRows', { offset: 1, limit: 1 })).rows[0][0], 'edited');
            assert.equal(await savedText(), csv.replace('alice', 'edited'));
            await check('csvOptions', { delimiter: ',', quote: '"', hasHeaders: false });
            const converted = await savedText();
            assert.ok(converted.startsWith(`${bom}name,note\r\n`));
            assert.ok(!converted.includes('A,B'));
            await check('deleteRow', { row: 1 });
            await check('deleteRow', { row: 0 });
            assert.equal(await savedText(), bom);
            assert.equal((await check('insertRow', { row: 0, values: ['again', ''] })).rowCount, 1);
            assert.equal(await savedText(), `${bom}again,\r\n`);
          }
        }
      }
      for (const delimiter of ['\t', ';']) {
        const contents = `name, role\nAlice, admin${delimiter}London\nBob${delimiter}Paris\nCarol${delimiter}Rome\nDan${delimiter}Berlin\n`;
        const opened = await check('open', { name: 'later-fields.csv', buffer: new TextEncoder().encode(contents).buffer });
        assert.equal(opened.csvOptions.delimiter, delimiter);
        assert.equal(opened.csvOptions.hasHeaders, false);
        assert.match(opened.csvOptions.headersDisabledReason, /2 versus 1/);
        const expected = [['name, role', ''], ['Alice, admin', 'London'], ['Bob', 'Paris'], ['Carol', 'Rome'], ['Dan', 'Berlin']];
        assert.equal(JSON.stringify((await check('readRows', { offset: 0, limit: 100 })).rows), JSON.stringify(expected));
        assert.equal(await savedText(), contents.replace('name, role\n', `name, role${delimiter}\n`));
        assert.equal(JSON.stringify((await check('readRows', { offset: 0, limit: 100 })).rows), JSON.stringify(expected));
      }
      const sparse = await check('open', { name: 'sparse.tsv', buffer: new TextEncoder().encode('cargo\tdestination\nGlitter\tMoon\nThunder\tCloud\tEXTRA\nSpoon\n').buffer });
      assert.equal(sparse.csvOptions.delimiter, '\t');
      assert.equal(sparse.csvOptions.hasHeaders, false);
      assert.equal(sparse.rowCount, 4);
      const ragged = await check('open', { name: 'ragged.tsv', buffer: new TextEncoder().encode('a\tb\n1\t2\n3\t4\tEXTRA\n5\t6\n').buffer });
      assert.equal(ragged.csvOptions.hasHeaders, false);
      assert.match(ragged.csvOptions.headersDisabledReason, /3 versus 2/);
      assert.equal(ragged.columns.join(','), 'A,B,C');
      assert.equal(ragged.rowCount, 4);
      const expected = [['a', 'b', ''], ['1', '2', ''], ['3', '4', 'EXTRA'], ['5', '6', '']];
      assert.equal(JSON.stringify((await check('readRows', { offset: 0, limit: 100 })).rows), JSON.stringify(expected));
      for (let i = 0; i < expected.length; ++i) {
        assert.equal(JSON.stringify((await check('readRows', { offset: i, limit: 1 })).rows[0]), JSON.stringify(expected[i]));
      }
      const unsafeHeaders = await request('csvOptions', { delimiter: '\t', quote: '"', hasHeaders: true });
      assert.equal(unsafeHeaders.ok, false);
      assert.match(unsafeHeaders.error, /Headers are disabled/);
      await check('editCell', { rowIndex: 0, column: 'C', value: 'c' });
      assert.equal(await savedText(), 'a\tb\tc\n1\t2\t\n3\t4\tEXTRA\n5\t6\t\n');
      const corrected = await check('csvOptions', { delimiter: '\t', quote: '"', hasHeaders: true });
      assert.equal(corrected.csvOptions.hasHeaders, true);
      assert.equal(corrected.csvOptions.headersDisabledReason, undefined);
      assert.equal(corrected.rowCount, 3);
    } else {
      assert.equal(outputs.filter(file => file.fileName.endsWith('.wasm')).length, 1);
      assert.equal(outputs.filter(file => /csv-worker.*\.js$/.test(file.fileName)).length, 1);
      assert.doesNotMatch(main, /data:application\/wasm;base64,/);
    }
  }
});

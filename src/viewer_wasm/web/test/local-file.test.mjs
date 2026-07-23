import assert from 'node:assert/strict';
import test from 'node:test';

import { readLocalFile } from '../src/local-file.js';

test('local file loading starts before the browser reads the file bytes', async () => {
  const events = [];
  const buffer = new ArrayBuffer(8);
  let finishRead;
  const file = {
    name: 'huge.csv',
    arrayBuffer() {
      events.push('read');
      return new Promise((resolve) => {
        finishRead = () => resolve(buffer);
      });
    },
  };

  const reading = readLocalFile(file, {
    onStart(name) {
      events.push(`loading:${name}`);
    },
  });

  assert.deepEqual(events, ['loading:huge.csv', 'read']);

  finishRead();
  assert.deepEqual(await reading, { name: 'huge.csv', buffer });
});

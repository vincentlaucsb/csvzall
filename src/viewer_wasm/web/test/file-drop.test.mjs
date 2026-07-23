import assert from 'node:assert/strict';
import test from 'node:test';

import {
  DROP_EXTENSION_MESSAGE,
  DROP_ONE_FILE_MESSAGE,
  isSupportedDroppedFileName,
  setupFileDrop,
  validateDroppedFiles,
} from '../src/file-drop.js';

class FakeTarget {
  listeners = new Map();

  addEventListener(type, listener) {
    this.listeners.set(type, listener);
  }

  removeEventListener(type) {
    this.listeners.delete(type);
  }

  async dispatch(type, event) {
    await this.listeners.get(type)?.(event);
  }
}

function fakeOverlay() {
  return {
    hidden: true,
    attributes: new Map(),
    setAttribute(name, value) {
      this.attributes.set(name, value);
    },
  };
}

function dragEvent(files = []) {
  return {
    defaultPrevented: false,
    dataTransfer: { files, types: ['Files'], dropEffect: 'none' },
    preventDefault() {
      this.defaultPrevented = true;
    },
  };
}

test('drop validation accepts only csv and tsv endings, case-insensitively', () => {
  assert.equal(isSupportedDroppedFileName('report.csv'), true);
  assert.equal(isSupportedDroppedFileName('REPORT.TSV'), true);
  assert.equal(isSupportedDroppedFileName('report.csv.txt'), false);
  assert.equal(isSupportedDroppedFileName('report'), false);
});

test('drop validation explains multiple and unsupported files clearly', () => {
  assert.deepEqual(validateDroppedFiles([]), { error: DROP_ONE_FILE_MESSAGE });
  assert.deepEqual(
    validateDroppedFiles([{ name: 'one.csv' }, { name: 'two.tsv' }]),
    { error: DROP_ONE_FILE_MESSAGE },
  );
  assert.deepEqual(
    validateDroppedFiles([{ name: 'notes.txt' }]),
    { error: DROP_EXTENSION_MESSAGE },
  );
});

test('file drag shows the overlay and a valid drop opens the file', async () => {
  const target = new FakeTarget();
  const overlay = fakeOverlay();
  const opened = [];
  const statuses = [];
  setupFileDrop({
    target,
    overlay,
    onFile(file) {
      opened.push(file.name);
    },
    onStatus(message) {
      statuses.push(message);
    },
  });

  const enter = dragEvent();
  await target.dispatch('dragenter', enter);
  assert.equal(enter.defaultPrevented, true);
  assert.equal(overlay.hidden, false);
  assert.equal(overlay.attributes.get('aria-hidden'), 'false');

  const file = { name: 'table.tsv' };
  const drop = dragEvent([file]);
  await target.dispatch('drop', drop);
  assert.equal(drop.defaultPrevented, true);
  assert.equal(overlay.hidden, true);
  assert.deepEqual(opened, ['table.tsv']);
  assert.deepEqual(statuses, []);
});

test('unsupported drops update status without opening a file', async () => {
  const target = new FakeTarget();
  const overlay = fakeOverlay();
  const opened = [];
  const statuses = [];
  setupFileDrop({
    target,
    overlay,
    onFile(file) {
      opened.push(file.name);
    },
    onStatus(message) {
      statuses.push(message);
    },
  });

  await target.dispatch('drop', dragEvent([{ name: 'table.xlsx' }]));
  assert.deepEqual(opened, []);
  assert.deepEqual(statuses, [DROP_EXTENSION_MESSAGE]);
});

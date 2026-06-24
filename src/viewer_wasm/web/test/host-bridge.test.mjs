import assert from 'node:assert/strict';
import test from 'node:test';
import { createHostBridge, VIEWER_SOURCE } from '../src/host-bridge.js';

function createFakeWindow({ embedded = true } = {}) {
  const listeners = new Map();
  const posts = [];
  const parent = {
    postMessage(message, targetOrigin, transfer) {
      posts.push({ message, targetOrigin, transfer });
    },
  };
  const windowRef = {
    parent: null,
    addEventListener(type, listener) {
      listeners.set(type, listener);
    },
    removeEventListener(type, listener) {
      if (listeners.get(type) === listener) {
        listeners.delete(type);
      }
    },
    dispatchMessage(data) {
      listeners.get('message')?.({ data });
    },
  };
  windowRef.parent = embedded ? parent : windowRef;
  return { windowRef, posts };
}

function csvBuffer(text = 'a,b\n1,2\n') {
  return new TextEncoder().encode(text).buffer;
}

test('standalone mode does not post ready and leaves save to browser download', async () => {
  const { windowRef, posts } = createFakeWindow({ embedded: false });
  const bridge = createHostBridge({
    windowRef,
    onOpenFile() {
      throw new Error('standalone mode should not receive host files');
    },
  });

  bridge.start();
  await bridge.markReady();

  const buffer = csvBuffer();
  const handled = await bridge.saveFile({
    name: 'local.csv',
    result: { buffer, byteOffset: 0, byteLength: buffer.byteLength },
  });

  assert.equal(handled, false);
  assert.equal(posts.length, 0);
  assert.equal(bridge.isHostMode(), false);
});

test('early host open-file is queued until ready and then opened', async () => {
  const { windowRef, posts } = createFakeWindow();
  const opened = [];
  const modes = [];
  const bridge = createHostBridge({
    windowRef,
    onOpenFile(file) {
      opened.push(file);
    },
    onHostModeChange(enabled) {
      modes.push(enabled);
    },
  });

  bridge.start();
  const buffer = csvBuffer('name\nAda\n');
  windowRef.dispatchMessage({
    source: 'obsidian-csvzall',
    type: 'open-file',
    name: 'people.csv',
    buffer,
  });

  assert.equal(opened.length, 0);
  assert.equal(bridge.isHostMode(), true);
  assert.deepEqual(modes, [true]);

  await bridge.markReady();

  assert.equal(opened.length, 1);
  assert.equal(opened[0].name, 'people.csv');
  assert.equal(opened[0].buffer, buffer);
  assert.equal(posts[0].message.source, VIEWER_SOURCE);
  assert.equal(posts[0].message.type, 'ready');
});

test('host save posts bytes back with a transferable ArrayBuffer', async () => {
  const { windowRef, posts } = createFakeWindow();
  const bridge = createHostBridge({
    windowRef,
    onOpenFile() {},
  });

  bridge.start();
  windowRef.dispatchMessage({
    source: 'obsidian-csvzall',
    type: 'open-file',
    name: 'input.csv',
    buffer: csvBuffer(),
  });
  await bridge.markReady();

  const buffer = csvBuffer('x\n3\n');
  const handled = await bridge.saveFile({
    name: 'input.csv',
    result: { buffer, byteOffset: 0, byteLength: buffer.byteLength },
  });

  const savePost = posts.find((post) => post.message.type === 'save-file');
  assert.equal(handled, true);
  assert.equal(savePost.message.source, VIEWER_SOURCE);
  assert.equal(savePost.message.name, 'input.csv');
  assert.equal(savePost.message.buffer, buffer);
  assert.equal(savePost.message.byteOffset, 0);
  assert.equal(savePost.message.byteLength, buffer.byteLength);
  assert.deepEqual(savePost.transfer, [buffer]);
});

test('dirty-state messages fire only when the host-visible dirty flag changes', async () => {
  const { windowRef, posts } = createFakeWindow();
  const bridge = createHostBridge({
    windowRef,
    onOpenFile() {},
  });

  bridge.start();
  windowRef.dispatchMessage({
    source: 'obsidian-csvzall',
    type: 'open-file',
    name: 'input.csv',
    buffer: csvBuffer(),
  });
  await bridge.markReady();

  bridge.emitDirtyState(false);
  bridge.emitDirtyState(false);
  bridge.emitDirtyState(true);
  bridge.emitDirtyState(true);
  bridge.emitDirtyState(false);

  const dirtyMessages = posts
    .map((post) => post.message)
    .filter((message) => message.type === 'dirty-state');

  assert.deepEqual(dirtyMessages.map((message) => message.dirty), [false, true, false]);
  assert(dirtyMessages.every((message) => message.source === VIEWER_SOURCE));
});

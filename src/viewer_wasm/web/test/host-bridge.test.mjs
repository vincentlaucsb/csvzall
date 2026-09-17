import assert from 'node:assert/strict';
import test from 'node:test';
import { createHostBridge, HOST_CAPABILITIES, VIEWER_SOURCE } from '../src/host-bridge.js';

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
    dispatchMessage(data, source = parent) {
      listeners.get('message')?.({ data, source });
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
  assert.deepEqual(posts[0].message.capabilities, HOST_CAPABILITIES);
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
  const saving = bridge.saveFile({
    name: 'input.csv',
    result: { buffer, byteOffset: 0, byteLength: buffer.byteLength },
  });

  const savePost = posts.find((post) => post.message.type === 'save-file');
  windowRef.dispatchMessage({ source: 'obsidian-csvzall', type: 'save-result', requestId: savePost.message.requestId, success: true });
  const handled = await saving;
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

test('save acknowledgements require the parent and matching request; failures propagate', async () => {
  const { windowRef, posts } = createFakeWindow();
  const bridge = createHostBridge({ windowRef, onOpenFile() {} });
  bridge.start();
  windowRef.dispatchMessage({ source: 'obsidian-csvzall', type: 'open-file', name: 'a.csv', buffer: csvBuffer() });
  await bridge.markReady();
  let completed = false;
  const save = bridge.saveFile({ name: 'a.csv', result: { buffer: csvBuffer() } }).then(value => { completed = true; return value; });
  const requestId = posts.at(-1).message.requestId;
  const ack = { source: 'obsidian-csvzall', type: 'save-result', requestId, success: true };
  windowRef.dispatchMessage(ack, {});
  windowRef.dispatchMessage({ ...ack, requestId: requestId + 1 });
  windowRef.dispatchMessage({ ...ack, success: 'true' });
  await Promise.resolve();
  assert.equal(completed, false);
  windowRef.dispatchMessage(ack);
  assert.equal(await save, true);
  const failed = bridge.saveFile({ name: 'a.csv', result: { buffer: csvBuffer() } });
  const failure = assert.rejects(failed, /disk full/);
  windowRef.dispatchMessage({ ...ack, requestId: posts.at(-1).message.requestId, success: false, error: 'disk full' });
  await failure;
  const pending = bridge.saveFile({ name: 'a.csv', result: { buffer: csvBuffer() } });
  const destroyed = assert.rejects(pending, /closed before save/);
  bridge.destroy();
  await destroyed;
});

test('only the parent can open host files or request host viewport refreshes', async () => {
  const { windowRef } = createFakeWindow();
  let resized = 0;
  const bridge = createHostBridge({ windowRef, onOpenFile() {}, onViewportResize() { resized++; } });
  bridge.start();
  const open = { source: 'obsidian-csvzall', type: 'open-file', name: 'a.csv', buffer: csvBuffer() };
  windowRef.dispatchMessage(open, {});
  assert.equal(bridge.isHostMode(), false);
  const resize = { source: 'obsidian-csvzall', type: 'viewport-resized' };
  windowRef.dispatchMessage(resize);
  assert.equal(resized, 0);
  windowRef.dispatchMessage(open);
  windowRef.dispatchMessage(resize, {});
  windowRef.dispatchMessage(resize);
  assert.equal(resized, 1);
});

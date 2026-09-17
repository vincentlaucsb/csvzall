import test from 'node:test';
import assert from 'node:assert/strict';
import { installDialogDismiss, installDialogDismissals } from '../src/viewer/modules/dialog-dismiss.mjs';

function harness() {
  const dialog = new EventTarget();
  const attrs = new Set();
  Object.assign(dialog, {
    open: true,
    ownerDocument: { defaultView: { Event } },
    hasAttribute: name => attrs.has(name),
    setAttribute: name => attrs.add(name),
    removeAttribute: name => attrs.delete(name),
    getBoundingClientRect: () => ({ left: 100, top: 100, right: 300, bottom: 300 }),
    close(value) { this.open = false; this.returnValue = value; this.dispatchEvent(new Event('close')); },
  });
  const pointer = (type, x, y, button = 0) => {
    const event = new Event(type);
    Object.assign(event, { clientX: x, clientY: y, button, isPrimary: true });
    dialog.dispatchEvent(event);
  };
  return { dialog, pointer };
}

test('backdrop press and click cancel the dialog and notify close listeners', () => {
  const { dialog, pointer } = harness();
  installDialogDismiss(dialog);
  let closed = 0;
  dialog.addEventListener('close', () => closed++);
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  assert.equal(dialog.open, false);
  assert.equal(dialog.returnValue, 'cancel');
  assert.equal(closed, 1);
});

test('padding clicks, inside-to-outside drags, outside-to-inside drags, right clicks and canceled pointers stay open', () => {
  const { dialog, pointer } = harness();
  installDialogDismiss(dialog);
  for (const [start, end, button] of [[150, 150, 0], [150, 50, 0], [50, 150, 0], [50, 50, 2]]) {
    pointer('pointerdown', start, start, button); pointer('click', end, end, button);
    assert.equal(dialog.open, true);
  }
  pointer('pointerdown', 50, 50); pointer('pointercancel', 50, 50); pointer('click', 50, 50);
  assert.equal(dialog.open, true);
});

test('dismissal respects prevented cancellation, installation is idempotent, and cleanup removes it', () => {
  const { dialog, pointer } = harness();
  const cleanup = installDialogDismiss(dialog);
  installDialogDismiss(dialog);
  let canceled = 0;
  dialog.addEventListener('cancel', event => { canceled++; event.preventDefault(); });
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  assert.equal(dialog.open, true);
  assert.equal(canceled, 1);
  cleanup();
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  assert.equal(canceled, 1);
});

test('only ordinary dialogs opt into backdrop dismissal', () => {
  const { dialog, pointer } = harness();
  const cleanup = installDialogDismissals({ querySelectorAll(selector) {
    assert.equal(selector, 'dialog.csvzall-dialog');
    return [dialog];
  } });
  cleanup();
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  assert.equal(dialog.open, true);
});

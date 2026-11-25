import { applyCsvOptionsHostUi, updateHeaderToggle } from '../src/viewer/modules/csv-options.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { createDialog } from '../src/viewer/modules/dialog.mjs';

test('CSV options are hidden only for mobile hosts and close an already-open dialog', () => {
  const button = { hidden: false };
  const dialog = { open: true, close() { this.open = false; } };
  applyCsvOptionsHostUi(button, dialog, true, { mobile: true });
  assert.equal(button.hidden, true);
  assert.equal(dialog.open, false);
  applyCsvOptionsHostUi(button, dialog, true, { mobile: false });
  assert.equal(button.hidden, false);
  applyCsvOptionsHostUi(button, dialog, true);
  assert.equal(button.hidden, false);
  applyCsvOptionsHostUi(button, dialog, false, { mobile: true });
  assert.equal(button.hidden, false);
});

function harness() {
  const dialog = new EventTarget();
  Object.assign(dialog, {
    open: false,
    ownerDocument: { defaultView: { Event } },
    getBoundingClientRect: () => ({ left: 100, top: 100, right: 300, bottom: 300 }),
    showModal() { this.open = true; },
    close(value) {
      this.open = false;
      if (value !== undefined) this.returnValue = value;
      this.dispatchEvent(new Event('close'));
    },
  });
  const pointer = (type, x, y, button = 0) => {
    const event = new Event(type);
    Object.assign(event, { clientX: x, clientY: y, button, isPrimary: true });
    dialog.dispatchEvent(event);
  };
  const escape = () => {
    if (dialog.dispatchEvent(new Event('cancel', { cancelable: true }))) dialog.close();
  };
  return { dialog, pointer, escape };
}

test('backdrop press and click cancel the dialog and notify close listeners', () => {
  const { dialog, pointer } = harness();
  createDialog(dialog).show();
  let closed = 0;
  dialog.addEventListener('close', () => closed++);
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  assert.equal(dialog.open, false);
  assert.equal(dialog.returnValue, 'cancel');
  assert.equal(closed, 1);
});

test('padding clicks, inside-to-outside drags, outside-to-inside drags, right clicks and canceled pointers stay open', () => {
  const { dialog, pointer } = harness();
  createDialog(dialog).show();
  for (const [start, end, button] of [[150, 150, 0], [150, 50, 0], [50, 150, 0], [50, 50, 2]]) {
    pointer('pointerdown', start, start, button); pointer('click', end, end, button);
    assert.equal(dialog.open, true);
  }
  pointer('pointerdown', 50, 50); pointer('pointercancel', 50, 50); pointer('click', 50, 50);
  assert.equal(dialog.open, true);
});

test('dismissal respects prevented cancellation and destroy closes the dialog and removes listeners', () => {
  const { dialog, pointer } = harness();
  const modal = createDialog(dialog);
  modal.show();
  let canceled = 0;
  dialog.addEventListener('cancel', event => { canceled++; event.preventDefault(); });
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  assert.equal(dialog.open, true);
  assert.equal(canceled, 1);
  modal.destroy();
  assert.equal(dialog.open, false);
  dialog.showModal();
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  assert.equal(canceled, 1);
});

test('progress dialogs stay open on Escape and backdrop click', () => {
  const { dialog, pointer, escape } = harness();
  const modal = createDialog(dialog, { dismissible: false });
  modal.show();
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  escape();
  assert.equal(dialog.open, true);
  modal.close();
  assert.equal(dialog.open, false);
});

test('Escape returns Cancel after a previous explicit choice', async () => {
  const { dialog, pointer, escape } = harness();
  const modal = createDialog(dialog);
  let focusCount = 0;
  const first = modal.showForResult({ focus() { focusCount++; } });
  modal.close('save');
  assert.equal(await first, 'save');
  assert.equal(focusCount, 1);
  const second = modal.showForResult();
  escape();
  assert.equal(await second, 'cancel');
  const third = modal.showForResult();
  pointer('pointerdown', 50, 50); pointer('click', 50, 50);
  assert.equal(await third, 'cancel');
});


test('variable-width files disable and uncheck headers with remediation; corrected files reset the control', () => {
  const checkbox = { checked: true, disabled: false };
  const message = { textContent: '' };
  const banner = { hidden: true };
  const reason = 'Headers are disabled. Add missing header names, then save and reopen.';
  updateHeaderToggle(checkbox, message, { hasHeaders: true, headersDisabledReason: reason }, banner);
  assert.equal(checkbox.checked, false);
  assert.equal(checkbox.disabled, true);
  assert.equal(banner.hidden, false);
  assert.equal(message.textContent, reason);
  updateHeaderToggle(checkbox, message, { hasHeaders: false }, banner);
  assert.equal(checkbox.disabled, false);
  assert.equal(checkbox.checked, false);
  assert.equal(banner.hidden, true);
  assert.equal(message.textContent, '');
  updateHeaderToggle(checkbox, message, { hasHeaders: true }, banner);
  assert.equal(checkbox.checked, true);
});

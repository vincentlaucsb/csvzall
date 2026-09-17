import test from 'node:test';
import assert from 'node:assert/strict';
import { DropdownMenu } from '../vendor/popright/dist/DropdownMenu.js';

class ElementStub {
  listeners = new Map();
  constructor(ownerDocument, parent = null) { this.ownerDocument = ownerDocument; this.parent = parent; }
  addEventListener(type, callback) { this.listeners.set(type, callback); }
  removeEventListener(type) { this.listeners.delete(type); }
  contains(node) { for (; node; node = node.parent) if (node === this) return true; return false; }
}

test('vendored dropdown keeps trigger pointer/focus events for the closing click', () => {
  const originalNode = globalThis.Node;
  const originalElement = globalThis.Element;
  globalThis.Node = globalThis.Element = ElementStub;
  try {
    const doc = new ElementStub();
    const trigger = new ElementStub(doc);
    const triggerIcon = new ElementStub(doc, trigger);
    const root = new ElementStub(doc);
    // Exercise the package's real event handlers without depending on DOM layout.
    const menu = Object.create(DropdownMenu.prototype);
    const closes = [];
    let reopens = 0;
    Object.assign(menu, { root, targets: [trigger], currentContext: { target: trigger },
      options: { trigger: 'click', closeOnBlur: true }, targetCleanups: [], globalCleanups: [],
      close(reason) { closes.push(reason); this.root = null; },
      requestOpen() { reopens++; },
    });
    menu.attachTargets();
    menu.attachGlobalListeners();
    doc.listeners.get('pointerdown')({ target: triggerIcon });
    root.listeners.get('focusout')({ relatedTarget: trigger });
    assert.equal(menu.isOpen, true);
    trigger.listeners.get('click')({ target: triggerIcon, currentTarget: trigger });
    assert.equal(menu.isOpen, false);
    assert.deepEqual(closes, ['manual']);
    assert.equal(reopens, 0);

    trigger.listeners.get('click')({ target: triggerIcon, currentTarget: trigger });
    assert.equal(reopens, 1);
    menu.root = root;
    doc.listeners.get('pointerdown')({ target: new ElementStub(doc) });
    assert.equal(menu.isOpen, false);
    assert.deepEqual(closes, ['manual', 'outside-pointer']);
    for (const cleanup of [...menu.targetCleanups, ...menu.globalCleanups]) cleanup();
  } finally {
    globalThis.Node = originalNode;
    globalThis.Element = originalElement;
  }
});

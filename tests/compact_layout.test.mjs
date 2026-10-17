import test from 'node:test';
import assert from 'node:assert/strict';
import { compactViewport, createCompactLayout } from '../src/viewer/modules/compact-layout.mjs';

function fixture(width = 300, height = 800) {
  const frames = new Map(), timers = new Map(), listeners = new Map(), attributes = new Set();
  let next = 0, menuOptions;
  const button = (id, width, text = id) => ({ id, hidden: false, disabled: false, textContent: text, attrs: new Set(), clicks: 0,
    getAttribute(name) { return name === 'aria-label' ? text : null; },
    setAttribute(name) { this.attrs.add(name); }, removeAttribute(name) { this.attrs.delete(name); },
    closest() { return this.hidden || this.group.hidden ? this : null; },
    getBoundingClientRect() { return { width: this.hidden || this.attrs.has('data-overflowed') ? 0 : width }; },
    click() { if (!this.disabled) this.clicks++; },
  });
  const insert = button('insert-menu', 100, 'Insert'), remove = button('delete-menu', 105, 'Delete');
  const reset = button('reset', 80, 'Reset'), save = button('save', 75, 'Save'), more = button('more-actions', 32);
  const group = children => {
    const result = { children, hidden: false, getBoundingClientRect() {
      const visible = children.map(b => b.getBoundingClientRect().width).filter(Boolean);
      return { width: this.hidden ? 0 : visible.reduce((a, b) => a + b, 0) + Math.max(visible.length - 1, 0) * 5 };
    } };
    for (const child of children) child.group = result;
    return result;
  };
  const edits = group([insert, remove, reset, save]), overflow = group([more]);
  const all = [insert, remove, reset, save, more];
  const toolbar = { children: [edits, overflow], clientWidth: width,
    ownerDocument: { body: { toggleAttribute(name, enabled) { if (enabled) attributes.add(name); else attributes.delete(name); } } },
    querySelectorAll(selector) { return selector === '[data-overflowed]' ? all.filter(b => b.attrs.has('data-overflowed')) : all.filter(b => b !== more); },
  };
  const windowRef = { innerWidth: width, innerHeight: height,
    getComputedStyle: () => ({ columnGap: '5px' }),
    requestAnimationFrame(fn) { frames.set(++next, fn); return next; }, cancelAnimationFrame(id) { frames.delete(id); },
    setTimeout(fn) { timers.set(++next, fn); return next; }, clearTimeout(id) { timers.delete(id); },
    addEventListener: (type, fn) => listeners.set(type, fn), removeEventListener: type => listeners.delete(type),
    ResizeObserver: class { observe() {} disconnect() {} },
    MutationObserver: class { observe() {} disconnect() {} },
  };
  const menu = { isOpen: false, update(value) { Object.assign(menuOptions, value); }, destroy() { this.destroyed = true; } };
  const dropdowns = new Map([[remove, { items: [{ id: 'delete-row', label: 'Row' }], onSelect() { remove.click(); } }]]);
  const layout = createCompactLayout({ windowRef, toolbar, moreButton: more, dropdowns,
    createMenu(_button, items) { menuOptions = { items }; dropdowns.set(more, { menu }); return menu; } });
  const flush = jobs => { const pending = [...jobs.values()]; jobs.clear(); for (const fn of pending) fn(); };
  return { layout, windowRef, toolbar, all, attributes, menu, more, save, reset, insert, remove, edits, dropdowns,
    open: root => menuOptions.onOpen({ root }),
    items: () => menuOptions.items(), frames: () => flush(frames), timers: () => flush(timers),
    resize(width, height) { Object.assign(windowRef, { innerWidth: width, innerHeight: height }); toolbar.clientWidth = width; listeners.get('resize')(); flush(frames); } };
}

test('portrait and landscape compact boundaries include 600 CSS pixels', () => {
  for (const [w, h, compact] of [[600, 900, true], [601, 900, false], [1000, 600, true], [1000, 601, false], [600, 600, true]]) {
    assert.equal(compactViewport(w, h), compact);
  }
});

test('overflow preserves Save, live disabled states and the original Insert/Delete actions', () => {
  const f = fixture();
  assert.equal(f.save.attrs.has('data-overflowed'), false);
  assert.deepEqual(f.items().map(item => item.id), ['delete-menu', 'reset']);
  const deletion = f.items()[0];
  assert.equal(deletion.type, 'submenu');
  deletion.items[0].onSelect({ close() {} });
  assert.equal(f.remove.clicks, 1);
  f.remove.disabled = true;
  assert.equal(f.items()[0].disabled, true);
  deletion.items[0].onSelect({ close() {} });
  assert.equal(f.remove.clicks, 1);
  f.reset.disabled = true;
  assert.equal(f.items()[1].disabled, true);
  f.items()[1].onSelect({ close() {} });
  assert.equal(f.reset.clicks, 0);
  f.edits.hidden = true;
  assert.deepEqual(f.items(), []);
  f.layout.destroy();
});

test('keyboard resizing and open menus defer reflow until editing and menu interaction finish', () => {
  const f = fixture(900, 700);
  assert.equal(f.layout.compact, false);
  f.layout.onCellEditingStarted();
  f.resize(900, 350);
  assert.equal(f.layout.compact, false);
  f.layout.onCellEditingStopped();
  f.layout.onCellEditingStarted(); // A late stop must not unfreeze the next edit.
  f.timers(); f.frames();
  assert.equal(f.layout.compact, false);
  f.layout.onCellEditingStopped(); f.timers(); f.frames();
  assert.equal(f.layout.compact, true);
  f.menu.isOpen = true;
  f.resize(900, 700);
  assert.equal(f.layout.compact, true);
  f.menu.isOpen = false; f.layout.refresh(); f.frames();
  assert.equal(f.layout.compact, false);
  assert.equal(f.more.hidden, true);
  assert(f.all.every(button => !button.attrs.has('data-overflowed')));
  f.layout.destroy();
  assert.equal(f.menu.destroyed, true);
});

test('compact submenus require explicit activation before overlapping a mouse or touch pointer', () => {
  const f = fixture(), handlers = new Map();
  f.open({ addEventListener(type, handler, capture) { assert.equal(capture, true); handlers.set(type, handler); } });
  for (const type of ['pointerenter', 'pointermove']) {
    for (const pointerType of ['touch', 'mouse', 'pen']) {
      let stopped = false;
      handlers.get(type)({ pointerType, stopImmediatePropagation() { stopped = true; } });
      assert.equal(stopped, true);
    }
  }
  assert.equal(handlers.has('pointerdown'), false);
  assert.equal(handlers.has('click'), false);
  f.layout.destroy();
});

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { mountTableForm, tableQuery } from '../src/viewer/modules/table-form.mjs';

class Element {
  constructor(tag, doc) {
    this.tag = tag; this.ownerDocument = doc; this.children = []; this.listeners = new Map();
    this.classList = { add() {} }; this.value = ''; this.textContent = ''; this.checked = false;
  }
  appendChild(child) { child.parentElement = this; this.children.push(child); return child; }
  replaceChildren() { this.children = []; }
  setAttribute(key, value) { this[key] = value; }
  addEventListener(type, handler) { this.listeners.set(type, [...(this.listeners.get(type) || []), handler]); }
  removeEventListener(type, handler) { this.listeners.set(type, this.listeners.get(type).filter(fn => fn !== handler)); }
  dispatchEvent(event) {
    this.listeners.get(event.type)?.forEach(fn => fn(event));
    this.parentElement?.dispatchEvent(event);
  }
  querySelectorAll(tag) { return this.children.flatMap(child => [...(child.tag === tag ? [child] : []), ...child.querySelectorAll(tag)]); }
}
function fixture(options = {}) {
  const doc = { createElement(tag) { return new Element(tag, this); }, defaultView: { Event: class { constructor(type) { this.type = type; } } } };
  const root = doc.createElement('div');
  const form = mountTableForm(root, { preview: async () => 'preview', ...options });
  const field = title => {
    const label = root.querySelectorAll('label').find(label => label.children.some(child => child.tag === 'span' && child.textContent === title));
    return label.children.find(child => ['input', 'select', 'textarea'].includes(child.tag));
  };
  const button = title => root.querySelectorAll('button').find(button => button.textContent === title);
  const fire = (node, type = 'change') => node.dispatchEvent({ type });
  return { root, form, field, button, fire };
}
const settle = async () => { for (let i = 0; i < 8; ++i) await Promise.resolve(); };

test('shared table form switches between all, selected columns and SQL without stale options', () => {
  const f = fixture({ columns: ['title', 'priority'] });
  assert.deepEqual(f.form.getValue().columns, []);
  assert.equal(f.form.getValue().autoUpdate, true);
  f.field('Include').value = 'columns'; f.fire(f.field('Include'));
  assert.throws(() => f.form.getValue(), /at least one/);
  const check = f.root.querySelectorAll('input').find(input => input.value === 'priority');
  check.checked = true; f.fire(check);
  assert.deepEqual(f.form.getValue().columns, ['priority']);
  f.field('Include').value = 'sql'; f.fire(f.field('Include'));
  assert.throws(() => f.form.getValue(), /SQLite query/);
  f.field('SQLite query').value = 'SELECT title FROM data';
  assert.deepEqual(f.form.getValue().columns, []);
  f.form.setValue({ columns: ['title'], sql: '', autoUpdate: false });
  assert.equal(f.field('Include').value, 'columns');
  assert.deepEqual(f.form.getValue().columns, ['title']);
  assert.equal(f.form.getValue().autoUpdate, false);
  f.form.dispose();
  assert.equal(f.root.children.length, 0);
});

test('changing CSV source ignores stale column discovery and preserves initial selection', async () => {
  const resolvers = new Map();
  const f = fixture({ value: { source: 'a.csv', columns: ['title'] }, loadColumns: source => new Promise(resolve => resolvers.set(source, resolve)) });
  resolvers.get('a.csv')(['title', 'value']); await settle();
  assert.deepEqual(f.form.getValue().columns, ['title']);
  f.field('CSV source').value = 'b.csv'; f.fire(f.field('CSV source'));
  f.field('CSV source').value = 'c.csv'; f.fire(f.field('CSV source'));
  resolvers.get('c.csv')(['current']); await settle();
  resolvers.get('b.csv')(['old']); await settle();
  const names = f.root.querySelectorAll('input').filter(input => input.type === 'checkbox').map(input => input.value);
  assert.ok(names.includes('current')); assert.ok(!names.includes('old'));
});

test('late previews cannot display stale results after edits or form disposal', async () => {
  let finish;
  const f = fixture({ preview: () => new Promise(resolve => { finish = resolve; }) });
  f.fire(f.button('Preview'), 'click');
  f.field('SQLite query').value = 'new'; f.fire(f.field('SQLite query'), 'input');
  finish('stale'); await settle();
  assert.equal(f.root.querySelectorAll('pre')[0].hidden, true);
  f.fire(f.button('Preview'), 'click');
  f.form.dispose(); finish('late'); await settle();
  assert.equal(f.root.children.length, 0);
});

test('table queries quote column identifiers, accept CTEs and wrap results with a preview limit', () => {
  assert.equal(tableQuery({ columns: ['a"b'], sql: '' }, 20), 'SELECT * FROM (\nSELECT data."a""b" FROM data\n) AS csvzall_table LIMIT 20');
  assert.equal(tableQuery({ sql: 'WITH x AS (SELECT * FROM data) SELECT * FROM x;', columns: [] }), 'SELECT * FROM (\nWITH x AS (SELECT * FROM data) SELECT * FROM x\n) AS csvzall_table');
  assert.equal(tableQuery({ sql: "SELECT ';' AS label; -- trailing ; comment", columns: [] }), "SELECT * FROM (\nSELECT ';' AS label -- trailing ; comment\n) AS csvzall_table");
  assert.equal(tableQuery({ sql: 'SELECT title FROM data; /* trailing ; comment */', columns: [] }), 'SELECT * FROM (\nSELECT title FROM data /* trailing ; comment */\n) AS csvzall_table');
  assert.equal(tableQuery({ sql: "SELECT 'it''s;' AS label;", columns: [] }), "SELECT * FROM (\nSELECT 'it''s;' AS label\n) AS csvzall_table");
});

test('closing a shared form aborts a pending submission', async () => {
  let signal;
  let finish;
  const f = fixture({ submit: async (_value, cancellation) => { signal = cancellation; await new Promise(resolve => { finish = resolve; }); } });
  f.fire(f.button('Insert table'), 'click');
  assert.equal(signal.aborted, false);
  f.form.dispose();
  assert.equal(signal.aborted, true);
  finish(); await settle();
  assert.equal(f.root.children.length, 0);
});


test('export format is opt-in and participates in form state and stale preview protection', async () => {
  const native = fixture({ value: { format: 'value' } });
  assert.equal(native.field('Insert as').parentElement.hidden, true);
  assert.equal(native.form.getValue().format, 'table');
  const f = fixture({ showFormat: true, value: { format: 'value', sql: 'SELECT COUNT(*) FROM data' } });
  assert.equal(f.form.getValue().format, 'value'); assert.equal(f.field('Insert as').parentElement.hidden, false);
  f.form.setValue({ format: 'table' }); assert.equal(f.form.getValue().format, 'table');
  let finish; const pending = fixture({ showFormat: true, preview: () => new Promise(resolve => { finish = resolve; }) });
  pending.fire(pending.button('Preview'), 'click');
  pending.field('Insert as').value = 'value'; pending.fire(pending.field('Insert as'));
  finish('stale'); await settle(); assert.equal(pending.root.querySelectorAll('pre')[0].hidden, true);
});

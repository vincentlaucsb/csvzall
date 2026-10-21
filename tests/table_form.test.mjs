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
  closest(tag) { return this.tag === tag ? this : this.parentElement?.closest(tag); }
  focus() { this.ownerDocument.activeElement = this; }
  setSelectionRange(start, end) { this.selectionStart = start; this.selectionEnd = end; }
  setRangeText(text, start, end) {
    this.value = this.value.slice(0, start) + text + this.value.slice(end);
    this.setSelectionRange(start + text.length, start + text.length);
  }
  addEventListener(type, handler) { this.listeners.set(type, [...(this.listeners.get(type) || []), handler]); }
  removeEventListener(type, handler) { this.listeners.set(type, this.listeners.get(type).filter(fn => fn !== handler)); }
  dispatchEvent(event) {
    event.target ??= this;
    event.stopPropagation ??= () => { event.cancelBubble = true; };
    this.listeners.get(event.type)?.forEach(fn => fn(event));
    if (!event.cancelBubble) this.parentElement?.dispatchEvent(event);
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
  const picker = root.querySelectorAll('fieldset').find(node => node.className === 'csvzall-query-picker');
  return { root, form, field, button, fire, picker };
}
const settle = async () => { for (let i = 0; i < 8; ++i) await Promise.resolve(); };

test('syntax choice is opt-in, value exports force comment syntax, and paused exports retain their query', () => {
  const f = fixture({ showFormat: true, showSyntax: true, value: { syntax: 'code', sql: 'SELECT 1', autoUpdate: false } });
  assert.equal(f.form.getValue().syntax, 'code');
  assert.equal(f.form.getValue().autoUpdate, false);
  f.field('Insert as').value = 'value'; f.fire(f.field('Insert as'));
  assert.equal(f.form.getValue().syntax, 'comment');
  assert.equal(f.field('Note syntax').disabled, true);
  assert.equal(f.form.getValue().sql, 'SELECT 1');
  f.form.dispose();
});

test('SQL column buttons start an empty query and quote spaces, reserved words, quotes and Unicode', () => {
  for (const [name, quoted] of [['Customer name', '"Customer name"'], ['select', '"select"'], ['a"b', '"a""b"'], ['日本語 😀', '"日本語 😀"']]) {
    let previews = 0, changes = 0;
    const f = fixture({ columns: [name], preview: async () => { ++previews; }, onChange: () => { ++changes; } });
    assert.equal(f.picker.hidden, true);
    f.field('Include').value = 'sql'; f.fire(f.field('Include'));
    assert.equal(f.picker.hidden, false);
    const query = f.field('SQLite query'); query.value = ' \n ';
    const before = changes;
    f.fire(f.button(name), 'click');
    assert.equal(f.form.getValue().sql, `SELECT ${quoted} FROM data`);
    assert.equal(query.selectionStart, query.value.length);
    assert.equal(query.selectionEnd, query.value.length);
    assert.equal(query.ownerDocument.activeElement, query);
    assert.equal(changes, before + 1);
    assert.equal(previews, 0);
  }
});

test('SQL column buttons insert at the cursor or replace a selection without changing surrounding SQL', () => {
  const f = fixture({ columns: ['a"b'], value: { sql: 'SELECT  FROM data WHERE active = 1' } });
  const query = f.field('SQLite query'); query.setSelectionRange(7, 7);
  f.fire(f.button('a"b'), 'click');
  assert.equal(query.value, 'SELECT "a""b" FROM data WHERE active = 1');
  assert.equal(query.selectionStart, 13);
  query.value = 'SELECT old FROM data'; query.setSelectionRange(7, 10);
  f.fire(f.button('a"b'), 'click');
  assert.equal(query.value, 'SELECT "a""b" FROM data');
  query.setSelectionRange(query.value.length, query.value.length);
  f.fire(f.button('a"b'), 'click');
  assert.equal(query.value, 'SELECT "a""b" FROM data"a""b"');
});

test('SQL picker updates after column discovery and exposes a compact searchable list', async () => {
  let finish, changes = 0;
  const f = fixture({ value: { source: 'a.csv', sql: 'SELECT * FROM data' }, onChange: () => { ++changes; }, loadColumns: () => new Promise(resolve => { finish = resolve; }) });
  assert.equal(f.picker.querySelectorAll('button').filter(button => !button.hidden).length, 0);
  finish(['one', 'two', 'three', 'four', 'five', 'six', 'Seventh', 'a"b']); await settle();
  assert.equal(f.button('Seventh'), undefined);
  f.fire(f.button('Show all 8 columns'), 'click');
  assert.ok(f.button('Seventh'));
  const search = f.picker.querySelectorAll('input')[0]; search.value = 'SEV';
  const before = changes;
  f.fire(search, 'input');
  assert.equal(changes, before);
  assert.ok(f.button('Seventh')); assert.equal(f.button('one'), undefined);
  let prevented = false;
  search.dispatchEvent({ type: 'keydown', key: 'Enter', preventDefault() { prevented = true; } });
  assert.equal(prevented, true);
  assert.equal(f.field('SQLite query').value, 'SELECT * FROM data');
  f.fire(f.button('Show fewer columns'), 'click');
  assert.ok(f.button('one')); assert.equal(f.button('Seventh'), undefined);
  f.form.setColumns(['new']);
  assert.equal(f.button('one'), undefined); assert.ok(f.button('new'));
});

test('inserting a column invalidates pending previews and remains disabled during submission', async () => {
  let finish;
  const f = fixture({ columns: ['name'], value: { sql: 'SELECT  FROM data' }, preview: () => new Promise(resolve => { finish = resolve; }) });
  f.fire(f.button('Preview'), 'click');
  f.field('SQLite query').setSelectionRange(7, 7); f.fire(f.button('name'), 'click');
  finish('stale'); await settle();
  assert.equal(f.root.querySelectorAll('pre')[0].hidden, true);
  const pending = fixture({ columns: ['name'], value: { sql: 'SELECT * FROM data' }, submit: () => new Promise(resolve => { finish = resolve; }) });
  pending.fire(pending.button('Insert table'), 'click');
  assert.equal(pending.button('name').disabled, true);
  pending.fire(pending.button('name'), 'click');
  assert.equal(pending.field('SQLite query').value, 'SELECT * FROM data');
  finish(); await settle(); assert.equal(pending.button('name').disabled, false);
});

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

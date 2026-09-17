import test from 'node:test';
import assert from 'node:assert/strict';
import { installHostTheme, parseHostTheme } from '../src/viewer/modules/host-theme.mjs';

const message = (mode = 'dark', variables = { '--background-primary': '#123456', '--text-normal': '#abcdef' }) => ({
  source: 'obsidian-csvzall', type: 'theme', version: 1, mode, variables,
});

test('host theme parser rejects invalid protocols and values and ignores arbitrary properties', () => {
  for (const data of [null, {}, { ...message(), version: 2 }, { ...message(), mode: 'system' },
    { ...message(), variables: [] }, message('dark', { '--text-normal': 42 }),
    message('dark', { '--background-primary': 'url(https://example.com)' }),
    message('dark', { '--text-normal': 'red; display:none' })]) assert.equal(parseHostTheme(data), null);
  assert.deepEqual(parseHostTheme(message('light', { '--text-normal': ' red ', '--untrusted': 'url(x)' })), {
    mode: 'light', variables: { '--text-normal': 'red' },
  });
});

function harness(family = 'alpine', embedded = true) {
  const listeners = new Map();
  const properties = new Map();
  const classes = new Set([`ag-theme-${family}-auto-dark`]);
  const styles = [];
  const posts = [];
  const root = { dataset: {}, style: {
    setProperty: (name, value) => properties.set(name, value),
    removeProperty: name => properties.delete(name),
  } };
  const grid = { classList: {
    contains: name => classes.has(name), add: name => classes.add(name), remove: name => classes.delete(name),
    toggle: (name, on) => on ? classes.add(name) : classes.delete(name),
  } };
  const windowRef = {
    parent: { postMessage: message => posts.push(message) },
    document: { documentElement: root, head: { append: style => styles.push(style) },
      createElement: () => ({}), getElementById: () => grid },
    addEventListener: (type, listener) => listeners.set(type, listener),
    removeEventListener: type => listeners.delete(type),
  };
  if (!embedded) windowRef.parent = windowRef;
  const dispatch = (data, source = windowRef.parent) => listeners.get('message')?.({ data, source });
  return { windowRef, root, properties, classes, styles, posts, listeners, dispatch };
}

for (const family of ['alpine', 'quartz']) {
  test(`${family} applies live host colors/mode without replacing grid or editor DOM`, () => {
    const h = harness(family);
    const dispose = installHostTheme(h.windowRef);
    assert.deepEqual(h.posts, [{ source: 'csvzall-viewer', type: 'theme-ready', version: 1 }]);
    h.dispatch(message(), {});
    h.dispatch({ ...message(), version: 2 });
    assert.equal(h.properties.size, 0);
    assert.equal(h.styles.length, 0);
    h.dispatch(message());
    assert.equal(h.properties.get('--background-primary'), '#123456');
    assert.equal(h.properties.get('color-scheme'), 'dark');
    assert.deepEqual([...h.classes], [`ag-theme-${family}`, `ag-theme-${family}-dark`]);
    h.dispatch(message('light', { '--text-normal': '#222' }));
    assert.equal(h.properties.has('--background-primary'), false);
    assert.equal(h.properties.get('--text-normal'), '#222');
    assert.equal(h.properties.get('color-scheme'), 'light');
    assert.deepEqual([...h.classes], [`ag-theme-${family}`]);
    assert.equal(h.root.dataset.csvzallHostTheme, 'light');
    assert.equal(h.styles.length, 1);
    dispose();
    assert.equal(h.listeners.size, 0);
  });
}

test('standalone viewer keeps system theme and installs no listener or stylesheet', () => {
  const h = harness('quartz', false);
  installHostTheme(h.windowRef)();
  assert.equal(h.listeners.size, 0);
  assert.equal(h.posts.length, 0);
  assert.equal(h.styles.length, 0);
  assert.equal(h.properties.size, 0);
  assert.deepEqual([...h.classes], ['ag-theme-quartz-auto-dark']);
});

import assert from 'node:assert/strict';
import test from 'node:test';
import { createHostGridAdapter } from '../src/host-grid.js';

function fixture(hosted = true) {
  const jobs = new Map();
  let id = 0;
  const calls = [];
  const grid = {
    getFocusedCell: () => ({ rowIndex: 99, column: 'focused' }),
    ensureIndexVisible: (...args) => calls.push(['row', ...args]),
    ensureColumnVisible: (...args) => calls.push(['column', ...args]),
  };
  const windowRef = {
    Event,
    dispatchEvent: event => calls.push(['event', event.type]),
    setTimeout(fn, delay) { jobs.set(++id, { fn, delay }); return id; },
    clearTimeout(id) { jobs.delete(id); },
  };
  const adapter = createHostGridAdapter({ windowRef, getGrid: () => grid, isHostMode: () => hosted });
  const flush = () => { const current = [...jobs]; jobs.clear(); for (const [, job] of current.sort((a,b) => a[1].delay-b[1].delay)) job.fn(); };
  return { adapter, calls, jobs, flush, grid };
}

test('keyboard settling preserves active edit cell using visibility refreshes only', () => {
  const { adapter, calls, jobs, flush } = fixture();
  adapter.onCellEditingStarted({ rowIndex: 3, column: 'edited' });
  assert.deepEqual([...jobs.values()].map(job => job.delay), [40, 140, 320, 650]);
  flush();
  assert.deepEqual(calls, Array(4).fill([['event', 'resize'], ['row', 3, 'middle'], ['column', 'edited']]).flat());
  calls.length = 0;
  adapter.onViewportResize();
  flush();
  assert.deepEqual(calls, [['event', 'resize'], ['row', 3, 'middle'], ['column', 'edited']]);
  calls.length = 0;
  adapter.onCellEditingStopped();
  flush();
  assert.deepEqual(calls, [['event', 'resize'], ['row', 99, 'middle'], ['column', 'focused']]);
});

test('a delayed stop from an earlier edit cannot clear the next editor', () => {
  const { adapter, calls, flush } = fixture();
  adapter.onCellEditingStarted({ rowIndex: 3, column: 'old' });
  adapter.onCellEditingStopped();
  adapter.onCellEditingStarted({ rowIndex: 4, column: 'new' });
  flush();
  assert.equal(calls.filter(call => call[0] === 'row').length, 4);
  assert(calls.filter(call => call[0] === 'row').every(call => call[1] === 4));
});

test('standalone never refreshes for host keyboard hooks; destruction cancels scheduled work', () => {
  const standalone = fixture(false);
  standalone.adapter.onCellEditingStarted({ rowIndex: 3, column: 'x' });
  standalone.adapter.onViewportResize();
  standalone.flush();
  assert.deepEqual(standalone.calls, []);
  const hosted = fixture();
  hosted.adapter.onCellEditingStarted({ rowIndex: 3, column: 'x' });
  hosted.adapter.destroy();
  hosted.flush();
  assert.deepEqual(hosted.calls, []);
});

test('settling callbacks tolerate a missing or destroyed grid', () => {
  const destroyed = fixture();
  destroyed.adapter.onCellEditingStarted({ rowIndex: 3, column: 'x' });
  destroyed.grid.isDestroyed = () => true;
  destroyed.flush();
  assert(destroyed.calls.every(call => call[0] === 'event'));
  const jobs = [];
  const absent = createHostGridAdapter({
    windowRef: { Event, dispatchEvent() {}, setTimeout(fn) { jobs.push(fn); return jobs.length; } },
    getGrid: () => null,
    isHostMode: () => true,
  });
  absent.onCellEditingStarted({ rowIndex: 3, column: 'x' });
  assert.doesNotThrow(() => jobs.forEach(fn => fn()));
});

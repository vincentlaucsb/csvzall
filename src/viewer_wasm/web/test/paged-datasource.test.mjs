import assert from 'node:assert/strict';
import test from 'node:test';
import { createPagedDatasource } from '../../../viewer/modules/grid.mjs';

test('the opening block is immediate and subsequent blocks wait for scrolling to settle', t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const loaded = [], failed = [], cached = new Set([0, 500, 1000]);
  const source = createPagedDatasource(p => loaded.push(p.startRow), row => cached.has(row));
  const request = startRow => source.getRows({ startRow, failCallback: () => failed.push(startRow) });
  request(0);
  assert.deepEqual(loaded, [0]);
  request(500);
  t.mock.timers.tick(30);
  request(1000);
  cached.delete(500);
  t.mock.timers.tick(30);
  source.onVerticalScroll();
  t.mock.timers.tick(39);
  assert.deepEqual(loaded, [0]);
  t.mock.timers.tick(1);
  assert.deepEqual(loaded, [0, 1000]);
  assert.deepEqual(failed, [500]);
  request(0);
  t.mock.timers.tick(39);
  assert.deepEqual(loaded, [0, 1000]);
  t.mock.timers.tick(1);
  assert.deepEqual(loaded, [0, 1000, 0]);
});

test('replacing a datasource cancels queued and late requests for the old file', t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const failed = [];
  const source = createPagedDatasource(() => assert.fail('obsolete fetch'), () => true);
  const request = startRow => source.getRows({ startRow, failCallback: () => failed.push(startRow) });
  request(500);
  source.destroy();
  request(0);
  t.mock.timers.tick(50);
  assert.deepEqual(failed, [500, 0]);
});

import assert from 'node:assert/strict';
import test from 'node:test';
import { createSaveState } from '../src/save-state.js';
import { createMutationTracker } from '../../../viewer/modules/save.mjs';

test('pending mutation dispatch invalidates a save before its worker response arrives', async () => {
  const state = createSaveState();
  const tracker = createMutationTracker(() => state.changed());
  let finishSave, finishEdit;
  let cleaned = false;
  const saving = state.save({ prepare: async () => ({}), persist: () => new Promise(resolve => { finishSave = resolve; }), onSaved() { cleaned = true; } });
  await Promise.resolve();
  const edit = tracker.run(() => new Promise(resolve => { finishEdit = resolve; }));
  await Promise.resolve();
  finishSave(true); await saving;
  assert.equal(cleaned, false);
  let settled = false;
  const waiting = tracker.settle().then(() => { settled = true; });
  await Promise.resolve(); assert.equal(settled, false);
  finishEdit(); await edit; await waiting;
  assert.equal(settled, true);
});

const deferred = () => { let resolve, reject; const promise = new Promise((a, b) => { resolve = a; reject = b; }); return { promise, resolve, reject }; };

test('save stays dirty until acknowledged and never clears edits made during preparation or writing', async () => {
  for (const phase of ['prepare', 'persist', 'none']) {
    const state = createSaveState();
    state.changed();
    const prepared = deferred();
    const written = deferred();
    let cleaned = false;
    const saving = state.save({ prepare: () => prepared.promise, persist: () => written.promise, onSaved() { cleaned = true; } });
    if (phase === 'prepare') state.changed();
    prepared.resolve({});
    await Promise.resolve();
    assert.equal(cleaned, false);
    if (phase === 'persist') state.changed();
    written.resolve(true);
    assert.equal(await saving, true);
    assert.equal(cleaned, phase === 'none');
  }
});

test('failed preparation or host writes retain dirty state; standalone download clears saved revision', async () => {
  const state = createSaveState();
  let cleaned = false;
  for (const phase of ['prepare', 'persist']) {
    await assert.rejects(state.save({
      prepare: async () => { if (phase === 'prepare') throw Error('failed'); return {}; },
      persist: async () => { throw Error('failed'); },
      onSaved() { cleaned = true; },
    }), /failed/);
    assert.equal(cleaned, false);
  }
  assert.equal(await state.save({ prepare: async () => ({}), persist: async () => false, onSaved() { cleaned = true; } }), false);
  assert.equal(cleaned, true);
});

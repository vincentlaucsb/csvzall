export function buildSavePayload(viewState, getColumns) {
  if (!viewState.columnOrderDirty) {
    return {};
  }
  return { columns: getColumns() };
}

// Dispatch in order because edits and structural changes share row positions.
// Invalidate save checkpoints when an operation is queued, before its response.
export function createMutationTracker(onStart) {
  const pending = new Set();
  let tail = Promise.resolve();
  return {
    run(action) {
      onStart();
      const result = tail.then(action);
      tail = result.catch(() => {});
      pending.add(result);
      void result.then(() => pending.delete(result), () => pending.delete(result));
      return result;
    },
    async settle() {
      while (pending.size) await Promise.all([...pending]);
    },
  };
}

export async function saveViewerState({ viewState, postJson, getColumns }) {
  const reloadAfterSave = viewState.columnOrderDirty;
  const revision = viewState.revision;
  const result = await postJson('/api/save', buildSavePayload(viewState, getColumns));
  const saved = viewState.markClean(revision);
  return { result, reloadAfterSave: reloadAfterSave && saved };
}

export function installHostSaveRequests({ win = window, save, getDirty }) {
  const parent = win.parent !== win ? win.parent : null;
  let saving = false;
  const listener = async (event) => {
    const data = event.data;
    if (!parent || event.source !== parent || data?.source !== 'obsidian-csvzall' ||
      data.type !== 'save-request' || data.version !== 1 || !Number.isSafeInteger(data.requestId)) return;
    if (saving) return;
    saving = true;
    try {
      if (!(await save())) throw new Error('Could not save CSV. Your edits are still open.');
      parent.postMessage({ source: 'csvzall-viewer', type: 'save-response', version: 1, requestId: data.requestId, success: true, dirty: getDirty() }, '*');
    } catch (error) {
      parent.postMessage({ source: 'csvzall-viewer', type: 'save-response', version: 1, requestId: data.requestId,
        success: false, dirty: getDirty(), error: error instanceof Error ? error.message : String(error) }, '*');
    } finally { saving = false; }
  };
  win.addEventListener('message', listener);
  return () => win.removeEventListener('message', listener);
}

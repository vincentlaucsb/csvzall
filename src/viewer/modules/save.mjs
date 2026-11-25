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
    run(action, notify = onStart) {
      notify();
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
      if (!(await save())) throw new Error('Could not save the file. Your edits are still open.');
      parent.postMessage({ source: 'csvzall-viewer', type: 'save-response', version: 1, requestId: data.requestId, success: true, dirty: getDirty() }, '*');
    } catch (error) {
      parent.postMessage({ source: 'csvzall-viewer', type: 'save-response', version: 1, requestId: data.requestId,
        success: false, dirty: getDirty(), error: error instanceof Error ? error.message : String(error) }, '*');
    } finally { saving = false; }
  };
  win.addEventListener('message', listener);
  return () => win.removeEventListener('message', listener);
}

export function installHostAutosave({ win, source, documentType, setPolicy, onMessage }) {
  const parent = win.parent !== win ? win.parent : null;
  if (!parent) return () => {};
  function listener(event) {
    const data = event.data;
    if (event.source !== parent || data?.source !== 'obsidian-csvzall' || data.version !== 1) return;
    if (data.type === `${documentType}-autosave-settings` && typeof data.enabled === 'boolean' &&
        typeof data.visible === 'boolean') {
      setPolicy(data);
    } else {
      onMessage(data);
    }
  }
  win.addEventListener('message', listener);
  parent.postMessage({ source, type: `${documentType}-autosave-ready`, version: 1 }, '*');
  return () => win.removeEventListener('message', listener);
}

export function installHostCsvUpdates({ shell, source, win = window }) {
  const parent = win.parent !== win ? win.parent : null;
  if (!parent) return () => {};
  let requestId = 0;
  shell.connectRecovery(edits => {
    const id = ++requestId;
    parent.postMessage({ source, type: 'csv-recovery-request', version: 1, requestId: id, edits }, '*');
  });
  return installHostAutosave({ win, source, documentType: 'csv', setPolicy: shell.setAutoSave, onMessage(data) {
    if (data.type === 'csv-recovery-result' && requestId > 0 && data.requestId === requestId) {
      shell.recoveryResult(data);
    } else if (data.type === 'csv-file-modified') {
      if (source === 'csvzall-wasm-viewer' &&
          (!(data.buffer instanceof ArrayBuffer) || !Number.isSafeInteger(data.revision) || data.revision < 1)) return;
      shell.externalChange(data);
    }
  } });
}

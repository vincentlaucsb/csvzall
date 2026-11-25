// Parsing, indexing, mutations and byte generation stay in the worker.
export function createWasmAdapter(worker) {
  let nextId = 1;
  let failed;
  const pending = new Map();
  const onMessage = event => {
    const { id, ok, result, error } = event.data ?? {};
    const request = pending.get(id);
    if (!request) return;
    pending.delete(id);
    if (ok) request.resolve(result);
    else request.reject(new Error(error || 'Worker request failed'));
  };
  const rejectAll = error => {
    failed = error;
    for (const request of pending.values()) request.reject(error);
    pending.clear();
  };
  const onError = event => rejectAll(new Error(event.message || 'CSV worker failed'));
  worker.addEventListener('message', onMessage);
  worker.addEventListener('error', onError);
  const request = (type, payload = {}, transfer = []) => {
    if (failed) return Promise.reject(failed);
    const id = nextId++;
    return new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject });
      try { worker.postMessage({ id, type, payload }, transfer); }
      catch (error) { pending.delete(id); reject(error); }
    });
  };
  return {
    capabilities: { localFiles: true, columnOrder: false }, request,
    save: () => request('prepareSave'),
    async persist(result, { name, hostBridge, downloadBytes }) {
      let handled;
      try {
        handled = await hostBridge?.saveFile({ name, result });
        if (!handled) downloadBytes(name, new Uint8Array(result.buffer, result.byteOffset, result.byteLength));
      } catch (error) {
        await request('finishSave', { saveId: result.saveId, commit: false }).catch(() => {});
        throw error;
      }
      result.schema = await request('finishSave', { saveId: result.saveId, commit: true });
      return Boolean(handled);
    },
    destroy() {
      rejectAll(new Error('Viewer closed'));
      worker.removeEventListener('message', onMessage);
      worker.removeEventListener('error', onError);
      worker.terminate();
    },
  };
}

/** Optional host integration. Standalone viewers never expose note insertion. */
export function createNoteExport({ button, getQuery, getDirty, setStatus, windowRef = globalThis.window }) {
  let enabled = false, pending = null, nextId = 0, disposed = false;
  function refresh() {
    button.hidden = !enabled;
    button.disabled = !enabled || pending !== null || !getQuery();
    button.textContent = getDirty() ? 'Save and insert into note' : 'Insert into note';
  }
  function receive(event) {
    if (event.source !== windowRef?.parent || windowRef.parent === windowRef) return;
    const data = event.data;
    if (!data || data.source !== 'obsidian-csvzall' || data.version !== 1) return;
    if (data.type === 'note-export-capabilities' && typeof data.enabled === 'boolean') { enabled = data.enabled; refresh(); }
    if (data.type === 'insert-query-response' && data.requestId === pending && typeof data.success === 'boolean') {
      pending = null;
      setStatus(data.success ? 'Inserted export into note.' : typeof data.error === 'string' ? data.error : 'Insertion cancelled.');
      refresh();
    }
  }
  function insert() {
    const sql = getQuery();
    if (!enabled || !sql || pending !== null || disposed) return;
    pending = ++nextId; refresh();
    windowRef.parent.postMessage({ source: 'csvzall-viewer', type: 'insert-query', version: 1, requestId: pending, sql, save: getDirty() }, '*');
  }
  button.addEventListener('click', insert);
  windowRef?.addEventListener('message', receive);
  refresh();
  if (windowRef?.parent && windowRef.parent !== windowRef) windowRef.parent.postMessage({ source: 'csvzall-viewer', type: 'note-export-ready', version: 1 }, '*');
  return { refresh, destroy() { disposed = true; button.removeEventListener('click', insert); windowRef?.removeEventListener('message', receive); } };
}

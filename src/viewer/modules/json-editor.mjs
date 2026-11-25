import './host-theme.mjs';
import { createJsonEditor } from './codemirror-json.mjs';
import { JsonDocument } from './json-document.mjs';
import { postDirtyState, installUnsavedChangesBeforeUnload } from './dirty-state.mjs';
import { installHostSaveRequests } from './save.mjs';
import { createViewerToolbar } from './viewer-toolbar.mjs';

export async function openJsonEditor(win, createEditor = createJsonEditor) {
  const document = win.document;
  const status = document.querySelector('#status');
  const button = document.querySelector('#save');
  const reset = document.querySelector('#reset');
  const token = new URLSearchParams(win.location.search).get('token');
  async function request(options = {}) {
    const response = await win.fetch('/api/document', {
      ...options, headers: { 'X-Session-Token': token, ...options.headers }, cache: 'no-store',
    });
    if (!response.ok) throw new Error(await response.text() || response.statusText);
    return response.json();
  }
  let saved, editor, saving = false;
  let refreshPending = false;
  const state = { dirty: false };
  win.addEventListener('focus', scheduleRefresh);
  win.addEventListener('message', event => {
    if (win.parent !== win && event.source === win.parent && event.data?.source === 'obsidian-csvzall' &&
        event.data.type === 'json-file-modified' && event.data.version === 1) scheduleRefresh();
  });
  const source = await request();
  let format = new JsonDocument(source.text);
  let revision = source.revision;
  let queue = Promise.resolve();
  const enqueue = action => {
    const operation = queue.then(action);
    queue = operation.catch(() => {});
    return operation;
  };
  const reportError = error => { status.textContent = error.message; };
  const update = doc => {
    const dirty = !doc.eq(saved);
    if (state.dirty !== dirty) postDirtyState(dirty);
    state.dirty = dirty;
    status.textContent = dirty ? 'Unsaved changes' : '';
    button.disabled = !dirty || Boolean(saving) || !source.editable;
    reset.disabled = button.disabled;
    if (!dirty && refreshPending) scheduleRefresh();
  };
  const save = () => enqueue(async () => {
    const doc = editor.state.doc;
    if (doc.eq(saved)) return true;
    button.disabled = true;
    reset.disabled = true;
    saving = true;
    try {
      const result = await request({ method: 'POST', headers: { 'If-Match': revision, 'Content-Type': 'text/plain;charset=utf-8' },
        body: format.serialize(doc.toString()) });
      revision = result.revision;
      saved = doc;
    } finally { saving = false; update(editor.state.doc); }
    return true;
  });
  const saveClick = () => { void save().catch(reportError); };
  const mount = () => createEditor(document.querySelector('#editor'), format, source.editable, update, saveClick);
  function scheduleRefresh() {
    refreshPending = true;
    if (!editor || state.dirty) return;
    void enqueue(async () => {
      if (state.dirty || !refreshPending) return;
      refreshPending = false;
      const latest = await request();
      if (state.dirty) { refreshPending = true; return; }
      if (latest.revision === revision) return;
      const focused = editor.hasFocus;
      editor.destroy();
      format = new JsonDocument(latest.text);
      revision = latest.revision;
      editor = mount();
      saved = editor.state.doc;
      update(saved);
      if (focused) editor.focus();
    }).catch(reportError);
  }
  editor = mount();
  saved = editor.state.doc;
  update(saved);
  document.title = source.name;
  document.querySelector('#file-name').textContent = source.name;
  button.addEventListener('click', saveClick);
  reset.addEventListener('click', () => {
    if (reset.disabled) return;
    editor.dispatch({ changes: { from: 0, to: editor.state.doc.length, insert: saved } });
    scheduleRefresh();
    editor.focus();
  });
  installHostSaveRequests({ win, save, getDirty: () => state.dirty });
  installUnsavedChangesBeforeUnload(state, win);
}
if (typeof window !== 'undefined') {
  document.querySelector('header').replaceWith(createViewerToolbar(document).header);
  if (window.parent !== window) document.body.dataset.hostMode = '';
  openJsonEditor(window).catch(error => { document.querySelector('#status').textContent = error.message; });
}

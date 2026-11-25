import './host-theme.mjs';
import { createJsonEditor } from './codemirror-json.mjs';
import { JsonDocument } from './json-document.mjs';
import { postDirtyState, installUnsavedChangesBeforeUnload } from './dirty-state.mjs';
import { createAutosave } from './autosave.mjs';
import { installHostAutosave, installHostSaveRequests } from './save.mjs';
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
  let saved, editor, pendingSaves = 0;
  let autosavePolicy, autosave;
  let refreshPending = false;
  const state = { dirty: false };
  win.addEventListener('focus', scheduleRefresh);
  const disconnect = installHostAutosave({
    win, source: 'csvzall-viewer', documentType: 'json',
    setPolicy: policy => {
      if (autosave) autosave.setPolicy({ ...policy, visible: source.editable && policy.visible });
      else autosavePolicy = policy;
    },
    onMessage: data => { if (data.type === 'json-file-modified') scheduleRefresh(); },
  });
  const source = await request().catch(error => { disconnect(); throw error; });
  let format = new JsonDocument(source.text);
  let revision = source.revision;
  let queue = Promise.resolve();
  const enqueue = action => {
    const operation = queue.then(action);
    queue = operation.catch(() => {});
    return operation;
  };
  const reportError = error => { status.textContent = error.message; };
  autosave = createAutosave({
    control: document.querySelector('#autosave-control'), input: document.querySelector('#autosave'),
    value: document.querySelector('#autosave-value'),
  }, {
    win,
    canSave: () => source.editable && state.dirty && pendingSaves === 0,
    save: () => save().catch(error => { reportError(error); throw error; }),
  });
  const update = (doc, edited = false) => {
    const dirty = !doc.eq(saved);
    if (state.dirty !== dirty) postDirtyState(dirty);
    state.dirty = dirty;
    status.textContent = dirty ? 'Unsaved changes' : '';
    button.disabled = !dirty || pendingSaves > 0 || !source.editable;
    reset.disabled = button.disabled;
    if (!dirty && refreshPending) scheduleRefresh();
    if (edited) autosave.edited();
    else autosave.schedule();
  };
  const save = () => {
    autosave.cancel();
    pendingSaves++;
    button.disabled = true;
    reset.disabled = true;
    return enqueue(async () => {
      const doc = editor.state.doc;
      if (doc.eq(saved)) return true;
      const result = await request({ method: 'POST', headers: { 'If-Match': revision, 'Content-Type': 'text/plain;charset=utf-8' },
        body: format.serialize(doc.toString()) });
      revision = result.revision;
      saved = doc;
      return true;
    }).catch(error => {
      autosave.failed();
      throw error;
    }).finally(() => {
      pendingSaves--;
      update(editor.state.doc);
    });
  };
  const saveClick = () => { void save().catch(reportError); };
  const mount = () => createEditor(document.querySelector('#editor'), format, source.editable,
    doc => update(doc, true), saveClick);
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
  if (autosavePolicy) autosave.setPolicy({ ...autosavePolicy, visible: source.editable && autosavePolicy.visible });
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

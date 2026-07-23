import { createGrid } from 'ag-grid-community';
import { createContextMenu, createDropdownMenu } from 'popright';
import 'ag-grid-community/styles/ag-grid.css';
import 'ag-grid-community/styles/ag-theme-quartz.css';
import 'popright/styles.css';
import 'popright/dropdown.css';
import './styles.css';
import { setupFileDrop } from './file-drop.js';
import { createHostBridge } from './host-bridge.js';
import { createIcon } from './icons.js';
import { setupPwa } from './pwa.js';

const fileInput = document.getElementById('file-input');
const filePicker = document.querySelector('.file-picker');
const statusNode = document.getElementById('status');
const recordCountNode = document.getElementById('record-count');
const saveButton = document.getElementById('save');
const resetButton = document.getElementById('reset');
const insertMenuButton = document.getElementById('insert-menu');
const deleteMenuButton = document.getElementById('delete-menu');
const installButton = document.getElementById('install-app');
const dropOverlay = document.getElementById('drop-overlay');
const gridElement = document.getElementById('grid');
const loadingDialog = document.getElementById('loading-dialog');
const loadingTitleNode = document.getElementById('loading-title');
const loadingDetailNode = document.getElementById('loading-detail');
const insertColumnDialog = document.getElementById('insert-column-dialog');
const insertColumnForm = document.getElementById('insert-column-form');
const insertColumnName = document.getElementById('insert-column-name');
const insertColumnError = document.getElementById('insert-column-error');
const cancelInsertColumn = document.getElementById('cancel-insert-column');
const renameColumnDialog = document.getElementById('rename-column-dialog');
const renameColumnForm = document.getElementById('rename-column-form');
const renameColumnName = document.getElementById('rename-column-name');
const renameColumnError = document.getElementById('rename-column-error');
const cancelRenameColumn = document.getElementById('cancel-rename-column');

const worker = new Worker(new URL('./csv-worker.js', import.meta.url), { type: 'module' });
let nextRequestId = 1;
const pendingRequests = new Map();
let viewOpen = false;
let activeName = '';
let activeColumns = [];
let rowCount = 0;
let viewGeneration = 0;
let dirty = false;
let pendingInsertColumn = 0;
let pendingRenameColumn = '';
let hostBridge = null;
let wasmReady = false;
let pendingOpenFile = null;

function setStatus(message) {
  statusNode.textContent = message;
}

function showLoading(title, detail = '') {
  loadingTitleNode.textContent = title;
  loadingDetailNode.textContent = detail;
  if (!loadingDialog.open) {
    if (typeof loadingDialog.showModal === 'function') {
      loadingDialog.showModal();
    } else {
      loadingDialog.setAttribute('open', '');
    }
  }
}

function hideLoading() {
  if (loadingDialog.open) {
    loadingDialog.close();
  }
}

function showDialog(dialog, focusTarget) {
  if (typeof dialog.showModal === 'function') {
    dialog.showModal();
  } else {
    dialog.setAttribute('open', '');
  }
  focusTarget?.focus();
}

function workerRequest(type, payload = {}, transfer = []) {
  const id = nextRequestId;
  nextRequestId += 1;
  return new Promise((resolve, reject) => {
    pendingRequests.set(id, { resolve, reject });
    worker.postMessage({ id, type, payload }, transfer);
  });
}

function rowsToObjects(columns, rows, offset) {
  return rows.map((values, index) => {
    const row = { _csvzallRowId: offset + index };
    columns.forEach((column, columnIndex) => {
      row[column] = values[columnIndex] ?? '';
    });
    return row;
  });
}

function rowInsertionIndex(anchorRow, count, offset) {
  const safeCount = Number.isInteger(count) && count > 0 ? count : 0;
  if (!Number.isInteger(anchorRow)) {
    return safeCount;
  }
  const clampedAnchor = Math.max(0, Math.min(anchorRow, safeCount));
  return Math.max(0, Math.min(clampedAnchor + offset, safeCount));
}

function updateRecordCount(nextRowCount = rowCount) {
  recordCountNode.textContent =
    `${nextRowCount.toLocaleString()} rows, ${activeColumns.length.toLocaleString()} columns`;
}

function refreshActionState() {
  saveButton.disabled = !viewOpen || !dirty;
  resetButton.disabled = !viewOpen || !dirty;
  insertMenuButton.disabled = !viewOpen;
  deleteMenuButton.disabled = !viewOpen;
}

function setDirty(nextDirty, { force = false } = {}) {
  const changed = dirty !== nextDirty;
  dirty = nextDirty;
  refreshActionState();
  if (changed || force) {
    hostBridge?.emitDirtyState(dirty);
  }
}

function applySchema(schema) {
  activeColumns = schema.columns;
  rowCount = schema.rowCount;
  updateRecordCount(rowCount);
}

function markDataDirty(message = `Unsaved changes in ${activeName}.`) {
  setDirty(true);
  setStatus(message);
}

function downloadBytes(name, bytes) {
  const blob = new Blob([bytes], { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  URL.revokeObjectURL(url);
}

function setHostMode(enabled) {
  document.body.toggleAttribute('data-host-mode', enabled);
  if (filePicker) {
    filePicker.hidden = enabled;
  }
  fileInput.disabled = enabled || fileInput.disabled;
  saveButton.textContent = enabled ? 'Save' : 'Download CSV';
}

const gridOptions = {
  columnDefs: [],
  defaultColDef: {
    editable: true,
    resizable: true,
    sortable: false,
    filter: false,
    minWidth: 140,
    flex: 1,
  },
  rowModelType: 'infinite',
  rowSelection: {
    mode: 'singleRow',
    checkboxes: false,
    headerCheckbox: false,
    enableClickSelection: true,
  },
  cacheBlockSize: 500,
  maxBlocksInCache: 8,
  blockLoadDebounceMillis: 40,
  animateRows: false,
  suppressColumnVirtualisation: false,
  onCellValueChanged(event) {
    if (!viewOpen || !event.colDef.field || event.colDef.field === '_csvzallRowId') {
      return;
    }
    void commitCellEdit(event);
  },
};

const gridApi = createGrid(gridElement, gridOptions);

document.addEventListener('contextmenu', (event) => {
  event.preventDefault();
}, { capture: true });

function setGridColumnDefs(columns) {
  gridApi.setGridOption('columnDefs', columns.map((column) => ({
    headerName: column,
    field: column,
    editable: true,
    resizable: true,
    minWidth: 140,
    flex: 1,
  })));
}

function setDatasource(datasource) {
  gridApi.setGridOption('datasource', datasource);
}

function refreshRows() {
  if (typeof gridApi.purgeInfiniteCache === 'function') {
    gridApi.purgeInfiniteCache();
  } else if (typeof gridApi.refreshInfiniteCache === 'function') {
    gridApi.refreshInfiniteCache();
  }
}

function selectedSourceRow() {
  const selected = gridApi.getSelectedRows ? gridApi.getSelectedRows() : [];
  if (Number.isInteger(selected[0]?._csvzallRowId)) {
    return selected[0]._csvzallRowId;
  }
  const focused = gridApi.getFocusedCell ? gridApi.getFocusedCell() : null;
  if (focused && Number.isInteger(focused.rowIndex) && typeof gridApi.getDisplayedRowAtIndex === 'function') {
    const focusedNode = gridApi.getDisplayedRowAtIndex(focused.rowIndex);
    if (Number.isInteger(focusedNode?.data?._csvzallRowId)) {
      return focusedNode.data._csvzallRowId;
    }
  }
  return rowCount;
}

function focusedColumnName() {
  const focused = gridApi.getFocusedCell ? gridApi.getFocusedCell() : null;
  const column = focused?.column;
  if (!column) {
    return '';
  }
  return column.getColId ? column.getColId() : (column.colId || '');
}

function createToolbarDropdown(button, items, onSelect) {
  return createDropdownMenu(button, {
    items,
    theme: 'system',
    onSelect,
  });
}

async function commitCellEdit(event) {
  try {
    await workerRequest('editCell', {
      rowIndex: event.data._csvzallRowId,
      column: event.colDef.field,
      value: event.newValue ?? '',
    });
    markDataDirty();
  } catch (error) {
    event.node.setDataValue(event.colDef.field, event.oldValue ?? '');
    setStatus(error instanceof Error ? error.message : 'Edit failed');
  }
}

function loadView(schema, name) {
  viewOpen = true;
  activeName = name;
  applySchema(schema);
  viewGeneration += 1;
  const generation = viewGeneration;
  setGridColumnDefs(activeColumns);
  setDatasource({
    async getRows(params) {
      try {
        const offset = params.startRow;
        const limit = Math.max(params.endRow - params.startRow, 1);
        const page = await workerRequest('readRows', { offset, limit });
        if (generation !== viewGeneration) {
          params.failCallback();
          return;
        }
        rowCount = page.totalRows;
        params.successCallback(
          rowsToObjects(activeColumns, page.rows, page.offset),
          page.totalRows);
        updateRecordCount(page.totalRows);
      } catch (error) {
        params.failCallback();
        setStatus(error instanceof Error ? error.message : 'Row load failed');
      }
    },
  });
  setDirty(false, { force: hostBridge?.isHostMode() });
  setStatus(`Loaded ${name}.`);
}

async function openBuffer(name, buffer, sourceLabel) {
  viewOpen = false;
  rowCount = 0;
  activeColumns = [];
  setDirty(false);
  refreshActionState();
  showLoading('Opening CSV', `${sourceLabel || name} is being indexed in the background.`);
  setStatus(`Opening ${name}...`);
  try {
    const schema = await workerRequest('open', { name, buffer }, [buffer]);
    loadView(schema, name);
  } catch (error) {
    setStatus(error instanceof Error ? error.message : 'Open failed');
  } finally {
    hideLoading();
  }
}

async function openFile(file) {
  const name = file.name || 'input.csv';
  const buffer = await file.arrayBuffer();
  await openBuffer(name, buffer, name);
}

async function openFileWhenReady(file) {
  if (!wasmReady) {
    pendingOpenFile = file;
    return;
  }
  await openFile(file);
}

async function requestOpenFile(file) {
  if (dirty && !window.confirm(
    `Open ${file.name}? Unsaved changes to ${activeName || 'the current file'} will be lost.`,
  )) {
    return;
  }
  await openFileWhenReady(file);
}

async function openHostFile(file) {
  await openBuffer(file.name || 'input.csv', file.buffer, 'Host file');
}

function columnIndexFor(column) {
  const columnIndex = activeColumns.indexOf(column || focusedColumnName());
  return columnIndex >= 0 ? columnIndex : activeColumns.length;
}

function showColumnError(message) {
  insertColumnError.textContent = message;
  insertColumnError.hidden = false;
}

function clearColumnError() {
  insertColumnError.textContent = '';
  insertColumnError.hidden = true;
}

function showRenameColumnError(message) {
  renameColumnError.textContent = message;
  renameColumnError.hidden = false;
}

function clearRenameColumnError() {
  renameColumnError.textContent = '';
  renameColumnError.hidden = true;
}

async function insertColumnAt(column, name) {
  const schema = await workerRequest('insertColumn', { column, name, value: '' });
  applySchema(schema);
  setGridColumnDefs(activeColumns);
  refreshRows();
  markDataDirty();
}

async function insertRowAt(row) {
  const values = activeColumns.map(() => '');
  try {
    const schema = await workerRequest('insertRow', { row, values });
    applySchema(schema);
    refreshRows();
    markDataDirty();
  } catch (error) {
    setStatus(error instanceof Error ? error.message : 'Insert failed');
  }
}

async function insertRowRelativeToSelection(offset) {
  await insertRowAt(rowInsertionIndex(selectedSourceRow(), rowCount, offset));
}

async function deleteRowAt(row) {
  if (row >= rowCount) {
    setStatus('Select a row to delete.');
    return;
  }
  try {
    const schema = await workerRequest('deleteRow', { row });
    applySchema(schema);
    refreshRows();
    markDataDirty();
  } catch (error) {
    setStatus(error instanceof Error ? error.message : 'Delete failed');
  }
}

async function moveRowBy(row, delta) {
  const target = row + delta;
  if (row < 0 || row >= rowCount || target < 0 || target >= rowCount) {
    return;
  }
  try {
    await workerRequest('swapRows', { first: row, second: target });
    refreshRows();
    markDataDirty();
  } catch (error) {
    setStatus(error instanceof Error ? error.message : 'Move failed');
  }
}

async function deleteColumnByName(column) {
  if (!column || !activeColumns.includes(column)) {
    setStatus('Focus a column to delete.');
    return;
  }
  try {
    const schema = await workerRequest('deleteColumn', { column });
    applySchema(schema);
    setGridColumnDefs(activeColumns);
    refreshRows();
    markDataDirty();
  } catch (error) {
    setStatus(error instanceof Error ? error.message : 'Column delete failed');
  }
}

async function renameColumnByName(column, name) {
  try {
    const schema = await workerRequest('renameColumn', { column, name });
    applySchema(schema);
    setGridColumnDefs(activeColumns);
    refreshRows();
    markDataDirty();
  } catch (error) {
    throw new Error(error instanceof Error ? error.message : 'Column rename failed');
  }
}

function showInsertColumnDialog(column = '', offset = 0) {
  const columnIndex = columnIndexFor(column);
  pendingInsertColumn = Math.min(columnIndex + offset, activeColumns.length);
  insertColumnName.value = '';
  clearColumnError();
  showDialog(insertColumnDialog, insertColumnName);
}

function showRenameColumnDialog(column) {
  if (!column || !activeColumns.includes(column)) {
    setStatus('Choose a column to rename.');
    return;
  }
  pendingRenameColumn = column;
  renameColumnName.value = column;
  clearRenameColumnError();
  showDialog(renameColumnDialog, renameColumnName);
  renameColumnName.select();
}

const rowMenu = createContextMenu({
  trigger: 'manual',
  theme: 'system',
  items: ({ data }) => {
    const row = data?.row;
    return [
      { id: 'move-up', label: 'Move Up', icon: () => createIcon('arrow-up'), disabled: !Number.isInteger(row) || row <= 0 },
      { id: 'move-down', label: 'Move Down', icon: () => createIcon('arrow-down'), disabled: !Number.isInteger(row) || row >= rowCount - 1 },
      { type: 'separator' },
      { id: 'insert-row-before', label: 'Insert Row Before', icon: () => createIcon('row-insert-bottom') },
      { id: 'insert-row-after', label: 'Insert Row After', icon: () => createIcon('row-insert-bottom') },
      { type: 'separator' },
      { id: 'delete-row', label: 'Delete Row', icon: () => createIcon('trash'), variant: 'danger' },
      { id: 'delete-column', label: 'Delete Column', icon: () => createIcon('column-remove'), variant: 'danger' },
    ];
  },
  onSelect(event) {
    const row = event.context.data?.row;
    const column = event.context.data?.column;
    if (event.id === 'move-up' && Number.isInteger(row)) {
      void moveRowBy(row, -1);
    }
    if (event.id === 'move-down' && Number.isInteger(row)) {
      void moveRowBy(row, 1);
    }
    if (event.id === 'insert-row-before' && Number.isInteger(row)) {
      void insertRowAt(rowInsertionIndex(row, rowCount, 0));
    }
    if (event.id === 'insert-row-after' && Number.isInteger(row)) {
      void insertRowAt(rowInsertionIndex(row, rowCount, 1));
    }
    if (event.id === 'delete-row' && Number.isInteger(row)) {
      void deleteRowAt(row);
    }
    if (event.id === 'delete-column' && typeof column === 'string') {
      void deleteColumnByName(column);
    }
  },
});

const columnMenu = createContextMenu({
  trigger: 'manual',
  theme: 'system',
  items: [
    { id: 'rename-column', label: 'Rename Column', icon: () => createIcon('pencil') },
    { type: 'separator' },
    { id: 'delete-column', label: 'Delete Column', icon: () => createIcon('column-remove'), variant: 'danger' },
  ],
  onSelect(event) {
    const column = event.context.data?.column;
    if (event.id === 'rename-column' && typeof column === 'string') {
      showRenameColumnDialog(column);
    }
    if (event.id === 'delete-column' && typeof column === 'string') {
      void deleteColumnByName(column);
    }
  },
});

gridOptions.onCellContextMenu = (event) => {
  const nativeEvent = event.event;
  const row = event.data?._csvzallRowId;
  const column = event.colDef?.field;
  if (!nativeEvent || !Number.isInteger(row)) {
    return;
  }
  nativeEvent.preventDefault();
  event.node?.setSelected?.(true);
  rowMenu.open({
    x: nativeEvent.clientX,
    y: nativeEvent.clientY,
    target: gridElement,
    triggerEvent: nativeEvent,
    context: { row, column },
  });
};
gridApi.setGridOption('onCellContextMenu', gridOptions.onCellContextMenu);

gridElement.addEventListener('contextmenu', (event) => {
  const headerCell = event.target?.closest?.('.ag-header-cell[col-id]');
  const column = headerCell?.getAttribute('col-id') ?? '';
  if (!column || !activeColumns.includes(column)) {
    return;
  }
  event.preventDefault();
  columnMenu.open({
    x: event.clientX,
    y: event.clientY,
    target: headerCell,
    triggerEvent: event,
    context: { column },
  });
});

createToolbarDropdown(insertMenuButton, [
  { id: 'insert-row-before', label: 'Row Before', icon: () => createIcon('row-insert-bottom') },
  { id: 'insert-row-after', label: 'Row After', icon: () => createIcon('row-insert-bottom') },
  { type: 'separator' },
  { id: 'insert-column-before', label: 'Column Before', icon: () => createIcon('column-insert-right') },
  { id: 'insert-column-after', label: 'Column After', icon: () => createIcon('column-insert-right') },
], (event) => {
  if (event.id === 'insert-row-before') {
    void insertRowRelativeToSelection(0);
  }
  if (event.id === 'insert-row-after') {
    void insertRowRelativeToSelection(1);
  }
  if (event.id === 'insert-column-before') {
    showInsertColumnDialog('', 0);
  }
  if (event.id === 'insert-column-after') {
    showInsertColumnDialog('', 1);
  }
});

createToolbarDropdown(deleteMenuButton, [
  { id: 'delete-row', label: 'Row', icon: () => createIcon('row-remove'), variant: 'danger' },
  { id: 'delete-column', label: 'Column', icon: () => createIcon('column-remove'), variant: 'danger' },
], (event) => {
  if (event.id === 'delete-row') {
    void deleteRowAt(selectedSourceRow());
  }
  if (event.id === 'delete-column') {
    void deleteColumnByName(focusedColumnName());
  }
});

insertColumnForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const name = insertColumnName.value.trim();
  if (!name) {
    showColumnError('Enter a column name.');
    insertColumnName.focus();
    return;
  }
  if (activeColumns.includes(name)) {
    showColumnError(`Column already exists: ${name}`);
    insertColumnName.focus();
    return;
  }
  try {
    await insertColumnAt(pendingInsertColumn, name);
    insertColumnDialog.close('insert');
  } catch (error) {
    showColumnError(error instanceof Error ? error.message : 'Column insert failed');
  }
});

cancelInsertColumn.addEventListener('click', () => {
  insertColumnDialog.close('cancel');
});

renameColumnForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const name = renameColumnName.value.trim();
  if (!name) {
    showRenameColumnError('Enter a column name.');
    renameColumnName.focus();
    return;
  }
  if (name === pendingRenameColumn) {
    renameColumnDialog.close('rename');
    return;
  }
  if (activeColumns.includes(name)) {
    showRenameColumnError(`Column already exists: ${name}`);
    renameColumnName.focus();
    return;
  }
  try {
    await renameColumnByName(pendingRenameColumn, name);
    renameColumnDialog.close('rename');
  } catch (error) {
    showRenameColumnError(error instanceof Error ? error.message : 'Column rename failed');
  }
});

cancelRenameColumn.addEventListener('click', () => {
  renameColumnDialog.close('cancel');
});

fileInput.addEventListener('change', () => {
  const file = fileInput.files?.[0];
  fileInput.value = '';
  if (file) {
    void requestOpenFile(file);
  }
});

saveButton.addEventListener('click', () => {
  if (!viewOpen) {
    return;
  }
  void (async () => {
    showLoading('Preparing Download', `${activeName} is being rewritten with your edits.`);
    try {
      const result = await workerRequest('save');
      if (await hostBridge.saveFile({ name: activeName, result })) {
        setDirty(false);
        refreshRows();
        setStatus(`Saved ${activeName} to host.`);
        return;
      }
      const bytes = new Uint8Array(result.buffer, result.byteOffset, result.byteLength);
      downloadBytes(activeName, bytes);
      setDirty(false);
      refreshRows();
      setStatus(`Downloaded ${activeName}.`);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : 'Download failed');
    } finally {
      hideLoading();
    }
  })();
});

resetButton.addEventListener('click', () => {
  if (!viewOpen) {
    return;
  }
  void (async () => {
    showLoading('Resetting CSV', `${activeName} is being restored from the original bytes.`);
    try {
      const schema = await workerRequest('reset');
      applySchema(schema);
      setGridColumnDefs(activeColumns);
      setDirty(false);
      refreshRows();
      setStatus(`Reset ${activeName}.`);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : 'Reset failed');
    } finally {
      hideLoading();
    }
  })();
});

worker.addEventListener('message', (event) => {
  const { id, ok, result, error } = event.data ?? {};
  const pending = pendingRequests.get(id);
  if (!pending) {
    return;
  }
  pendingRequests.delete(id);
  if (ok) {
    pending.resolve(result);
  } else {
    pending.reject(new Error(error || 'Worker request failed'));
  }
});

worker.addEventListener('error', (event) => {
  const error = new Error(event.message || 'CSV worker failed');
  for (const pending of pendingRequests.values()) {
    pending.reject(error);
  }
  pendingRequests.clear();
  hideLoading();
  setStatus(error.message);
});

async function start() {
  showLoading('Loading CSV Engine', 'The parser is starting in a background worker.');
  try {
    await workerRequest('init');
    wasmReady = true;
    if (pendingOpenFile) {
      const file = pendingOpenFile;
      pendingOpenFile = null;
      await openFile(file);
    }
    if (!hostBridge.isHostMode()) {
      if (!viewOpen) {
        setStatus('Open a local CSV file to begin.');
      }
      fileInput.disabled = false;
    }
  } catch (error) {
    setStatus(error instanceof Error ? error.message : 'Failed to load WASM module');
    return;
  } finally {
    hideLoading();
  }
  await hostBridge.markReady();
  if (!viewOpen && hostBridge.isHostMode()) {
    setStatus('Waiting for host CSV file...');
  }
}

hostBridge = createHostBridge({
  onOpenFile: openHostFile,
  onHostModeChange: setHostMode,
});
hostBridge.start();
setupFileDrop({
  overlay: dropOverlay,
  enabled: () => !hostBridge.isHostMode(),
  onFile: requestOpenFile,
  onStatus: setStatus,
});
setupPwa({ installButton, onLaunchFile: requestOpenFile });
fileInput.disabled = true;
refreshActionState();
setStatus('Loading CSV engine...');
void start();

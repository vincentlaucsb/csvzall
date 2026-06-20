import { createGrid } from 'ag-grid-community';
import 'ag-grid-community/styles/ag-grid.css';
import 'ag-grid-community/styles/ag-theme-quartz.css';
import './styles.css';

const fileInput = document.getElementById('file-input');
const statusNode = document.getElementById('status');
const recordCountNode = document.getElementById('record-count');
const saveButton = document.getElementById('save');
const resetButton = document.getElementById('reset');
const gridElement = document.getElementById('grid');
const loadingDialog = document.getElementById('loading-dialog');
const loadingTitleNode = document.getElementById('loading-title');
const loadingDetailNode = document.getElementById('loading-detail');

const worker = new Worker(new URL('./csv-worker.js', import.meta.url), { type: 'module' });
let nextRequestId = 1;
const pendingRequests = new Map();
let viewOpen = false;
let activeName = '';
let activeColumns = [];
let viewGeneration = 0;
let dirty = false;

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

function setDirty(nextDirty) {
  dirty = nextDirty;
  saveButton.disabled = !viewOpen || !dirty;
  resetButton.disabled = !viewOpen || !dirty;
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

async function commitCellEdit(event) {
  try {
    await workerRequest('editCell', {
      rowIndex: event.data._csvzallRowId,
      column: event.colDef.field,
      value: event.newValue ?? '',
    });
    setDirty(true);
    setStatus(`Unsaved changes in ${activeName}.`);
  } catch (error) {
    event.node.setDataValue(event.colDef.field, event.oldValue ?? '');
    setStatus(error instanceof Error ? error.message : 'Edit failed');
  }
}

function loadView(schema, name) {
  viewOpen = true;
  activeName = name;
  activeColumns = schema.columns;
  viewGeneration += 1;
  const generation = viewGeneration;
  recordCountNode.textContent =
    `${schema.rowCount.toLocaleString()} rows, ${activeColumns.length.toLocaleString()} columns`;
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
        params.successCallback(
          rowsToObjects(activeColumns, page.rows, page.offset),
          page.totalRows);
        recordCountNode.textContent =
          `${page.totalRows.toLocaleString()} rows, ${activeColumns.length.toLocaleString()} columns`;
      } catch (error) {
        params.failCallback();
        setStatus(error instanceof Error ? error.message : 'Row load failed');
      }
    },
  });
  setDirty(false);
  setStatus(`Loaded ${name}.`);
}

async function openFile(file) {
  const name = file.name || 'input.csv';
  viewOpen = false;
  setDirty(false);
  showLoading('Opening CSV', `${name} is being indexed in the background.`);
  setStatus(`Opening ${name}...`);
  try {
    const buffer = await file.arrayBuffer();
    const schema = await workerRequest('open', { name, buffer }, [buffer]);
    loadView(schema, name);
  } catch (error) {
    setStatus(error instanceof Error ? error.message : 'Open failed');
  } finally {
    hideLoading();
  }
}

fileInput.addEventListener('change', () => {
  const file = fileInput.files?.[0];
  if (file) {
    void openFile(file);
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
      activeColumns = schema.columns;
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
    setStatus('Open a local CSV file to begin.');
    fileInput.disabled = false;
  } catch (error) {
    setStatus(error instanceof Error ? error.message : 'Failed to load WASM module');
  } finally {
    hideLoading();
  }
}

fileInput.disabled = true;
setStatus('Loading CSV engine...');
void start();

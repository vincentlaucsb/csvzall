import createCsvzallViewerModule from './generated/csvzall_viewer_wasm.js';
import wasmUrl from './generated/csvzall_viewer_wasm.wasm?url';

let moduleInstance = null;
let view = null;
let activePath = '';
const preparedSavePath = '/pending-save.csv';
let nextSaveId = 0;
let preparedSaveId = 0;

function discardPreparedSave() {
  preparedSaveId = 0;
  if (moduleInstance.FS.analyzePath(preparedSavePath).exists) {
    moduleInstance.FS.unlink(preparedSavePath);
  }
}

function vectorToArray(vector) {
  const values = [];
  try {
    for (let i = 0; i < vector.size(); i += 1) {
      values.push(vector.get(i));
    }
  } finally {
    vector.delete?.();
  }
  return values;
}

function rowsVectorToArrays(rowsVector) {
  const rows = [];
  try {
    for (let i = 0; i < rowsVector.size(); i += 1) {
      rows.push(vectorToArray(rowsVector.get(i)));
    }
  } finally {
    rowsVector.delete?.();
  }
  return rows;
}

function arrayToVector(values) {
  const vector = new moduleInstance.StringList();
  for (const value of values ?? []) {
    vector.push_back(value ?? '');
  }
  return vector;
}

function ensureReady() {
  if (!moduleInstance) {
    throw new Error('CSV engine is still loading');
  }
}

function ensureView() {
  ensureReady();
  if (!view) {
    throw new Error('No CSV file is open');
  }
}

function safeFileName(name) {
  return String(name || '').replace(/[^A-Za-z0-9_.-]+/g, '_') || 'input.csv';
}

function toIndex(value, fallback = 0) {
  return Number.isFinite(value) ? Math.max(0, Math.trunc(value)) : fallback;
}

function schema() {
  ensureView();
  const columns = vectorToArray(view.headers());
  return {
    columns,
    fileSize: view.fileSize(),
    rowCount: view.rowCount(),
    csvOptions: { delimiter: view.delimiter(), quote: view.quote(), hasHeaders: view.hasHeaders(), bom: view.hasBom(),
      ...(view.headersDisabledReason() ? { headersDisabledReason: view.headersDisabledReason() } : {}) },
  };
}

function errorMessage(error) {
  // Emscripten C++ exceptions carry a native allocation. Decode and release it
  // after catching at the worker boundary, keeping validation errors recoverable.
  if (moduleInstance && (typeof error === 'number' || typeof error?.excPtr === 'number')) {
    try {
      const [type, message] = moduleInstance.getExceptionMessage(error);
      return message || type;
    } finally {
      moduleInstance.decrementExceptionRefcount(error);
    }
  }
  return error instanceof Error ? error.message : String(error);
}

const handlers = {
  async init() {
    if (!moduleInstance) {
      moduleInstance = await createCsvzallViewerModule({
        locateFile(path) {
          if (path.endsWith('.wasm')) {
            return wasmUrl;
          }
          return path;
        },
      });
      moduleInstance.FS.mkdirTree('/working');
    }
    return { ready: true };
  },

  open({ name, buffer }) {
    ensureReady();
    const nextPath = `/working/${activePath.startsWith('/working/0-') ? 1 : 0}-${safeFileName(name)}`;
    moduleInstance.FS.writeFile(nextPath, new Uint8Array(buffer));
    let next;
    try {
      next = moduleInstance.CsvViewData.open(nextPath);
    } catch (error) {
      moduleInstance.FS.unlink(nextPath);
      throw error;
    }
    view?.delete?.();
    if (activePath) moduleInstance.FS.unlink(activePath);
    activePath = nextPath;
    view = next;
    return schema();
  },

  readRows({ offset, limit, sortColumn, sortDirection }) {
    ensureView();
    const start = Number.isFinite(offset) ? Math.max(0, Math.trunc(offset)) : 0;
    const count = Number.isFinite(limit) ? Math.max(1, Math.trunc(limit)) : 1;
    if (sortColumn !== undefined || sortDirection !== undefined) {
      if (typeof sortColumn !== 'string' || !['asc', 'desc'].includes(sortDirection)) {
        throw new Error('Invalid row sort.');
      }
      const page = view.readSortedRows(start, count, sortColumn, sortDirection === 'desc');
      return { offset: start, rowIds: vectorToArray(page.rowIds),
        rows: rowsVectorToArrays(page.rows), changes: page.changes, totalRows: view.rowCount() };
    }
    const rows = rowsVectorToArrays(view.readRows(start, count));
    return {
      offset: start,
      rows,
      changes: view.readRowChanges(start, rows.length),
      totalRows: view.rowCount(),
    };
  },

  editCell({ rowIndex, column, value }) {
    ensureView();
    return { ok: true, edited: view.editCell(rowIndex, column, value ?? '') };
  },

  insertRow({ row, values }) {
    ensureView();
    const rowValues = arrayToVector(values);
    try {
      view.insertRow(toIndex(row, view.rowCount()), rowValues);
    } finally {
      rowValues.delete?.();
    }
    return schema();
  },

  deleteRow({ row }) {
    ensureView();
    view.deleteRow(toIndex(row));
    return schema();
  },

  swapRows({ first, second }) {
    ensureView();
    view.swapRows(toIndex(first), toIndex(second));
    return { ok: true };
  },

  insertColumn({ column, name, value }) {
    ensureView();
    view.insertColumn(toIndex(column, vectorToArray(view.headers()).length), name ?? '', value ?? '');
    return schema();
  },

  renameColumn({ column, name }) {
    ensureView();
    view.renameColumn(column ?? '', name ?? '');
    return schema();
  },

  deleteColumn({ column }) {
    ensureView();
    view.deleteColumn(column ?? '');
    return schema();
  },

  csvOptions({ delimiter, quote, hasHeaders }) {
    ensureView();
    view.setCsvOptions(delimiter, quote, hasHeaders);
    return schema();
  },

  readCsvOptions({ delimiter, quote, hasHeaders }) {
    ensureView();
    view.readCsvOptions(delimiter, quote, hasHeaders);
    return schema();
  },

  reset() {
    ensureView();
    view.reset();
    return schema();
  },

  prepareSave() {
    ensureView();
    try {
      view.writeSaveFile(preparedSavePath);
      const bytes = moduleInstance.FS.readFile(preparedSavePath);
      preparedSaveId = ++nextSaveId;
      return { buffer: bytes.buffer, byteOffset: bytes.byteOffset, byteLength: bytes.byteLength, saveId: preparedSaveId };
    } catch (error) {
      discardPreparedSave();
      throw error;
    }
  },

  finishSave({ saveId, commit }) {
    if (!preparedSaveId || saveId !== preparedSaveId) return null;
    try {
      if (!commit) return null;
      view.commitSaveFile(preparedSavePath);
      return schema();
    } finally {
      discardPreparedSave();
    }
  },
};

self.addEventListener('message', (event) => {
  const { id, type, payload } = event.data ?? {};
  if (!id || !type) {
    return;
  }

  Promise.resolve()
    .then(() => {
      const handler = handlers[type];
      if (!handler) {
        throw new Error(`Unknown worker request: ${type}`);
      }
      // A newer document or edit supersedes the snapshot while the host writes.
      if (preparedSaveId && type !== 'readRows' && type !== 'finishSave') discardPreparedSave();
      return handler(payload ?? {});
    })
    .then((result) => {
      if (result?.buffer instanceof ArrayBuffer) {
        self.postMessage({ id, ok: true, result }, [result.buffer]);
      } else {
        self.postMessage({ id, ok: true, result });
      }
    })
    .catch((error) => {
      self.postMessage({
        id,
        ok: false,
        error: errorMessage(error),
      });
    });
});

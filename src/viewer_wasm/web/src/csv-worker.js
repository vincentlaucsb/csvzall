import createCsvzallViewerModule from './generated/csvzall_viewer_wasm.js';
import wasmUrl from './generated/csvzall_viewer_wasm.wasm?url';

let moduleInstance = null;
let view = null;
let activePath = '';

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

function schema() {
  ensureView();
  const columns = vectorToArray(view.headers());
  return {
    columns,
    fileSize: view.fileSize(),
    rowCount: view.rowCount(),
  };
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
    view?.delete?.();
    view = null;
    activePath = `/working/${safeFileName(name)}`;
    moduleInstance.FS.writeFile(activePath, new Uint8Array(buffer));
    view = moduleInstance.CsvViewData.open(activePath);
    return schema();
  },

  readRows({ offset, limit }) {
    ensureView();
    const start = Number.isFinite(offset) ? Math.max(0, Math.trunc(offset)) : 0;
    const count = Number.isFinite(limit) ? Math.max(1, Math.trunc(limit)) : 1;
    return {
      offset: start,
      rows: rowsVectorToArrays(view.readRows(start, count)),
      totalRows: view.rowCount(),
    };
  },

  editCell({ rowIndex, column, value }) {
    ensureView();
    view.editCell(rowIndex, column, value ?? '');
    return { ok: true };
  },

  reset() {
    ensureView();
    view.reset();
    return schema();
  },

  save() {
    ensureView();
    view.save();
    const bytes = moduleInstance.FS.readFile(activePath);
    const copy = bytes.slice();
    return { buffer: copy.buffer, byteOffset: copy.byteOffset, byteLength: copy.byteLength };
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
        error: error instanceof Error ? error.message : String(error),
      });
    });
});

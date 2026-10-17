import { createGrid } from 'ag-grid-community';
import { createContextMenu, createDropdownMenu } from 'popright';
import 'ag-grid-community/styles/ag-grid.css';
import 'ag-grid-community/styles/ag-theme-alpine.css';
import '../../../viewer/viewer.css';
import { createViewerShell } from '../../../viewer/modules/shell.mjs';
import { createWasmAdapter } from './wasm-adapter.js';
import { createHostBridge } from './host-bridge.js';
import { setupFileDrop } from './file-drop.js';
import { setupPwa } from './pwa.js';
import CsvWorker from './csv-worker.js?worker';

// No SQL or chart imports: Vite cannot pull native features into Lite.
const shell = createViewerShell({
  backend: createWasmAdapter(new CsvWorker()),
  createGrid, createContextMenu, createDropdownMenu, features: [],
});
const host = createHostBridge({
  onOpenFile: file => shell.openBuffer(file.name || 'input.csv', file.buffer, 'Host file'),
  onHostModeChange: shell.setHostMode,
  onViewportResize: shell.onViewportResize,
  onSaveRequested: shell.save,
  getDirty: () => shell.dirty,
});
shell.connectHost(host);
host.start();
setupFileDrop({
  overlay: document.getElementById('drop-overlay'),
  enabled: () => !host.isHostMode(),
  onFile: shell.requestOpenFile, onStatus: shell.setStatus,
});
setupPwa({ installButton: document.getElementById('install-app'), onLaunchFile: shell.requestOpenFile });
window.addEventListener('pagehide', () => { host.destroy(); shell.destroy(); }, { once: true });
shell.start().then(() => host.markReady()).catch(error => shell.setStatus(error.message));

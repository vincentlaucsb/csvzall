import { postDirtyState } from './modules/dirty-state.mjs';
import { createFallbackContextMenu } from './modules/fallback-menu.mjs';
import { installHostCsvUpdates, installHostSaveRequests } from './modules/save.mjs';
import { createViewerShell } from './modules/shell.mjs';
import { createNativeAdapter } from './modules/adapters/native.mjs';
import { sqlFeature } from './modules/features/sql.mjs';
import { chartsFeature } from './modules/features/charts.mjs';

async function loadMenuFactories() {
  try {
    const popright = await import('popright');
    return {
      createContextMenu: popright.createContextMenu,
      createDropdownMenu: popright.createDropdownMenu ?? null,
    };
  } catch (error) {
    return { createContextMenu: createFallbackContextMenu, createDropdownMenu: null };
  }
}

if (typeof window !== 'undefined' && typeof document !== 'undefined') {
  const createGrid = (element, options) => window.agGrid.createGrid(element,options);
  loadMenuFactories().then(async menus => {
    const backend = createNativeAdapter();
    const shell = createViewerShell({ backend, createGrid, ...menus, features: [sqlFeature, chartsFeature] });
    shell.connectHost({ isHostMode: () => window.parent !== window, emitDirtyState: dirty => postDirtyState(dirty) });
    const removeSave = installHostSaveRequests({ save: async () => { await shell.save(); return true; }, getDirty: () => shell.dirty });
    const removeUpdates = installHostCsvUpdates({ shell, source: 'csvzall-viewer' });
    window.addEventListener('pagehide', () => { removeUpdates(); removeSave(); shell.destroy(); }, {once:true});
    await shell.start();
  }).catch(error => { const status = document.getElementById('status'); if (status) status.textContent = error.message; });
}

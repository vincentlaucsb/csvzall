import { applyCsvOptionsHostUi, updateHeaderToggle } from './csv-options.mjs';
import { applyHostGridTheme } from './host-theme.mjs';
import './dialog-dismiss.mjs';
import { mountViewerShell } from './shell-markup.mjs';
import { createHostGridAdapter } from './host-grid.mjs';
import { createSaveState } from './save-state.mjs';
import { createMutationTracker } from './save.mjs';
import { currentGridColumns, rowsToObjects, rowInsertionIndex } from './grid.mjs';
import { createTablerIcon as createIcon, decorateButton, decorateIconButton, decorateMenuButton } from './icons.mjs';
import { readLocalFile } from './local-file.mjs';
import { installUnsavedChangesBeforeUnload } from './dirty-state.mjs';
import { createCompactLayout } from './compact-layout.mjs';

/** Backend: capabilities, request, save, persist, destroy. Features: {id, mount(shell)}. */
export function createViewerShell({ backend, createGrid, createContextMenu, createDropdownMenu, features = [] }) {
  mountViewerShell();
  applyHostGridTheme(document.getElementById('grid'), document.documentElement.dataset.csvzallHostTheme);
  const columnFeatures = [];
  const menus = [];
  const dropdowns = new Map();
  let compactLayout = null;
  let statusMessage = '', briefStatusMessage = '';
  const featureDisposers = [];
  const fileInput = document.getElementById('file-input');
  const filePicker = document.querySelector('.file-picker');
  const statusNode = document.getElementById('status');
  const recordCountNode = document.getElementById('record-count');
  const saveButton = document.getElementById('save');
  const resetButton = document.getElementById('reset');
  const csvOptionsButton = document.getElementById('csv-options');
  const csvOptionsDialog = document.getElementById('csv-options-dialog');
  const csvOptionsForm = document.getElementById('csv-options-form');
  const csvDelimiter = document.getElementById('csv-options-delimiter');
  const csvQuote = document.getElementById('csv-options-quote');
  const csvHasHeaders = document.getElementById('csv-options-has-headers');
  const csvHeaderWarning = document.getElementById('csv-options-header-warning');
  document.getElementById('csv-options-warning-icon').append(createIcon('alert-triangle'));
  const csvInputDelimiter = document.getElementById('csv-input-delimiter');
  const csvInputQuote = document.getElementById('csv-input-quote');
  const csvInputRead = document.getElementById('csv-input-read');
  const csvOptionsError = document.getElementById('csv-options-error');
  const insertMenuButton = document.getElementById('insert-menu');
  const deleteMenuButton = document.getElementById('delete-menu');
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

  const pendingCellEdits = new Set();
  let viewOpen = false;
  let editable = true;
  let restoringCell = false;
  let resultMode = false;
  let activeName = '';
  let activeColumns = [];
  let csvOptions = null;
  let readingCsv = false;
  let rowCount = 0;
  let viewGeneration = 0;
  let dirty = false;
  let dataDirty = false;
  let pendingInsertColumn = 0;
  let pendingRenameColumn = '';
  let hostBridge = null;
  let engineReady = false;
  let pendingOpenFile = null;
  const saveState = createSaveState();
  const csvMutations = createMutationTracker(() => setDirty(true));
  const mutationTypes = new Set(['editCell', 'insertColumn', 'insertRow', 'deleteRow', 'swapRows', 'deleteColumn', 'renameColumn', 'reset', 'csvOptions']);
  const hostGrid = createHostGridAdapter({
    getGrid: () => gridApi,
    isHostMode: () => hostBridge?.isHostMode() ?? false,
  });

  function renderStatus() {
    if (!resultMode) statusNode.textContent = compactLayout?.compact && hostBridge?.isHostMode() ? briefStatusMessage : statusMessage;
  }
  function setStatus(message, brief = message) {
    statusMessage = message; briefStatusMessage = brief;
    renderStatus();
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

  function request(type, payload = {}, transfer = []) {
    if ((mutationTypes.has(type) || type === 'readCsvOptions') && (!editable || !viewOpen)) return Promise.reject(new Error('This CSV is read-only.'));
    return mutationTypes.has(type) ? csvMutations.run(() => sendRequest(type, payload, transfer)) : sendRequest(type, payload, transfer);
  }

  function sendRequest(type, payload, transfer) {
    return backend.request(type, payload, transfer);
  }

  function updateRecordCount(nextRowCount = rowCount) {
    if (resultMode) return;
    recordCountNode.textContent =
      `${nextRowCount.toLocaleString()} rows, ${activeColumns.length.toLocaleString()} columns`;
  }

  function refreshActionState() {
    saveButton.disabled = !viewOpen || !dirty;
    resetButton.disabled = !viewOpen || !dirty;
    csvOptionsButton.disabled = !viewOpen || !editable || !csvOptions;
    insertMenuButton.disabled = !viewOpen;
    deleteMenuButton.disabled = !viewOpen;
  }

  function setDirty(nextDirty, { force = false, columnOrder = false } = {}) {
    if (nextDirty || columnOrder) saveState.changed();
    if (!columnOrder) dataDirty = nextDirty;
    if (columnOrder) nextDirty = dataDirty || nextDirty;
    const changed = dirty !== nextDirty;
    dirty = nextDirty;
    refreshActionState();
    if (changed || force) {
      hostBridge?.emitDirtyState(dirty);
    }
  }

  function applySchema(schema) {
    activeColumns = schema.columns;
    csvOptions = schema.csvOptions ?? null;
    rowCount = schema.rowCount;
    updateRecordCount(rowCount);
  }

  function markDataDirty(message = `Unsaved changes in ${activeName}.`) {
    setDirty(true);
    setStatus(message, 'Unsaved changes.');
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

  function setHostMode(enabled, hostUi) {
    document.body.toggleAttribute('data-host-mode', enabled);
    applyCsvOptionsHostUi(csvOptionsButton, csvOptionsDialog, enabled, hostUi);
    if (filePicker) {
      filePicker.hidden = enabled;
    }
    document.getElementById('file-tools').hidden = enabled || !backend.capabilities.localFiles;
    fileInput.disabled = enabled || !engineReady;
    decorateButton(saveButton, 'device-floppy', enabled || !backend.capabilities.localFiles ? 'Save' : 'Download CSV');
    compactLayout?.refresh();
  }

  const gridOptions = {
    columnDefs: [],
    defaultColDef: {
      editable: () => editable,
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
    suppressMovableColumns: !backend.capabilities.columnOrder,
    onColumnMoved(event) {
      if (editable && viewOpen && event.finished && backend.capabilities.columnOrder) {
        const columns = currentGridColumns(gridApi, activeColumns);
        setDirty(!columns.every((column, index) => column === activeColumns[index]), { columnOrder: true });
      }
    },
    onCellEditingStarted(event) { compactLayout?.onCellEditingStarted(); hostGrid.onCellEditingStarted(event); },
    onCellEditingStopped(event) { compactLayout?.onCellEditingStopped(); hostGrid.onCellEditingStopped(event); },
    onCellValueChanged(event) {
      if (restoringCell || !editable || !viewOpen || !event.colDef.field || event.colDef.field === '_csvzallRowId') {
        return;
      }
      const pending = commitCellEdit(event);
      pendingCellEdits.add(pending);
      void pending.finally(() => pendingCellEdits.delete(pending));
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
      editable: () => editable,
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
    const dropdown = { items, onSelect };
    dropdowns.set(button, dropdown);
    if (!createDropdownMenu) {
      const menu = createContextMenu({ trigger: 'manual', items, theme: 'system', onSelect, onClose: () => compactLayout?.refresh() });
      button.addEventListener('click', event => {
        const rect = button.getBoundingClientRect();
        menu.open({ x: rect.left, y: rect.bottom + 4, target: button, triggerEvent: event });
      });
      menus.push(menu);
      dropdown.menu = menu;
      return menu;
    }
    const menu = createDropdownMenu(button, {
      items,
      theme: 'system',
      onSelect,
      onClose: () => compactLayout?.refresh(),
    });
    menus.push(menu);
    dropdown.menu = menu;
    return menu;
  }

  async function commitCellEdit(event) {
    try {
      await request('editCell', {
        rowIndex: event.data._csvzallRowId,
        column: event.colDef.field,
        value: event.newValue ?? '',
      });
      markDataDirty();
    } catch (error) {
      restoringCell = true;
      try { event.node.setDataValue(event.colDef.field, event.oldValue ?? ''); }
      finally { restoringCell = false; }
      setStatus(error instanceof Error ? error.message : 'Edit failed');
    }
  }

  function loadView(schema, name) {
    viewOpen = true;
    activeName = name;
    document.getElementById('file-name').textContent = name;
    document.getElementById('summary').textContent = '';
    editable = schema.editable !== false;
    document.getElementById('edit-toolbar').hidden = !editable;
    applySchema(schema);
    viewGeneration += 1;
    const generation = viewGeneration;
    setGridColumnDefs(activeColumns);
    setDatasource({
      async getRows(params) {
        try {
          const offset = params.startRow;
          const limit = Math.max(params.endRow - params.startRow, 1);
          const page = await request('readRows', { offset, limit });
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
    setStatus(`Loaded ${name}.`, 'Loaded.');
  }

  async function openBuffer(name, buffer, sourceLabel) {
    saveState.changed();
    viewOpen = false;
    rowCount = 0;
    activeColumns = [];
    setDirty(false);
    refreshActionState();
    showLoading('Opening CSV', `${sourceLabel || name} is being indexed in the background.`);
    setStatus(`Opening ${name}...`);
    try {
      const schema = await request('open', { name, buffer }, [buffer]);
      loadView(schema, name);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : 'Open failed');
    } finally {
      hideLoading();
    }
  }

  async function openFile(file) {
    try {
      const { name, buffer } = await readLocalFile(file, {
        onStart(name) {
          showLoading('Opening CSV', `${name} is being read from your device.`);
          setStatus(`Opening ${name}...`);
        },
      });
      await openBuffer(name, buffer, name);
    } catch (error) {
      hideLoading();
      setStatus(error instanceof Error ? error.message : 'Could not read the selected file.');
    }
  }

  async function openFileWhenReady(file) {
    if (!engineReady) {
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
    const schema = await request('insertColumn', { column, name, value: '' });
    applySchema(schema);
    setGridColumnDefs(activeColumns);
    refreshRows();
    markDataDirty();
  }

  async function insertRowAt(row) {
    const values = activeColumns.map(() => '');
    try {
      const schema = await request('insertRow', { row, values });
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
      const schema = await request('deleteRow', { row });
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
      await request('swapRows', { first: row, second: target });
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
      const schema = await request('deleteColumn', { column });
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
      const schema = await request('renameColumn', { column, name });
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
    if (csvOptions?.hasHeaders === false) {
      void insertColumnAt(pendingInsertColumn, '').catch(error => setStatus(error.message));
      return;
    }
    insertColumnName.value = '';
    clearColumnError();
    showDialog(insertColumnDialog, insertColumnName);
  }

  function showRenameColumnDialog(column) {
    if (csvOptions?.hasHeaders === false) {
      setStatus('Synthetic column labels cannot be renamed.');
      return;
    }
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
    items: () => [
      ...columnFeatures.map(feature => ({ id: feature.id, label: feature.label, icon: () => createIcon(feature.icon) })),
      ...(editable ? [
      { id: 'rename-column', label: 'Rename Column', icon: () => createIcon('pencil'), disabled: csvOptions?.hasHeaders === false },
      { type: 'separator' },
      { id: 'delete-column', label: 'Delete Column', icon: () => createIcon('column-remove'), variant: 'danger' },
      ] : []),
    ],
    onSelect(event) {
      const column = event.context.data?.column;
      const feature = columnFeatures.find(item => item.id === event.id);
      if (feature) { event.close(); feature.run(column); return; }
      if (event.id === 'rename-column' && typeof column === 'string') {
        showRenameColumnDialog(column);
      }
      if (event.id === 'delete-column' && typeof column === 'string') {
        void deleteColumnByName(column);
      }
    },
  });

  gridOptions.onCellContextMenu = (event) => {
    if (!editable || resultMode) return;
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
    if (!editable && !columnFeatures.length) return;
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

  csvOptionsButton.addEventListener('click', async () => {
    if (csvOptionsButton.hidden) return;
    await finishEditing();
    await csvMutations.settle();
    if (csvOptionsButton.hidden || !viewOpen || !editable || !csvOptions) return;
    csvDelimiter.value = csvOptions.delimiter === '\t' ? '\\t' : csvOptions.delimiter;
    csvQuote.value = csvOptions.quote;
    csvInputDelimiter.value = csvDelimiter.value;
    csvInputQuote.value = csvQuote.value;
    csvInputRead.disabled = dirty;
    document.getElementById('csv-input-help').textContent = dirty
      ? 'Save or reset changes before changing the input dialect.'
      : 'Reading changes the view without writing the file.';
    updateHeaderToggle(csvHasHeaders, document.getElementById('csv-options-header-help'), csvOptions, csvHeaderWarning);
    document.getElementById('csv-options-current').textContent =
      `Current dialect: delimiter ${JSON.stringify(csvOptions.delimiter)}, quote ${csvOptions.quote ? JSON.stringify(csvOptions.quote) : 'none'}. BOM: ${csvOptions.bom ? 'present' : 'absent'}.`;
    document.getElementById('csv-options-headers').textContent =
      `${csvOptions.hasHeaders ? 'Headers' : 'Display labels'}: ${activeColumns.join(', ')}`;
    csvOptionsError.hidden = true;
    showDialog(csvOptionsDialog, csvHasHeaders.disabled ? csvDelimiter : csvHasHeaders);
  });
  csvInputRead.addEventListener('click', async () => {
    if (dirty || saving || readingCsv) return;
    readingCsv = true;
    csvInputRead.disabled = true;
    const apply = document.getElementById('csv-options-apply');
    apply.disabled = true;
    const generation = viewGeneration;
    try {
      const schema = await request('readCsvOptions', {
        delimiter: csvInputDelimiter.value === '\\t' ? '\t' : csvInputDelimiter.value,
        quote: csvInputQuote.value, hasHeaders: csvHasHeaders.checked,
      });
      if (generation !== viewGeneration) return;
      loadView(schema, activeName);
      csvOptionsDialog.close('read');
    } catch (error) {
      csvOptionsError.textContent = error instanceof Error ? error.message : 'Could not read this dialect.';
      csvOptionsError.hidden = false;
    } finally { readingCsv = false; csvInputRead.disabled = dirty; apply.disabled = false; }
  });
  document.getElementById('csv-options-standard').addEventListener('click', () => {
    csvDelimiter.value = ',';
    csvQuote.value = '"';
  });
  document.getElementById('csv-options-cancel').addEventListener('click', () => csvOptionsDialog.close('cancel'));
  csvOptionsForm.addEventListener('submit', async event => {
    event.preventDefault();
    const apply = document.getElementById('csv-options-apply');
    if (apply.disabled || saving || readingCsv) return;
    apply.disabled = true;
    try {
      const next = { delimiter: csvDelimiter.value === '\\t' ? '\t' : csvDelimiter.value,
        quote: csvQuote.value, hasHeaders: csvHasHeaders.checked };
      if (next.delimiter !== csvOptions.delimiter || next.quote !== csvOptions.quote || next.hasHeaders !== csvOptions.hasHeaders) {
        const order = currentGridColumns(gridApi, activeColumns).map(column => activeColumns.indexOf(column));
        const schema = await request('csvOptions', next);
        applySchema(schema);
        setGridColumnDefs(activeColumns);
        if (backend.capabilities.columnOrder) gridApi.applyColumnState({ state: order.map(index => ({ colId: activeColumns[index] })), applyOrder: true });
        refreshRows();
        markDataDirty('CSV options applied. Save to write the current dialect and header mode.');
      }
      csvOptionsDialog.close('apply');
    } catch (error) {
      csvOptionsError.textContent = error instanceof Error ? error.message : 'Could not apply CSV options.';
      csvOptionsError.hidden = false;
    } finally { apply.disabled = false; }
  });

  fileInput.addEventListener('change', () => {
    const file = fileInput.files?.[0];
    fileInput.value = '';
    if (file) {
      void requestOpenFile(file);
    }
  });

  let saving = false;
  async function saveCurrentChanges() {
    if (!editable) throw new Error('This CSV is read-only.');
    if (!viewOpen) {
      throw new Error('No CSV is open.');
    }
    if (saving) throw new Error('A save is already in progress.');
    if (readingCsv) throw new Error('The input dialect is being applied.');
    saving = true;
    const wasInert = document.body.inert;
    document.body.inert = true;
    try {
      gridApi.stopEditing();
      await new Promise(resolve => window.setTimeout(resolve, 0));
      await Promise.all([...pendingCellEdits]);
      await csvMutations.settle();
      showLoading('Saving CSV', `${activeName} is being rewritten with your edits.`);
      const name = activeName;
      let savedResult;
      await saveState.save({
        async prepare() {
          savedResult = await backend.save({ columns: currentGridColumns(gridApi, activeColumns) });
          return savedResult;
        },
        persist: result => backend.persist(result, { name, hostBridge, downloadBytes }),
        onSaved() {
          if (savedResult.schema) {
            const changed = JSON.stringify(activeColumns) !== JSON.stringify(savedResult.schema.columns) || savedResult.schema.csvOptions?.hasHeaders === false;
            applySchema(savedResult.schema);
            if (changed) setGridColumnDefs(activeColumns);
          }
          setDirty(false);
          refreshRows();
        },
      });
      setStatus(savedResult.chartError ? 'Saved. Chart generation failed: ' + savedResult.chartError : 'Saved ' + name + '.',
        savedResult.chartError ? 'Saved. Chart generation failed: ' + savedResult.chartError : 'Saved.');
    } catch (error) {
      setStatus(error instanceof Error ? error.message : 'Download failed');
      throw error;
    } finally {
      hideLoading();
      document.body.inert = wasInert;
      saving = false;
    }
  }

  saveButton.addEventListener('click', () => { void saveCurrentChanges().catch(() => {}); });

  async function resetCurrentChanges() {
    if (!viewOpen) return;
    await finishEditing();
    await csvMutations.settle();
    showLoading('Resetting CSV', `${activeName} is being restored from the original bytes.`);
    try {
      const schema = await request('reset');
      saveState.changed();
      applySchema(schema);
      setGridColumnDefs(activeColumns);
      setDirty(false);
      refreshRows();
      setStatus(`Reset ${activeName}.`, 'Reset.');
    } catch (error) {
      setStatus(error instanceof Error ? error.message : 'Reset failed');
    } finally {
      hideLoading();
    }
  }
  resetButton.addEventListener('click', () => { void resetCurrentChanges(); });


  async function finishEditing() {
    gridApi.stopEditing();
    await new Promise(resolve => window.setTimeout(resolve, 0));
    await Promise.all([...pendingCellEdits]);
  }
  decorateMenuButton(insertMenuButton, 'plus', 'Insert');
  decorateMenuButton(deleteMenuButton, 'trash', 'Delete');
  decorateButton(resetButton, 'restore', 'Reset');
  decorateButton(csvOptionsButton, 'settings', 'Options');
  decorateButton(saveButton, 'device-floppy', 'Save');
  decorateIconButton(document.getElementById('more-actions'), 'dots', 'More actions');
  compactLayout = createCompactLayout({ toolbar: document.querySelector('.toolbar-actions'),
    moreButton: document.getElementById('more-actions'), createMenu: createToolbarDropdown, dropdowns, onChange: renderStatus });
  const removeBeforeUnload = installUnsavedChangesBeforeUnload({ get dirty() { return dirty; } });
  const shell = {
    backend, gridApi, gridElement, createGrid,
    get schema() { return { file: activeName, columns: [...activeColumns], totalRows: rowCount, editable }; },
    get dirty() { return dirty; },
    get saving() { return saving; },
    finishEditing: async () => { await finishEditing(); await csvMutations.settle(); },
    save: saveCurrentChanges, reset: resetCurrentChanges, openBuffer, requestOpenFile,
    setStatus, refreshRows,
    restoreTable() { resultMode = false; updateRecordCount(); setStatus(dirty ? 'Unsaved changes.' : 'Ready.'); },
    setResultMode(value) { resultMode = value; },
    addColumnAction(action) { columnFeatures.push(action); },
    connectHost(host) { hostBridge = host; setHostMode(host.isHostMode()); },
    setHostMode, onViewportResize: hostGrid.onViewportResize,
    async start() {
      try {
        const initial = await backend.request('init');
        engineReady = true;
        if (initial?.columns) loadView(initial, initial.file);
        if (pendingOpenFile) { const file = pendingOpenFile; pendingOpenFile = null; await openFile(file); }
        fileInput.disabled = !backend.capabilities.localFiles || hostBridge?.isHostMode();
        for (const feature of features) {
          if (!feature.id || typeof feature.mount !== 'function') throw new Error('Invalid viewer feature');
          const mounted = feature.mount(shell);
          if (mounted?.destroy) featureDisposers.push(() => mounted.destroy());
        }
        if (!viewOpen) { document.getElementById('file-name').textContent = 'csvzall'; setStatus('Open a local CSV file to begin.'); }
      } catch (error) { setStatus(error.message); throw error; }
    },
    destroy() {
      for (const dispose of featureDisposers.reverse()) dispose();
      for (const menu of [...menus, rowMenu, columnMenu]) menu?.destroy?.();
      compactLayout.destroy(); removeBeforeUnload?.(); hostGrid.destroy(); gridApi.destroy(); backend.destroy?.();
    },
  };
  refreshActionState();
  setHostMode(false);
  return shell;
}

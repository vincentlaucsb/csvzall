import { createAutosave } from './autosave.mjs';
import { applyCsvOptionsHostUi, updateHeaderToggle } from './csv-options.mjs';
import { applyHostGridTheme } from './host-theme.mjs';
import { createDialog } from './dialog.mjs';
import { mountViewerShell } from './shell-markup.mjs';
import { createHostGridAdapter } from './host-grid.mjs';
import { createSaveState } from './save-state.mjs';
import { createMutationTracker } from './save.mjs';
import { createPagedDatasource, currentGridColumns, editedColumns, entryRow, extendEntryRowBlock, insertedRow, makeEntryRow, rowsToObjects, rowInsertionIndex, sourceRowId } from './grid.mjs';
import { createTablerIcon as createIcon, decorateButton, decorateIconButton, decorateMenuButton } from './icons.mjs';
import { readLocalFile } from './local-file.mjs';
import { installUnsavedChangesBeforeUnload } from './dirty-state.mjs';
import { createCompactLayout } from './compact-layout.mjs';

/** Backend: capabilities, request, save, persist, destroy. Features: {id, mount(shell)}. */
export function createViewerShell({ backend, createGrid, createContextMenu, createDropdownMenu, features = [] }) {
  const cacheBlockSize = 500;
  backend.prefetchFirstPage?.(cacheBlockSize);
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
  const autoSaveControl = document.getElementById('autosave-control');
  const autoSaveSwitch = document.getElementById('autosave');
  const autoSaveValue = document.getElementById('autosave-value');
  const reloadExternalButton = document.getElementById('reload-external');
  const discardEditsDialog = createDialog(document.getElementById('discard-edits-dialog'));
  const discardEditsTitle = document.getElementById('discard-edits-title');
  const discardEditsDescription = document.getElementById('discard-edits-description');
  const discardEditsCancel = document.getElementById('discard-edits-cancel');
  const discardEditsConfirm = document.getElementById('discard-edits-confirm');
  const recoveryButton = document.getElementById('download-recovery');
  const conflictNotice = document.getElementById('csv-conflict');
  const resetButton = document.getElementById('reset');
  const csvOptionsButton = document.getElementById('csv-options');
  const csvOptionsDialog = createDialog(document.getElementById('csv-options-dialog'));
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
  const loadingDialog = createDialog(document.getElementById('loading-dialog'), { dismissible: false });
  const loadingTitleNode = document.getElementById('loading-title');
  const loadingDetailNode = document.getElementById('loading-detail');
  const insertColumnDialog = createDialog(document.getElementById('insert-column-dialog'));
  const insertColumnForm = document.getElementById('insert-column-form');
  const insertColumnName = document.getElementById('insert-column-name');
  const insertColumnError = document.getElementById('insert-column-error');
  const cancelInsertColumn = document.getElementById('cancel-insert-column');
  const renameColumnDialog = createDialog(document.getElementById('rename-column-dialog'));
  const renameColumnForm = document.getElementById('rename-column-form');
  const renameColumnName = document.getElementById('rename-column-name');
  const renameColumnError = document.getElementById('rename-column-error');
  const cancelRenameColumn = document.getElementById('cancel-rename-column');

  const pendingCellEdits = new Set();
  let saveEditDrain = null;
  const pendingCellFields = new WeakMap();
  const pendingEntryRows = new WeakMap();
  let viewOpen = false;
  let editable = true;
  let changingStructure = false;
  let pendingRowFocus = null;
  let resultMode = false;
  let activeName = '';
  let activeColumns = [];
  let csvOptions = null;
  let readingCsv = false;
  let rowCount = 0;
  let rowRevision = 0;
  let waitingForInitialCount = false;
  let viewGeneration = 0;
  let datasource;
  let dirty = false;
  let dataDirty = false;
  let columnOrderDirty = false;
  let pendingEntryCount = 0;
  let pendingInsertColumn = 0;
  let pendingRenameColumn = '';
  let hostBridge = null;
  let engineReady = false;
  let pendingOpenFile = null;
  let confirmingDiscard = false;
  let externalTimer;
  let pendingExternal;
  let externalConflict = false;
  let refreshingExternal = false;
  let saveBarrier;
  let requestRecovery;
  const recovery = [];
  const autoSave = createAutosave({ control: autoSaveControl, input: autoSaveSwitch, value: autoSaveValue }, {
    win: window,
    canSave: () => dirty && !externalConflict && !pendingExternal,
    save: () => saveCurrentChanges({ automatic: true }),
  });
  const scheduleAutoSave = autoSave.schedule;
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
    loadingDialog.show();
  }

  function hideLoading() {
    loadingDialog.close();
  }

  function queueMutation(type, payload, generation, notify) {
    const barrier = saveBarrier;
    if (hostBridge?.isHostMode() && type !== 'reset') recovery.push({ type, ...payload });
    return csvMutations.run(async () => {
      await barrier;
      if (!viewOpen || generation !== viewGeneration) throw new Error('The open CSV changed.');
      if (externalConflict) throw new Error('The CSV changed on disk. Save a copy of your edits or reload.');
      try {
        return await sendRequest(type, payload, []);
      } catch (error) {
        if (error?.conflict) externalChange({});
        throw error;
      }
    }, notify);
  }

  async function request(type, payload = {}, transfer = []) {
    if ((mutationTypes.has(type) || type === 'readCsvOptions') && (!editable || !viewOpen)) throw new Error('This CSV is read-only.');
    if (type === 'swapRows' && activeSort()) throw new Error('Clear the sort to move rows.');
    if (!mutationTypes.has(type)) return sendRequest(type, payload, transfer);
    const generation = viewGeneration;
    if (type === 'editCell') return queueMutation(type, payload, generation);
    if (changingStructure) throw new Error('Wait for the current table change to finish.');
    changingStructure = true;
    try {
      await finishEditing();
      return await queueMutation(type, payload, generation);
    } finally {
      changingStructure = false;
      scheduleExternal();
      scheduleAutoSave();
    }
  }

  function sendRequest(type, payload, transfer) {
    return backend.request(type, payload, transfer);
  }

  function updateRecordCount(nextRowCount = rowCount) {
    if (resultMode || waitingForInitialCount) return;
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
    if (columnOrder) columnOrderDirty = nextDirty;
    else {
      dataDirty = nextDirty;
      if (!nextDirty) columnOrderDirty = false;
    }
    refreshDirtyState(force);
  }

  function refreshDirtyState(force = false) {
    const nextDirty = dataDirty || columnOrderDirty || pendingEntryCount > 0;
    const changed = dirty !== nextDirty;
    dirty = nextDirty;
    refreshActionState();
    autoSave.edited();
    if (changed || force) {
      hostBridge?.emitDirtyState(dirty);
    }
  }

  function applySchema(schema) {
    activeColumns = schema.columns;
    csvOptions = schema.csvOptions ?? null;
    rowCount = schema.rowCount;
    updateRecordCount();
  }

  function activeSort() {
    return gridApi.getColumnState().find(column => column.sort === 'asc' || column.sort === 'desc');
  }

  function displayedRowCount() {
    return rowCount + Number(editable);
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

  const canEditCell = ({ data, colDef }) =>
    editable && Boolean(data) && !refreshingExternal && !externalConflict && !changingStructure && !pendingCellFields.get(data)?.has(colDef.field);
  // The infinite row model needs a numeric height to reposition cached rows.
  const rowMeasure = document.createElement('span');
  rowMeasure.className = 'csvzall-row-measure';
  gridElement.append(rowMeasure);
  function syncRowHeight() {
    const height = rowMeasure.offsetHeight;
    if (datasource && height > 0 && gridApi.getGridOption('rowHeight') !== height) {
      gridApi.setGridOption('rowHeight', height);
    }
  }
  const gridOptions = {
    loading: false,
    onGridStylesChanged(event) {
      if (event.rowHeightChanged) syncRowHeight();
    },
    columnDefs: [],
    readOnlyEdit: true,
    defaultColDef: {
      editable: canEditCell,
      resizable: true,
      sortable: true,
      unSortIcon: true,
      filter: false,
      minWidth: 140,
      flex: 1,
      valueFormatter: ({ data, value, column, api }) =>
        data?.[entryRow] && api.getAllDisplayedColumns()[0] === column ? 'Add row…' : value,
      cellClassRules: {
        'csvzall-edited-cell': ({ data, colDef }) => data?.[editedColumns]?.has(colDef.field) ?? false,
      },
    },
    rowClassRules: {
      'csvzall-entry-row': ({ data }) => data?.[entryRow] === true,
      'csvzall-inserted-row': ({ data }) => data?.[insertedRow] === true,
    },
    rowModelType: 'infinite',
    suppressMultiSort: true,
    suppressFieldDotNotation: true,
    rowSelection: {
      mode: 'singleRow',
      checkboxes: false,
      headerCheckbox: false,
      enableClickSelection: true,
      isRowSelectable: ({ data }) => Number.isInteger(data?.[sourceRowId]),
    },
    cacheBlockSize,
    maxBlocksInCache: 8,
    onBodyScroll(event) {
      if (event.direction === 'vertical') datasource?.onVerticalScroll();
    },
    animateRows: false,
    suppressColumnVirtualisation: false,
    suppressMovableColumns: !backend.capabilities.columnOrder,
    onColumnMoved(event) {
      if (editable && viewOpen && event.finished && backend.capabilities.columnOrder) {
        const columns = currentGridColumns(gridApi, activeColumns);
        setDirty(!columns.every((column, index) => column === activeColumns[index]), { columnOrder: true });
        const entry = gridApi.getRenderedNodes().find(node => node.data?.[entryRow]);
        if (entry) gridApi.refreshCells({ rowNodes: [entry], force: true });
      }
    },
    onCellEditingStarted(event) { compactLayout?.onCellEditingStarted(); hostGrid.onCellEditingStarted(event); },
    onCellEditingStopped(event) {
      compactLayout?.onCellEditingStopped();
      hostGrid.onCellEditingStopped(event);
      scheduleExternal();
      scheduleAutoSave();
    },
    onSortChanged() { pendingRowFocus = null; },
    onCellEditRequest(event) {
      const data = event.data;
      if (!editable || !viewOpen || typeof event.colDef.field !== 'string' ||
          (!data?.[entryRow] && !Number.isInteger(data?.[sourceRowId])) ||
          (data?.[entryRow] && (event.newValue ?? '') === '')) {
        return;
      }
      let fields = pendingCellFields.get(data);
      if (!fields) {
        fields = new Set();
        pendingCellFields.set(data, fields);
      }
      const field = event.colDef.field;
      if (fields.has(field)) return;
      fields.add(field);
      const pending = commitCellEdit(event, viewGeneration);
      pendingCellEdits.add(pending);
      saveEditDrain?.add(pending);
      void pending.finally(() => {
        fields.delete(field);
        pendingCellEdits.delete(pending);
        scheduleExternal();
        scheduleAutoSave();
      });
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
      editable: canEditCell,
      resizable: true,
      minWidth: 140,
      flex: 1,
    })));
  }

  function setDatasource({ getRows }) {
    datasource = createPagedDatasource(getRows, startRow =>
      Boolean(gridApi.getCacheBlockState()[startRow / gridOptions.cacheBlockSize]));
    gridApi.setGridOption('datasource', datasource);
    syncRowHeight();
  }

  function refreshRows() {
    pendingRowFocus = null;
    if (typeof gridApi.purgeInfiniteCache === 'function') {
      gridApi.purgeInfiniteCache();
    } else if (typeof gridApi.refreshInfiniteCache === 'function') {
      gridApi.refreshInfiniteCache();
    }
  }

  function selectedSourceRow() {
    const focused = gridApi.getFocusedCell();
    const focusedData = focused && gridApi.getDisplayedRowAtIndex(focused.rowIndex)?.data;
    if (focusedData?.[entryRow]) return rowCount;
    const selected = gridApi.getSelectedRows ? gridApi.getSelectedRows() : [];
    if (Number.isInteger(selected[0]?.[sourceRowId])) {
      return selected[0][sourceRowId];
    }
    if (Number.isInteger(focusedData?.[sourceRowId])) return focusedData[sourceRowId];
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

  async function commitCellEdit(event, generation) {
    const column = event.colDef.field;
    const value = event.newValue ?? '';
    const data = event.data;
    try {
      if (data[entryRow]) {
        const existing = pendingEntryRows.get(data);
        if (!existing) {
          const pending = appendEntryRow(event.node, data, column, value, generation);
          pendingEntryRows.set(data, pending);
          try { await pending; }
          finally { pendingEntryRows.delete(data); }
          return;
        }
        await existing;
        if (generation !== viewGeneration || !viewOpen) return;
      }
      const { edited } = await request('editCell', {
        rowIndex: data[sourceRowId],
        column,
        value,
      });
      if (!viewOpen || generation !== viewGeneration) return;
      markDataDirty();
      if (event.node.data !== data) return;
      data[column] = value;
      const columns = data[editedColumns] ??= new Set();
      if (edited) columns.add(column);
      else columns.delete(column);
      gridApi.refreshCells({ rowNodes: [event.node], columns: [event.column] });
    } catch (error) {
      if (!viewOpen || generation !== viewGeneration) return;
      const failure = error instanceof Error ? error : new Error('Edit failed');
      setStatus(failure.message);
      return failure;
    }
  }

  async function appendEntryRow(node, data, column, value, generation) {
    const values = activeColumns.map(field => field === column ? value : '');
    // Tab can start editing the next cell before the append is acknowledged.
    // Queue the append like a cell edit, without stopping the next editor.
    pendingEntryCount += 1;
    refreshDirtyState();
    let schema;
    try {
      schema = await queueMutation('insertRow', { values }, generation, () => saveState.changed());
      if (schema && generation === viewGeneration) markDataDirty();
    } finally {
      if (generation === viewGeneration) {
        pendingEntryCount -= 1;
        refreshDirtyState();
      }
    }
    if (!viewOpen || generation !== viewGeneration) return;
    applySchema(schema);
    rowRevision += 1;
    delete data[entryRow];
    data[sourceRowId] = rowCount - 1;
    data[insertedRow] = true;
    data[column] = value;
    extendEntryRowBlock(gridApi, node, data, rowCount, cacheBlockSize, activeColumns);
  }

  function loadView(schema, name, { deferInitialCount = false } = {}) {
    pendingRowFocus = null;
    pendingEntryCount = 0;
    viewOpen = true;
    activeName = name;
    document.getElementById('file-name').textContent = name;
    recovery.length = 0;
    document.getElementById('summary').textContent = '';
    editable = schema.editable !== false;
    document.getElementById('edit-toolbar').hidden = !editable;
    waitingForInitialCount = deferInitialCount && schema.rowCount > 0;
    applySchema(schema);
    viewGeneration += 1;
    const generation = viewGeneration;
    function showInitialCount() {
      if (generation !== viewGeneration || !waitingForInitialCount) return;
      waitingForInitialCount = false;
      updateRecordCount();
    }
    gridApi.applyColumnState({ defaultState: { sort: null } });
    setGridColumnDefs(activeColumns);
    setDatasource({
      async getRows(params) {
        try {
          const offset = params.startRow;
          const limit = Math.max(params.endRow - params.startRow, 1);
          const [sort] = params.sortModel;
          const payload = { offset, limit };
          if (sort) {
            Object.assign(payload, { sortColumn: sort.colId, sortDirection: sort.sort });
          }
          let page;
          let revision;
          do {
            if (pendingCellEdits.size) await Promise.all([...pendingCellEdits]);
            if (generation !== viewGeneration) return params.failCallback();
            revision = rowRevision;
            page = await request('readRows', payload);
            if (pendingEntryCount > 0) await Promise.all([...pendingCellEdits]);
            if (generation !== viewGeneration) return params.failCallback();
          } while (revision !== rowRevision);
          if (sort && (!Array.isArray(page.rowIds) || page.rowIds.length !== page.rows.length ||
              !page.rowIds.every(Number.isInteger))) {
            throw new Error('Sorted rows could not be matched to the CSV.');
          }
          const countChanged = rowCount !== page.totalRows;
          rowCount = page.totalRows;
          const rows = rowsToObjects(activeColumns, page.rows, page.offset, page.rowIds, page.changes);
          if (editable && offset + rows.length === rowCount && rows.length < limit) {
            rows.push(makeEntryRow(activeColumns));
          }
          params.successCallback(rows, displayedRowCount());
          const focus = pendingRowFocus;
          if (focus && focus.index >= offset && focus.index < offset + page.rows.length) {
            requestAnimationFrame(() => {
              if (pendingRowFocus !== focus) return;
              const row = gridApi.getDisplayedRowAtIndex(focus.index);
              if (row?.data?.[sourceRowId] !== focus.index || !row.data[insertedRow]) return;
              pendingRowFocus = null;
              const [column] = gridApi.getAllDisplayedColumns();
              if (column) gridApi.setFocusedCell(focus.index, column);
            });
          }
          if (waitingForInitialCount) {
            // Give the delivered rows a frame to paint before initializing locale formatting.
            requestAnimationFrame(() => requestAnimationFrame(showInitialCount));
          } else if (countChanged) {
            updateRecordCount();
          }
        } catch (error) {
          params.failCallback();
          showInitialCount();
          if (error?.conflict) externalChange({});
          else setStatus(error instanceof Error ? error.message : 'Row load failed');
        }
      },
    });
    setDirty(false, { force: hostBridge?.isHostMode() });
    setStatus(`Loaded ${name}.`, 'Loaded.');
  }

  async function openBuffer(name, buffer) {
    saveState.changed();
    const generation = ++viewGeneration;
    discardEditsDialog.close();
    pendingEntryCount = 0;
    viewOpen = false;
    rowCount = 0;
    activeColumns = [];
    setDirty(false);
    refreshActionState();
    setStatus(`Opening ${name}...`);
    try {
      const schema = await request('open', { name, buffer }, [buffer]);
      if (generation === viewGeneration) {
        loadView(schema, name);
        scheduleExternal();
      }
    } catch (error) {
      if (generation === viewGeneration) {
        setStatus(error instanceof Error ? error.message : 'Open failed');
      }
    }
  }

  async function openFile(file) {
    try {
      const { name, buffer } = await readLocalFile(file, {
        onStart(name) {
          setStatus(`Opening ${name}...`);
        },
      });
      await openBuffer(name, buffer);
    } catch (error) {
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
    if (confirmingDiscard) return;
    if (hasLocalChanges() && !await confirmDiscardEdits(
      `Open ${file.name}?`,
      `Opening this file discards unsaved edits to ${activeName}.`,
      'Discard edits and open',
    )) return;
    await openFileWhenReady(file);
  }

  async function openHostFile(file) {
    await openBuffer(file.name || 'input.csv', file.buffer);
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
    const append = row === undefined || Boolean(activeSort());
    const values = activeColumns.map(() => '');
    try {
      const schema = await request('insertRow', append ? { values } : { row, values });
      applySchema(schema);
      refreshRows();
      if (append) {
        pendingRowFocus = { index: schema.rowCount - 1 };
        gridApi.setRowCount(displayedRowCount(), true);
        gridApi.ensureIndexVisible(pendingRowFocus.index, 'bottom');
      }
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
    insertColumnDialog.show(insertColumnName);
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
    renameColumnDialog.show(renameColumnName);
    renameColumnName.select();
  }

  const rowMenu = createContextMenu({
    trigger: 'manual',
    theme: 'system',
    items: ({ data }) => {
      const row = data?.row;
      const sorted = Boolean(activeSort());
      return [
        { id: 'move-up', label: 'Move Up', icon: () => createIcon('arrow-up'), disabled: sorted || !Number.isInteger(row) || row <= 0 },
        { id: 'move-down', label: 'Move Down', icon: () => createIcon('arrow-down'), disabled: sorted || !Number.isInteger(row) || row >= rowCount - 1 },
        { type: 'separator' },
        ...rowInsertItems('Insert row '),
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
      if (event.id === 'add-row') void insertRowAt();
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
    items: ({ data }) => [
      ...(data?.column && activeSort()?.colId === data.column ? [{ id: 'clear-sort', label: 'Clear sort' }, { type: 'separator' }] : []),
      ...columnFeatures.map(feature => ({ id: feature.id, label: feature.label, icon: () => createIcon(feature.icon) })),
      ...(editable ? [
      { id: 'rename-column', label: 'Rename Column', icon: () => createIcon('pencil'), disabled: csvOptions?.hasHeaders === false },
      { type: 'separator' },
      { id: 'delete-column', label: 'Delete Column', icon: () => createIcon('column-remove'), variant: 'danger' },
      ] : []),
    ],
    onSelect(event) {
      const column = event.context.data?.column;
      if (event.id === 'clear-sort' && activeSort()?.colId === column) {
        gridApi.stopEditing();
        gridApi.applyColumnState({ defaultState: { sort: null } });
        return;
      }
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
    const row = event.data?.[sourceRowId];
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
    if (!editable && !columnFeatures.length && activeSort()?.colId !== column) return;
    columnMenu.open({
      x: event.clientX,
      y: event.clientY,
      target: headerCell,
      triggerEvent: event,
      context: { column },
    });
  });

  function rowInsertItems(prefix, includeAppend = false) {
    const icon = () => createIcon('row-insert-bottom');
    const addRow = { id: 'add-row', label: 'Add row', icon };
    return activeSort()
      ? [addRow]
      : [
          ...(includeAppend ? [addRow] : []),
          { id: 'insert-row-before', label: `${prefix}before`, icon },
          { id: 'insert-row-after', label: `${prefix}after`, icon },
        ];
  }

  createToolbarDropdown(insertMenuButton, () => [
    ...rowInsertItems('Row ', true),
    { type: 'separator' },
    { id: 'insert-column-before', label: 'Column Before', icon: () => createIcon('column-insert-right') },
    { id: 'insert-column-after', label: 'Column After', icon: () => createIcon('column-insert-right') },
  ], (event) => {
    if (event.id === 'add-row') void insertRowAt();
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
    csvOptionsDialog.show(csvHasHeaders.disabled ? csvDelimiter : csvHasHeaders);
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
    } finally { readingCsv = false; csvInputRead.disabled = dirty; apply.disabled = false; scheduleExternal(); scheduleAutoSave(); }
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
    } finally { apply.disabled = false; scheduleExternal(); scheduleAutoSave(); }
  });

  fileInput.addEventListener('change', () => {
    const file = fileInput.files?.[0];
    fileInput.value = '';
    if (file) {
      void requestOpenFile(file);
    }
  });

  function showExternalConflict() {
    externalConflict = true;
    if (!dirty && gridApi.getEditingCells().length) setDirty(true);
    reloadExternalButton.hidden = false;
    recoveryButton.hidden = false;
    conflictNotice.textContent = 'The CSV changed on disk. Save a copy of your edits before reloading if you want to keep them.';
    conflictNotice.hidden = false;
    statusNode.hidden = true;
  }

  function scheduleExternal() {
    window.clearTimeout(externalTimer);
    if (!pendingExternal || externalConflict) return;
    externalTimer = window.setTimeout(() => { void refreshExternal(); }, 0);
  }

  function externalChange(change) {
    if (change.revision !== undefined &&
        (hostBridge?.isCurrentRevision?.(change.revision) || change.revision <= (pendingExternal?.revision ?? 0))) return;
    pendingExternal = change;
    scheduleExternal();
  }

  function hasLocalChanges() {
    return dirty || gridApi.getEditingCells().length > 0 || pendingCellEdits.size > 0 || changingStructure;
  }

  async function refreshExternal({ discard = false } = {}) {
    const change = pendingExternal;
    if (!change || !viewOpen || saving || refreshingExternal || readingCsv) return;
    if (change.revision !== undefined && hostBridge?.isCurrentRevision?.(change.revision)) {
      pendingExternal = undefined;
      scheduleAutoSave();
      return;
    }
    if (discard && change.buffer === undefined && !backend.reload) return;
    if (!discard && hasLocalChanges()) {
      const changed = !backend.reload || await backend.externalChanged().catch(() => true);
      if (pendingExternal !== change) return;
      if (!changed) {
        pendingExternal = undefined;
        scheduleAutoSave();
        return;
      }
      if (hasLocalChanges()) {
        showExternalConflict();
        return;
      }
      scheduleExternal();
      return;
    }
    refreshingExternal = true;
    const wasInert = document.body.inert;
    document.body.inert = true;
    try {
      if (discard) {
        gridApi.stopEditing(true);
        await Promise.allSettled([...pendingCellEdits]);
        await csvMutations.settle().catch(() => {});
      }
      const buffer = change.buffer?.slice(0);
      const schema = backend.reload
        ? await backend.reload()
        : await request('open', { name: activeName, buffer }, [buffer]);
      if (schema) {
        const layout = gridApi.getColumnState();
        const row = gridApi.getFirstDisplayedRowIndex();
        const focused = gridApi.getFocusedCell();
        loadView(schema, activeName);
        gridApi.applyColumnState({ state: layout, applyOrder: true });
        if (row >= 0 && row < rowCount) gridApi.ensureIndexVisible(row, 'top');
        if (focused && focused.rowIndex < rowCount && activeColumns.includes(focused.column.getColId())) {
          gridApi.setFocusedCell(focused.rowIndex, focused.column.getColId());
        }
        if (change.revision !== undefined) hostBridge?.acceptRevision?.(change.revision);
        setStatus(`Loaded new changes ${new Date().toLocaleString()}`);
      }
      if (pendingExternal === change) pendingExternal = undefined;
      externalConflict = false;
      reloadExternalButton.hidden = true;
      recoveryButton.hidden = true;
      conflictNotice.hidden = true;
      statusNode.hidden = false;
    } catch (error) {
      externalConflict = true;
      reloadExternalButton.hidden = false;
      conflictNotice.textContent = error instanceof Error ? error.message : 'Could not reload the CSV.';
      conflictNotice.hidden = false;
      statusNode.hidden = true;
    } finally {
      document.body.inert = wasInert;
      refreshingExternal = false;
      scheduleExternal();
      scheduleAutoSave();
    }
  }

  recoveryButton.addEventListener('click', () => {
    void (async () => {
      await finishEditing();
      await csvMutations.settle().catch(() => {});
      const columnOrder = currentGridColumns(gridApi, activeColumns);
      const edits = JSON.stringify({ file: activeName, columnOrder, edits: recovery }, null, 2);
      requestRecovery(edits);
    })();
  });

  function recoveryResult({ success, path, error }) {
    if (!externalConflict) return;
    conflictNotice.textContent = success ? `Saved a copy of your edits to ${path}.` :
      (error || 'Could not save a copy of your edits.');
  }

  async function confirmDiscardEdits(title, description, actionLabel) {
    if (confirmingDiscard) return false;
    const generation = viewGeneration;
    confirmingDiscard = true;
    discardEditsTitle.textContent = title;
    discardEditsDescription.textContent = description;
    discardEditsConfirm.textContent = actionLabel;
    try {
      const result = await discardEditsDialog.showForResult(discardEditsCancel);
      return result === 'discard' && generation === viewGeneration;
    } finally {
      confirmingDiscard = false;
    }
  }

  async function requestExternalReload() {
    if (!pendingExternal || saving || refreshingExternal || confirmingDiscard) return;
    if (hasLocalChanges() && !await confirmDiscardEdits(
      'Discard edits and reload CSV?',
      'Reloading opens the latest file from disk and discards your unsaved edits.',
      'Discard edits and reload',
    )) return;
    await refreshExternal({ discard: true });
  }

  reloadExternalButton.addEventListener('click', () => { void requestExternalReload(); });
  discardEditsCancel.addEventListener('click', () => discardEditsDialog.close());
  discardEditsConfirm.addEventListener('click', () => discardEditsDialog.close('discard'));

  let saving = false;
  async function saveCurrentChanges({ automatic = false } = {}) {
    if (!editable) throw new Error('This CSV is read-only.');
    if (!viewOpen) {
      throw new Error('No CSV is open.');
    }
    if (automatic && (!autoSave.enabled || !dirty || externalConflict || pendingExternal ||
      saving || readingCsv || refreshingExternal || gridApi.getEditingCells().length)) return;
    if (externalConflict) throw new Error('The CSV changed on disk. Reload CSV before saving.');
    if (saving || refreshingExternal) throw new Error('A CSV update is already in progress.');
    if (readingCsv) throw new Error('The input dialect is being applied.');
    saving = true;
    autoSave.cancel();
    const wasInert = document.body.inert;
    if (!automatic) document.body.inert = true;
    let releaseWrites;
    try {
      if (!automatic) await finishEditing({ requireSuccess: true });
      else {
        const results = await Promise.all([...pendingCellEdits]);
        const failure = results.find(result => result instanceof Error);
        if (failure) throw failure;
      }
      await csvMutations.settle();
      if (!dirty || (automatic && (!autoSave.enabled || pendingExternal || gridApi.getEditingCells().length))) return;
      const savedEditCount = recovery.length;
      saveBarrier = new Promise(resolve => { releaseWrites = resolve; });
      if (!automatic) showLoading('Saving CSV', `${activeName} is being rewritten with your edits.`);
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
      recovery.splice(0, savedEditCount);
      setStatus(savedResult.chartError ? 'Saved. Chart generation failed: ' + savedResult.chartError : 'Saved ' + name + '.',
        savedResult.chartError ? 'Saved. Chart generation failed: ' + savedResult.chartError : 'Saved.');
    } catch (error) {
      autoSave.failed();
      if (error?.conflict) {
        if (backend.reload) pendingExternal ??= {};
        showExternalConflict();
      } else {
        setStatus(error instanceof Error ? error.message : 'Save failed');
      }
      throw error;
    } finally {
      saveBarrier = undefined;
      releaseWrites?.();
      if (!automatic) {
        hideLoading();
        document.body.inert = wasInert;
      }
      saving = false;
      scheduleExternal();
      scheduleAutoSave();
    }
  }

  saveButton.addEventListener('click', () => { void saveCurrentChanges().catch(() => {}); });

  async function resetCurrentChanges() {
    if (!viewOpen || saving || refreshingExternal) return;
    if (pendingExternal || externalConflict) { await requestExternalReload(); return; }
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
      recovery.length = 0;
      setStatus(`Reset ${activeName}.`, 'Reset.');
    } catch (error) {
      setStatus(error instanceof Error ? error.message : 'Reset failed');
    } finally {
      hideLoading();
    }
  }
  resetButton.addEventListener('click', () => { void resetCurrentChanges(); });


  async function finishEditing({ requireSuccess = false } = {}) {
    const pending = new Set(pendingCellEdits);
    if (requireSuccess) saveEditDrain = pending;
    try {
      gridApi.stopEditing();
      await new Promise(resolve => window.setTimeout(resolve, 0));
      for (const edit of pendingCellEdits) pending.add(edit);
      const results = await Promise.all(pending);
      if (requireSuccess) {
        const failure = results.find(result => result instanceof Error);
        if (failure) throw failure;
      }
    } finally {
      if (saveEditDrain === pending) saveEditDrain = null;
    }
  }
  decorateMenuButton(insertMenuButton, 'plus', 'Insert');
  decorateMenuButton(deleteMenuButton, 'trash', 'Delete');
  decorateButton(csvOptionsButton, 'settings', 'Options');
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
    setStatus, refreshRows, setAutoSave: autoSave.setPolicy, externalChange, recoveryResult,
    connectRecovery(callback) { requestRecovery = callback; },
    restoreTable() { resultMode = false; updateRecordCount(); setStatus(dirty ? 'Unsaved changes.' : 'Ready.'); },
    setResultMode(value) { resultMode = value; },
    addColumnAction(action) { columnFeatures.push(action); },
    connectHost(host) { hostBridge = host; setHostMode(host.isHostMode()); },
    setHostMode, onViewportResize: hostGrid.onViewportResize,
    async start() {
      try {
        const initial = await backend.request('init');
        engineReady = true;
        if (initial?.columns) loadView(initial, initial.file, { deferInitialCount: true });
        if (pendingOpenFile) { const file = pendingOpenFile; pendingOpenFile = null; await openFile(file); }
        fileInput.disabled = !backend.capabilities.localFiles || hostBridge?.isHostMode();
        for (const feature of features) {
          if (!feature.id || typeof feature.mount !== 'function') throw new Error('Invalid viewer feature');
          const mounted = feature.mount(shell);
          if (mounted?.destroy) featureDisposers.push(() => mounted.destroy());
        }
        scheduleExternal();
        if (!viewOpen) { document.getElementById('file-name').textContent = 'csvzall'; setStatus('Open a local CSV file to begin.'); }
      } catch (error) { setStatus(error.message); throw error; }
    },
    destroy() {
      autoSave.dispose();
      window.clearTimeout(externalTimer);
      viewGeneration += 1;
      pendingRowFocus = null;
      for (const dispose of featureDisposers.reverse()) dispose();
      for (const dialog of [discardEditsDialog, csvOptionsDialog, insertColumnDialog, renameColumnDialog, loadingDialog]) dialog.destroy();
      for (const menu of [...menus, rowMenu, columnMenu]) menu?.destroy?.();
      compactLayout.destroy(); removeBeforeUnload?.(); hostGrid.destroy(); gridApi.destroy(); backend.destroy?.();
    },
  };
  refreshActionState();
  setHostMode(false);
  return shell;
}

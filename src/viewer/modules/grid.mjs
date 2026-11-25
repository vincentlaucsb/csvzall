export function currentGridColumns(api, columns) {
  const knownColumns = new Set(columns);
  const gridColumns = typeof api.getAllDisplayedColumns === 'function'
    ? api.getAllDisplayedColumns().map((column) => column.getColId ? column.getColId() : (column.colId || ''))
    : [];
  const columnIds = gridColumns.length > 0 || typeof api.getColumnState !== 'function'
    ? gridColumns
    : api.getColumnState().map((column) => column.colId);
  return columnIds.filter((column) => knownColumns.has(column));
}

export const sourceRowId = Symbol('_csvzallRowId');
export const insertedRow = Symbol('csvzallInsertedRow');
export const editedColumns = Symbol('csvzallEditedColumns');

// AG Grid's built-in block debounce also delays the initial viewport. Schedule
// at the datasource instead so only the first request for a new file bypasses it.
export function createPagedDatasource(readRows, isBlockCached) {
  let firstRequest = true;
  let closed = false;
  let pending = [];
  let timer;

  function debounce() {
    if (!pending.length) return;
    clearTimeout(timer);
    timer = setTimeout(() => {
      const batch = pending;
      pending = [];
      for (const params of batch) {
        if (isBlockCached(params.startRow)) readRows(params);
        else params.failCallback();
      }
    }, 40);
  }

  return {
    getRows(params) {
      if (closed) return params.failCallback();
      const immediate = firstRequest && params.startRow === 0;
      firstRequest = false;
      if (immediate) return readRows(params);
      pending.push(params);
      debounce();
    },
    onVerticalScroll: debounce,
    destroy() {
      closed = true;
      clearTimeout(timer);
      const batch = pending;
      pending = [];
      for (const params of batch) params.failCallback();
    },
  };
}

export function rowsToObjects(columns, rows, offset = 0, rowIds, changes) {
  const changesById = changes?.length ? new Map(changes.map(change => [change.rowId, change])) : null;
  return rows.map((values, index) => {
    const row = Object.fromEntries(columns.map((column, columnIndex) => [column, values[columnIndex] ?? '']));
    row[sourceRowId] = rowIds ? rowIds[index] : offset + index;
    const change = changesById?.get(row[sourceRowId]);
    if (change?.inserted) row[insertedRow] = true;
    if (change?.columns.length) row[editedColumns] = new Set(change.columns);
    return row;
  });
}

export function rowInsertionIndex(anchorRow, rowCount, offset) {
  const count = Number.isInteger(rowCount) && rowCount > 0 ? rowCount : 0;
  if (!Number.isInteger(anchorRow)) {
    return count;
  }
  const clampedAnchor = Math.max(0, Math.min(anchorRow, count));
  return Math.max(0, Math.min(clampedAnchor + offset, count));
}

export function installColumnContextMenu({
  createContextMenu, gridElement, getColumns, queryColumn, renameColumn, deleteColumn, createIcon,
}) {
  if (typeof createContextMenu !== 'function') return;
  const items = [{ id: 'query-column', label: 'Query column', icon: () => createIcon('player-play') }];
  if (renameColumn) items.push({ id: 'rename-column', label: 'Rename Column', icon: () => createIcon('pencil') });
  if (deleteColumn) items.push({ type: 'separator' }, {
    id: 'delete-column', label: 'Delete Column', icon: () => createIcon('column-remove'), variant: 'danger',
  });
  const menu = createContextMenu({
    trigger: 'manual',
    theme: 'system',
    items,
    onSelect(event) {
      const column = event.context.data?.column;
      if (!getColumns().includes(column)) return;
      if (event.id === 'query-column') {
        // Popright restores the old focus on close; close before focusing SQL.
        event.close();
        queryColumn(column);
      } else if (event.id === 'rename-column') {
        renameColumn?.(column);
      } else if (event.id === 'delete-column') {
        void deleteColumn?.(column);
      }
    },
  });
  gridElement.addEventListener('contextmenu', (event) => {
    const headerCell = event.target?.closest?.('.ag-header-cell[col-id]');
    const column = headerCell?.getAttribute('col-id');
    if (!getColumns().includes(column)) return;
    event.preventDefault();
    menu.open({
      x: event.clientX, y: event.clientY, target: headerCell,
      triggerEvent: event, context: { column },
    });
  });
}

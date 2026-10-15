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

export function rowsToObjects(columns, rows, offset = 0) {
  return rows.map((values, index) => {
    const row = { _csvzallRowId: offset + index };
    columns.forEach((column, columnIndex) => {
      row[column] = values[columnIndex] ?? '';
    });
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

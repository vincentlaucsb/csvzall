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

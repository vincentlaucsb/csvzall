// Android's keyboard settles over several viewport updates. Keep the grid and
// editor mounted; scrolling the active cell is enough and preserves focus.
export function createHostGridAdapter({ windowRef = window, getGrid, isHostMode }) {
  let activeEditCell = null;
  let editGeneration = 0;
  const timers = new Set();

  function later(callback, delay) {
    const timer = windowRef.setTimeout(() => {
      timers.delete(timer);
      callback();
    }, delay);
    timers.add(timer);
  }

  function refresh() {
    if (!isHostMode()) return;
    // Never synthesize window resize: menu libraries interpret it as a reason
    // to dismiss open menus. AG Grid already observes real viewport changes.
    const grid = getGrid();
    if (!grid || grid.isDestroyed?.()) return;
    const cell = activeEditCell || grid.getFocusedCell?.();
    if (!cell) return;
    if (Number.isInteger(cell.rowIndex)) grid.ensureIndexVisible?.(cell.rowIndex, 'middle');
    if (cell.column) grid.ensureColumnVisible?.(cell.column);
  }

  return {
    onViewportResize() { later(refresh, 60); },
    onCellEditingStarted(event) {
      const generation = ++editGeneration;
      activeEditCell = Number.isInteger(event?.rowIndex)
        ? { rowIndex: event.rowIndex, column: event.column } : null;
      if (!isHostMode()) return;
      for (const delay of [40, 140, 320, 650]) {
        later(() => { if (generation === editGeneration) refresh(); }, delay);
      }
    },
    onCellEditingStopped() {
      const generation = ++editGeneration;
      later(() => {
        if (generation !== editGeneration) return;
        activeEditCell = null;
        // Focus may now belong to a menu. Do not scroll the previous grid cell.
      }, 180);
    },
    destroy() {
      editGeneration += 1;
      for (const timer of timers) windowRef.clearTimeout(timer);
      timers.clear();
      activeEditCell = null;
    },
  };
}

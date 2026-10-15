import { rowsToMarkdownTable } from './markdown.mjs';

export const VIEWER_SQL_TABLE_NAME = 'data';

export function defaultSqlQuery(tableName = VIEWER_SQL_TABLE_NAME) {
  return `SELECT *\nFROM ${tableName}\nLIMIT 100;`;
}

export function columnSqlQuery(column, tableName = VIEWER_SQL_TABLE_NAME) {
  return 'SELECT `' + column.replaceAll('`', '``') + '`\nFROM ' + tableName + '\nLIMIT 100;';
}

export async function waitForViewerEdits(api, pendingEdits) {
  api.stopEditing?.();
  // AG Grid delivers cell-value callbacks asynchronously after stopEditing.
  // Let them register their writes before waiting for the edit overlay.
  await new Promise((resolve) => setTimeout(resolve, 0));
  await Promise.all([...pendingEdits]);
}

// Keep the paged editor alive while SQL uses its own client-side result grid.
export function installSqlViewer({
  elements, tableName = VIEWER_SQL_TABLE_NAME, editable, createGrid,
  postJson, beforeQuery, restoreTable, writeClipboard, finishEditing = () => {},
  windowRef = globalThis.window,
}) {
  const {
    panel, openButton, exitButton, input, clearButton, error, documentationLink,
    runButton, copyButton, tableGrid, resultGrid, editToolbar, status, recordCount,
  } = elements;
  let active = false;
  let generation = 0;
  let resultApi;
  let result;
  let running = false;
  const suggestedQuery = defaultSqlQuery(tableName);
  input.value = suggestedQuery;
  input.placeholder = suggestedQuery;
  input.setAttribute('aria-label', `SQLite query over table ${tableName}`);
  documentationLink?.addEventListener('click', (event) => {
    if (windowRef?.parent && windowRef.parent !== windowRef) {
      // Obsidian's sandbox keeps popups disabled; the host opens this fixed page.
      event.preventDefault();
      windowRef.parent.postMessage({
        source: 'csvzall-viewer', type: 'open-documentation', version: 1, page: 'sqlite-capabilities',
      }, '*');
    }
  });

  function clearError() {
    error.textContent = '';
    error.hidden = true;
  }

  function resizeInput() {
    input.style.height = 'auto';
    input.style.height = `${Math.min(input.scrollHeight, 144)}px`;
  }

  async function run() {
    if (!active || running) return;
    const current = ++generation;
    running = true;
    runButton.disabled = true;
    copyButton.disabled = true;
    clearError();
    status.textContent = 'Running SQLite query…';
    recordCount.textContent = result
      ? `${result.rows.length.toLocaleString()} rows in previous SQLite result`
      : '';
    try {
      await beforeQuery();
      // Returning to the table while an edit settles must not launch a query.
      if (current !== generation) return;
      const next = await postJson('/api/sql-query', { sql: input.value });
      if (current !== generation) return;
      const options = {
        columnDefs: next.columns.map((column, index) => ({
          headerName: column,
          colId: String(index),
          valueGetter: (params) => params.data?.[index],
          sortable: true,
          filter: true,
          editable: false,
          resizable: true,
          minWidth: 140,
          flex: 1,
        })),
        rowData: next.rows,
        animateRows: false,
        overlayNoRowsTemplate: '<span>No SQLite results</span>',
      };
      resultGrid.hidden = false;
      if (!resultApi) {
        resultApi = createGrid(resultGrid, options);
      } else {
        resultApi.setGridOption('columnDefs', options.columnDefs);
        resultApi.setGridOption('rowData', options.rowData);
      }
      result = next;
      recordCount.textContent = `${next.rows.length.toLocaleString()} SQLite result row${next.rows.length === 1 ? '' : 's'}, ${next.columns.length.toLocaleString()} column${next.columns.length === 1 ? '' : 's'}`;
      status.textContent = 'SQLite results are read-only. Exit query to edit or save the CSV.';
    } catch (cause) {
      if (current !== generation) return;
      error.textContent = cause instanceof Error ? cause.message : 'SQLite query failed';
      error.hidden = false;
      status.textContent = 'SQLite query failed.';
    } finally {
      if (current === generation) {
        running = false;
        runButton.disabled = false;
        copyButton.disabled = !result?.columns.length;
      }
    }
  }

  function setMode(sql) {
    active = sql;
    if (sql) finishEditing();
    ++generation;
    running = false;
    panel.hidden = !sql;
    openButton.classList.toggle('active', sql);
    openButton.setAttribute('aria-expanded', String(sql));
    tableGrid.hidden = sql;
    resultGrid.hidden = true;
    editToolbar.hidden = !editable;
    editToolbar.inert = sql;
    editToolbar.setAttribute('aria-hidden', String(sql));
    runButton.disabled = false;
    clearError();
    if (sql) {
      resizeInput();
      result = undefined;
      copyButton.disabled = true;
      recordCount.textContent = '';
      status.textContent = 'Query ready. Press Run query or Ctrl/Cmd+Enter.';
      input.focus();
    } else {
      restoreTable();
    }
  }

  exitButton.addEventListener('click', () => {
    if (active) {
      setMode(false);
      openButton.focus();
    }
  });
  openButton.addEventListener('click', () => {
    if (!active) setMode(true);
    else input.focus();
  });
  runButton.addEventListener('click', () => { void run(); });
  input.addEventListener('input', () => { clearError(); resizeInput(); });
  input.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowRight' && input.value === '' &&
        !event.ctrlKey && !event.metaKey && !event.altKey && !event.shiftKey && !event.isComposing) {
      event.preventDefault();
      input.value = suggestedQuery;
      input.setSelectionRange(input.value.length, input.value.length);
      clearError();
      resizeInput();
      return;
    }
    if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      void run();
    }
  });
  clearButton.addEventListener('click', () => {
    input.value = '';
    clearError();
    resizeInput();
    input.focus();
  });
  copyButton.addEventListener('click', async () => {
    if (!result || !active || running) return;
    const current = generation;
    try {
      await writeClipboard(rowsToMarkdownTable(result.columns, result.rows));
      if (current === generation) status.textContent = 'Copied Markdown table.';
    } catch (cause) {
      if (current === generation) status.textContent = cause instanceof Error ? cause.message : 'Copy failed';
    }
  });
  setMode(false);
  return {
    get active() { return active; },
    queryColumn(column) {
      input.value = columnSqlQuery(column, tableName);
      setMode(true);
    },
  };
}

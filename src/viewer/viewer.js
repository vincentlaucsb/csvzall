function createFallbackContextMenu(options) {
  let root = null;
  let currentInput = {};

  const close = () => {
    if (!root) {
      return;
    }
    root.remove();
    root = null;
    document.removeEventListener('pointerdown', handlePointerDown, true);
    document.removeEventListener('keydown', handleKeyDown, true);
  };

  const handlePointerDown = (event) => {
    if (root && !root.contains(event.target)) {
      close();
    }
  };

  const handleKeyDown = (event) => {
    if (event.key === 'Escape') {
      close();
    }
  };

  const open = (input = {}) => {
    close();
    currentInput = input;
    const items = typeof options.items === 'function'
      ? options.items({ data: input.context, target: input.target, triggerEvent: input.triggerEvent })
      : options.items;
    const visibleItems = (items || []).filter((item) => !item.hidden);
    if (visibleItems.length === 0) {
      return;
    }

    root = document.createElement('div');
    root.setAttribute('data-popright-menu', '');
    root.setAttribute('role', 'menu');
    root.tabIndex = -1;
    root.style.position = 'fixed';
    root.style.left = `${input.x ?? 0}px`;
    root.style.top = `${input.y ?? 0}px`;

    visibleItems.forEach((item) => {
      if (item.type === 'separator') {
        const separator = document.createElement('div');
        separator.setAttribute('data-popright-separator', '');
        root.append(separator);
        return;
      }

      const button = document.createElement('button');
      button.type = 'button';
      button.setAttribute('data-popright-item', '');
      button.setAttribute('role', 'menuitem');
      button.textContent = item.label ?? item.id ?? '';
      if (item.variant) {
        button.dataset.variant = item.variant;
      }
      if (item.disabled) {
        button.disabled = true;
        button.setAttribute('data-disabled', '');
      }
      button.addEventListener('pointerenter', () => {
        root.querySelectorAll('[data-active]').forEach((active) => active.removeAttribute('data-active'));
        button.setAttribute('data-active', '');
      });
      button.addEventListener('click', (event) => {
        const selectEvent = {
          id: item.id,
          item,
          nativeEvent: event,
          context: {
            data: currentInput.context,
            target: currentInput.target,
            triggerEvent: currentInput.triggerEvent,
            x: currentInput.x,
            y: currentInput.y,
          },
          close,
          preventClose() {},
        };
        item.onSelect?.(selectEvent);
        options.onSelect?.(selectEvent);
        close();
      });
      root.append(button);
    });

    document.body.append(root);
    const rect = root.getBoundingClientRect();
    const left = Math.min(input.x ?? 0, Math.max(0, window.innerWidth - rect.width - 4));
    const top = Math.min(input.y ?? 0, Math.max(0, window.innerHeight - rect.height - 4));
    root.style.left = `${left}px`;
    root.style.top = `${top}px`;
    root.focus({ preventScroll: true });
    document.addEventListener('pointerdown', handlePointerDown, true);
    document.addEventListener('keydown', handleKeyDown, true);
  };

  return {
    open,
    close,
    update(nextOptions) {
      Object.assign(options, nextOptions);
    },
    destroy: close,
    get isOpen() {
      return root !== null;
    },
    get root() {
      return root;
    },
  };
}

async function csvzallViewBootstrap(dependencies = {}) {
  const { createContextMenu } = dependencies;
  const statusNode = document.getElementById('status');
  const summaryNode = document.getElementById('summary');
  const fileNode = document.getElementById('file-name');
  const modeNode = document.getElementById('mode-label');
  const quickFilterNode = document.getElementById('quick-filter');
  const token = new URLSearchParams(window.location.search).get('token');

  if (!token) {
    statusNode.textContent = 'Missing session token.';
    summaryNode.textContent = 'Open the viewer from the csvzall command output.';
    return;
  }

  const headers = { 'X-Session-Token': token };

  async function fetchJson(path, params = {}) {
    const url = new URL(path, window.location.origin);
    for (const [key, value] of Object.entries(params)) {
      url.searchParams.set(key, String(value));
    }
    const response = await fetch(url, { headers });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
  }

  async function postJson(path, body = {}) {
    const response = await fetch(new URL(path, window.location.origin), {
      method: 'POST',
      headers: { ...headers, 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const text = await response.text();
    if (!response.ok) {
      throw new Error(text.trim() || `HTTP ${response.status}`);
    }
    return text ? JSON.parse(text) : {};
  }

  function setDatasource(api, datasource) {
    if (api.setGridOption) {
      api.setGridOption('datasource', datasource);
      return;
    }
    api.setDatasource(datasource);
  }

  function setQuickFilter(api, value) {
    if (api.setGridOption) {
      api.setGridOption('quickFilterText', value);
      return;
    }
    api.setQuickFilter(value);
  }

  function rowsToObjects(columns, rows) {
    return rows.map((values, index) => {
      const row = { _csvzallRowId: index };
      columns.forEach((column, index) => {
        row[column] = values[index] ?? '';
      });
      return row;
    });
  }

  function renumberRows(rows) {
    rows.forEach((row, index) => {
      row._csvzallRowId = index;
    });
  }

  try {
    const schema = await fetchJson('/api/schema');
    fileNode.textContent = schema.file;
    summaryNode.textContent = `${schema.totalRows.toLocaleString()} rows across ${schema.columns.length.toLocaleString()} columns.`;
    const materialized = schema.mode === 'materialized';
    const editable = schema.editable === true;
    const editToolbar = document.getElementById('edit-toolbar');
    const insertButton = document.getElementById('insert-row');
    const deleteButton = document.getElementById('delete-row');
    const insertColumnButton = document.getElementById('insert-column');
    const deleteColumnButton = document.getElementById('delete-column');
    const resetButton = document.getElementById('reset');
    const saveButton = document.getElementById('save');
    const insertColumnDialog = document.getElementById('insert-column-dialog');
    const insertColumnForm = document.getElementById('insert-column-form');
    const insertColumnName = document.getElementById('insert-column-name');
    const insertColumnError = document.getElementById('insert-column-error');
    const cancelInsertColumn = document.getElementById('cancel-insert-column');
    modeNode.textContent = materialized ? 'Client-side sort/filter' : 'Server-paged rows';
    quickFilterNode.hidden = !materialized;
    quickFilterNode.disabled = !materialized;
    editToolbar.hidden = !editable;

    const makeColumnDefs = () => schema.columns.map((column) => ({
      headerName: column,
      field: column,
      sortable: materialized,
      filter: materialized,
      editable,
      resizable: true,
      minWidth: 140,
      flex: 1,
    }));

    const gridOptions = {
      columnDefs: makeColumnDefs(),
      defaultColDef: {
        sortable: materialized,
        filter: materialized,
        editable,
        resizable: true,
      },
      animateRows: false,
      suppressColumnVirtualisation: false,
      rowSelection: 'single',
    };

    if (!materialized) {
      gridOptions.rowModelType = 'infinite';
      gridOptions.cacheBlockSize = 500;
      gridOptions.maxBlocksInCache = 8;
      gridOptions.blockLoadDebounceMillis = 40;
    }

    const gridElement = document.getElementById('grid');
    const api = window.agGrid.createGrid
      ? window.agGrid.createGrid(gridElement, gridOptions)
      : (() => {
          new window.agGrid.Grid(gridElement, gridOptions);
          return gridOptions.api;
        })();

    if (materialized) {
      statusNode.textContent = `Loading ${schema.totalRows.toLocaleString()} rows for client-side sort/filter…`;
      let dirty = false;
      const allRows = schema.totalRows === 0
        ? []
        : rowsToObjects(schema.columns, (await fetchJson('/api/rows', { offset: 0, limit: schema.totalRows })).rows);
      const setDirty = (value) => {
        dirty = value;
        saveButton.disabled = !dirty;
        resetButton.disabled = !dirty;
        statusNode.textContent = dirty
          ? 'Unsaved changes.'
          : `Loaded ${allRows.length.toLocaleString()} rows for ${editable ? 'editing' : 'client-side sort/filter'}.`;
      };
      if (api.setGridOption) {
        api.setGridOption('rowData', allRows);
      } else {
        api.setRowData(allRows);
      }
      quickFilterNode.addEventListener('input', () => setQuickFilter(api, quickFilterNode.value));
      if (editable) {
        const refreshRows = () => {
          if (api.setGridOption) {
            api.setGridOption('rowData', allRows);
          } else {
            api.setRowData(allRows);
          }
        };
        const refreshColumns = () => {
          if (api.setGridOption) {
            api.setGridOption('columnDefs', makeColumnDefs());
          } else {
            api.setColumnDefs(makeColumnDefs());
          }
        };
        const selectedSourceRow = () => {
          const selected = api.getSelectedRows ? api.getSelectedRows() : [];
          return selected.length > 0 ? selected[0]._csvzallRowId : allRows.length;
        };
        const focusedColumnName = () => {
          const focused = api.getFocusedCell ? api.getFocusedCell() : null;
          const column = focused && focused.column;
          if (!column) {
            return '';
          }
          return column.getColId ? column.getColId() : (column.colId || '');
        };
        let pendingInsertColumn = schema.columns.length;
        const showColumnError = (message) => {
          insertColumnError.textContent = message;
          insertColumnError.hidden = false;
        };
        const clearColumnError = () => {
          insertColumnError.textContent = '';
          insertColumnError.hidden = true;
        };
        const insertColumnAt = async (column, name) => {
          await postJson('/api/insert-column', { column, name, value: '' });
          schema.columns.splice(column, 0, name);
          allRows.forEach((row) => {
            row[name] = '';
          });
          refreshColumns();
          refreshRows();
          setDirty(true);
        };
        const deleteRowAt = async (row) => {
          if (row >= allRows.length) {
            statusNode.textContent = 'Select a row to delete.';
            return;
          }
          try {
            await postJson('/api/delete-row', { row });
            allRows.splice(row, 1);
            renumberRows(allRows);
            refreshRows();
            setDirty(true);
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Delete failed';
          }
        };
        gridOptions.onCellValueChanged = async (event) => {
          if (!event.colDef.field || event.colDef.field === '_csvzallRowId') {
            return;
          }
          try {
            await postJson('/api/edit-cell', {
              row: event.data._csvzallRowId,
              column: event.colDef.field,
              value: event.newValue ?? '',
            });
            setDirty(true);
          } catch (error) {
            event.node.setDataValue(event.colDef.field, event.oldValue ?? '');
            statusNode.textContent = error instanceof Error ? error.message : 'Edit failed';
          }
        };
        if (api.setGridOption) {
          api.setGridOption('onCellValueChanged', gridOptions.onCellValueChanged);
        }
        if (typeof createContextMenu === 'function') {
          const rowMenu = createContextMenu({
            trigger: 'manual',
            theme: 'system',
            items: [
              { id: 'delete-row', label: 'Delete Row', variant: 'danger' },
            ],
            onSelect(event) {
              const row = event.context.data?.row;
              if (event.id === 'delete-row' && Number.isInteger(row)) {
                void deleteRowAt(row);
              }
            },
          });
          gridOptions.onCellContextMenu = (event) => {
            const nativeEvent = event.event;
            const row = event.data?._csvzallRowId;
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
              context: { row },
            });
          };
          if (api.setGridOption) {
            api.setGridOption('onCellContextMenu', gridOptions.onCellContextMenu);
          }
        }
        deleteButton.addEventListener('click', async () => {
          await deleteRowAt(selectedSourceRow());
        });
        insertButton.addEventListener('click', async () => {
          const row = selectedSourceRow();
          const values = schema.columns.map(() => '');
          try {
            await postJson('/api/insert-row', { row, values });
            const inserted = { _csvzallRowId: row };
            schema.columns.forEach((column) => {
              inserted[column] = '';
            });
            allRows.splice(row, 0, inserted);
            renumberRows(allRows);
            refreshRows();
            setDirty(true);
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Insert failed';
          }
        });
        insertColumnButton.addEventListener('click', async () => {
          const focused = focusedColumnName();
          const focusedIndex = schema.columns.indexOf(focused);
          pendingInsertColumn = focusedIndex >= 0 ? focusedIndex : schema.columns.length;
          insertColumnName.value = '';
          clearColumnError();
          insertColumnDialog.showModal();
          insertColumnName.focus();
        });
        insertColumnForm.addEventListener('submit', async (event) => {
          event.preventDefault();
          const name = insertColumnName.value.trim();
          if (!name) {
            showColumnError('Enter a column name.');
            insertColumnName.focus();
            return;
          }
          if (schema.columns.includes(name)) {
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
        deleteColumnButton.addEventListener('click', async () => {
          const column = focusedColumnName();
          if (!column || !schema.columns.includes(column)) {
            statusNode.textContent = 'Focus a column to delete.';
            return;
          }
          try {
            await postJson('/api/delete-column', { column });
            schema.columns.splice(schema.columns.indexOf(column), 1);
            allRows.forEach((row) => {
              delete row[column];
            });
            refreshColumns();
            refreshRows();
            setDirty(true);
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Column delete failed';
          }
        });
        resetButton.addEventListener('click', async () => {
          try {
            resetButton.disabled = true;
            saveButton.disabled = true;
            statusNode.textContent = 'Resetting…';
            await postJson('/api/reset');
            window.location.reload();
          } catch (error) {
            resetButton.disabled = !dirty;
            saveButton.disabled = !dirty;
            statusNode.textContent = error instanceof Error ? error.message : 'Reset failed';
          }
        });
        saveButton.addEventListener('click', async () => {
          try {
            saveButton.disabled = true;
            statusNode.textContent = 'Saving…';
            await postJson('/api/save');
            setDirty(false);
            statusNode.textContent = `Saved ${allRows.length.toLocaleString()} rows.`;
          } catch (error) {
            saveButton.disabled = !dirty;
            statusNode.textContent = error instanceof Error ? error.message : 'Save failed';
          }
        });
      }
      setDirty(false);
    } else {
      setDatasource(api, {
        async getRows(params) {
          const offset = params.startRow;
          const limit = Math.max(params.endRow - params.startRow, 1);
          statusNode.textContent = `Loading rows ${offset + 1}-${Math.min(params.endRow, schema.totalRows)} of ${schema.totalRows}…`;
          try {
            const page = await fetchJson('/api/rows', { offset, limit });
            const rowData = rowsToObjects(schema.columns, page.rows);
            const loadedThrough = page.offset + page.rows.length;
            const lastRow = loadedThrough >= page.totalRows ? page.totalRows : undefined;
            params.successCallback(rowData, lastRow);
            statusNode.textContent = `Loaded rows ${page.offset + 1}-${loadedThrough} of ${page.totalRows}.`;
          } catch (error) {
            params.failCallback();
            statusNode.textContent = error instanceof Error ? error.message : 'Row load failed';
          }
        },
      });
      statusNode.textContent = `Ready: ${schema.totalRows.toLocaleString()} rows indexed.`;
    }

    if (api.sizeColumnsToFit) {
      api.sizeColumnsToFit();
    }
  } catch (error) {
    summaryNode.textContent = 'The viewer could not load table data.';
    statusNode.textContent = error instanceof Error ? error.message : 'Unknown error';
  }
}

async function loadContextMenuFactory() {
  try {
    const response = await fetch('/assets/popright/index.js', {
      cache: 'force-cache',
      credentials: 'same-origin',
    });
    if (!response.ok) {
      return createFallbackContextMenu;
    }
    const popright = await import('/assets/popright/index.js');
    return popright.createContextMenu;
  } catch (error) {
    return createFallbackContextMenu;
  }
}

loadContextMenuFactory().then((createContextMenu) => {
  void csvzallViewBootstrap({ createContextMenu });
});

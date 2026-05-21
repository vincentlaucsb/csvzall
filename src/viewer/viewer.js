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
  const recordCountNode = document.getElementById('record-count');
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

  function slugify(value) {
    const slug = value.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
    return slug || 'chart';
  }

  function stripCsvExtension(value) {
    return value.replace(/\.[^.]+$/, '');
  }

  function populateColumnSelect(select, columns, { optional = false, preferred = '' } = {}) {
    select.textContent = '';
    if (optional) {
      const empty = document.createElement('option');
      empty.value = '';
      empty.textContent = 'None';
      select.append(empty);
    }
    columns.forEach((column) => {
      const option = document.createElement('option');
      option.value = column;
      option.textContent = column;
      select.append(option);
    });
    if (preferred && columns.includes(preferred)) {
      select.value = preferred;
    } else if (!optional && columns.length > 0) {
      select.value = columns[0];
    }
  }

  function guessColumn(columns, patterns) {
    const lowered = columns.map((column) => column.toLowerCase());
    for (const pattern of patterns) {
      const index = lowered.findIndex((column) => column === pattern || column.includes(pattern));
      if (index >= 0) {
        return columns[index];
      }
    }
    return '';
  }

  try {
    const schema = await fetchJson('/api/schema');
    fileNode.textContent = schema.file;
    summaryNode.textContent = '';
    recordCountNode.textContent = `${schema.totalRows.toLocaleString()} rows, ${schema.columns.length.toLocaleString()} columns`;
    const materialized = schema.mode === 'materialized';
    const editable = schema.editable === true;
    const editToolbar = document.getElementById('edit-toolbar');
    const addChartButton = document.getElementById('add-chart');
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
    const chartDialog = document.getElementById('heatmap-chart-dialog');
    const chartForm = document.getElementById('heatmap-chart-form');
    const chartList = document.getElementById('chart-list');
    const newChartButton = document.getElementById('new-chart');
    const generateChartButton = document.getElementById('generate-chart');
    const chartFormTitle = document.getElementById('chart-form-title');
    const chartType = document.getElementById('chart-type');
    const chartId = document.getElementById('chart-id');
    const chartTitle = document.getElementById('chart-title');
    const chartPrimaryColumnLabel = document.getElementById('chart-primary-column-label');
    const chartValueColumnLabel = document.getElementById('chart-value-column-label');
    const chartLabelColumnLabel = document.getElementById('chart-label-column-label');
    const chartDateColumn = document.getElementById('chart-date-column');
    const chartValueColumn = document.getElementById('chart-value-column');
    const chartLabelColumn = document.getElementById('chart-label-column');
    const chartRangeSection = document.getElementById('chart-range-section');
    const chartRangeFixed = document.getElementById('chart-range-fixed');
    const chartRangeRolling = document.getElementById('chart-range-rolling');
    const chartFixedRange = document.getElementById('chart-fixed-range');
    const chartRollingRange = document.getElementById('chart-rolling-range');
    const chartStart = document.getElementById('chart-start');
    const chartEnd = document.getElementById('chart-end');
    const chartLookbackCount = document.getElementById('chart-lookback-count');
    const chartLookbackUnit = document.getElementById('chart-lookback-unit');
    const chartOutput = document.getElementById('chart-output');
    const chartRunOnSave = document.getElementById('chart-run-on-save');
    const chartError = document.getElementById('chart-error');
    const cancelChart = document.getElementById('cancel-chart');
    modeNode.textContent = materialized ? 'Client-side' : 'Paged';
    quickFilterNode.hidden = !materialized;
    quickFilterNode.disabled = !materialized;
    editToolbar.hidden = !editable;
    addChartButton.hidden = false;

    const baseName = stripCsvExtension(schema.file);
    const baseSlug = slugify(baseName);
    const guessedDateColumn = guessColumn(schema.columns, ['date', 'day', 'attendance_date']);
    const guessedValueColumn = guessColumn(schema.columns, ['count', 'value', 'total']);
    const guessedLabelColumn = guessColumn(schema.columns, ['content', 'label', 'note', 'description']);
    let currentCharts = [];
    let selectedChartId = '';
    populateColumnSelect(chartDateColumn, schema.columns, { preferred: guessedDateColumn });
    populateColumnSelect(chartValueColumn, schema.columns, { optional: true, preferred: guessedValueColumn });
    populateColumnSelect(chartLabelColumn, schema.columns, { optional: true, preferred: guessedLabelColumn });

    const showChartError = (message) => {
      chartError.textContent = message;
      chartError.hidden = false;
    };
    const clearChartError = () => {
      chartError.textContent = '';
      chartError.hidden = true;
    };

    const chartTypeTitle = (type) => {
      if (type === 'bar') return 'Bar';
      if (type === 'line') return 'Line';
      return 'Heatmap';
    };

    const defaultChartValues = (type = chartType.value || 'heatmap') => ({
      type,
      id: `${baseSlug}-${type}`,
      title: baseName,
      date: guessedDateColumn,
      value: guessedValueColumn,
      label: guessedLabelColumn,
      start: '',
      end: '',
      lookback: '1y',
      output: `charts/${baseSlug}_${type}.svg`,
      runOnSave: true,
    });

    const parseLookback = (lookback) => {
      const match = String(lookback || '').trim().match(/^(\d+)\s*(d|day|days|y|year|years)?$/i);
      if (!match) {
        return { count: '1', unit: 'y' };
      }
      const unit = (match[2] || 'd').toLowerCase().startsWith('y') ? 'y' : 'd';
      return { count: match[1], unit };
    };

    const currentLookback = () => `${Math.max(1, Number.parseInt(chartLookbackCount.value, 10) || 1)}${chartLookbackUnit.value}`;

    const setRangeMode = (mode) => {
      const rolling = mode === 'rolling';
      chartRangeFixed.checked = !rolling;
      chartRangeRolling.checked = rolling;
      chartFixedRange.hidden = rolling;
      chartRollingRange.hidden = !rolling;
      chartStart.required = !rolling;
      chartEnd.required = !rolling;
      chartLookbackCount.required = rolling;
    };

    const setChartType = (type) => {
      chartType.value = type;
      chartFormTitle.textContent = chartTypeTitle(type);
      chartRangeSection.hidden = type !== 'heatmap';
      chartLabelColumn.closest('label').hidden = type === 'bar';
      chartValueColumnLabel.textContent = type === 'heatmap' ? 'Weight column' : 'Value column';
      chartValueColumn.querySelector('option[value=""]')?.toggleAttribute('disabled', type !== 'heatmap');
      if (type === 'heatmap') {
        chartPrimaryColumnLabel.textContent = 'Date column';
        chartLabelColumnLabel.textContent = 'Label column';
        chartValueColumn.required = false;
      } else if (type === 'bar') {
        chartPrimaryColumnLabel.textContent = 'Label column';
        chartLabelColumnLabel.textContent = 'Unused';
        chartValueColumn.required = true;
        if (!chartValueColumn.value) chartValueColumn.value = guessedValueColumn || schema.columns[0] || '';
      } else {
        chartPrimaryColumnLabel.textContent = 'X column';
        chartLabelColumnLabel.textContent = 'Series column';
        chartValueColumn.required = true;
        if (!chartValueColumn.value) chartValueColumn.value = guessedValueColumn || schema.columns[0] || '';
      }
    };

    const applyChartValues = (values) => {
      setChartType(values.type ?? 'heatmap');
      chartId.value = values.id ?? '';
      chartTitle.value = values.title ?? '';
      chartDateColumn.value = values.date ?? '';
      chartValueColumn.value = values.value ?? '';
      chartLabelColumn.value = values.label ?? '';
      chartStart.value = values.start ?? '';
      chartEnd.value = values.end ?? '';
      const lookback = values.lookback ?? '';
      const parsedLookback = parseLookback(lookback);
      chartLookbackCount.value = parsedLookback.count;
      chartLookbackUnit.value = parsedLookback.unit;
      setRangeMode(lookback ? 'rolling' : 'fixed');
      chartOutput.value = values.output ?? '';
      chartRunOnSave.checked = values.runOnSave !== false;
      selectedChartId = chartId.value;
      generateChartButton.disabled = !currentCharts.some((chart) => chart.id === selectedChartId);
    };

    const valuesFromChart = (chart) => {
      const type = chart.type || 'heatmap';
      return {
        type,
        id: chart.id,
        title: chart.options?.title ?? '',
        date: chart.options?.date ?? chart.options?.label ?? chart.options?.x ?? '',
        value: chart.options?.value ?? chart.options?.y ?? '',
        label: chart.options?.label ?? chart.options?.series ?? '',
        start: chart.options?.start ?? '',
        end: chart.options?.end ?? '',
        lookback: chart.options?.lookback ?? '',
        output: chart.output ?? '',
        runOnSave: chart.runOnSave === true,
      };
    };

    const renderChartList = () => {
      chartList.replaceChildren();
      if (currentCharts.length === 0) {
        const empty = document.createElement('p');
        empty.className = 'chart-list-empty';
        empty.textContent = 'No charts yet. Choose a chart type and save one.';
        chartList.append(empty);
        return;
      }
      currentCharts.forEach((chart) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'chart-list-item';
        button.setAttribute('role', 'option');
        button.setAttribute('aria-selected', String(chart.id === selectedChartId));
        const title = document.createElement('strong');
        title.textContent = chart.options?.title || chart.id;
        const detail = document.createElement('span');
        detail.textContent = `${chart.type || 'chart'} · ${chart.output || 'No output path'}`;
        button.append(title, detail);
        button.addEventListener('click', () => {
          selectedChartId = chart.id;
          applyChartValues(valuesFromChart(chart));
          clearChartError();
          renderChartList();
        });
        chartList.append(button);
      });
    };

    const loadChartList = async () => {
      const result = await fetchJson('/api/chart-config');
      currentCharts = Array.isArray(result.charts) ? result.charts : [];
      if (!currentCharts.some((chart) => chart.id === selectedChartId)) {
        selectedChartId = currentCharts[0]?.id ?? '';
      }
      if (selectedChartId) {
        const selected = currentCharts.find((chart) => chart.id === selectedChartId);
        if (selected) {
          applyChartValues(valuesFromChart(selected));
        }
      } else {
        applyChartValues(defaultChartValues('heatmap'));
      }
      renderChartList();
    };

    const useNewChartDefaults = () => {
      applyChartValues(defaultChartValues(chartType.value || 'heatmap'));
      selectedChartId = '';
      generateChartButton.disabled = true;
      clearChartError();
      renderChartList();
      chartId.focus();
    };

    addChartButton.addEventListener('click', async () => {
      clearChartError();
      try {
        await loadChartList();
      } catch (error) {
        currentCharts = [];
        applyChartValues(defaultChartValues());
        renderChartList();
        showChartError(error instanceof Error ? error.message : 'Chart config load failed');
      }
      chartDialog.showModal();
      chartDateColumn.focus();
    });
    newChartButton.addEventListener('click', useNewChartDefaults);
    chartType.addEventListener('change', () => {
      applyChartValues(defaultChartValues(chartType.value));
      selectedChartId = '';
      generateChartButton.disabled = true;
      clearChartError();
      renderChartList();
    });
    chartRangeFixed.addEventListener('change', () => setRangeMode('fixed'));
    chartRangeRolling.addEventListener('change', () => setRangeMode('rolling'));
    cancelChart.addEventListener('click', () => {
      chartDialog.close('cancel');
    });
    generateChartButton.addEventListener('click', async () => {
      const id = chartId.value.trim();
      if (!id) {
        showChartError('Select or save a chart first.');
        return;
      }
      try {
        const result = await postJson('/api/chart-config/generate', { id });
        statusNode.textContent = `Generated chart ${result.id}.`;
        clearChartError();
      } catch (error) {
        showChartError(error instanceof Error ? error.message : 'Chart generation failed');
      }
    });
    chartForm.addEventListener('submit', async (event) => {
      event.preventDefault();
      const payload = {
        type: chartType.value,
        id: chartId.value.trim(),
        title: chartTitle.value.trim(),
        date: chartType.value === 'heatmap' ? chartDateColumn.value : '',
        value: chartType.value !== 'line' ? chartValueColumn.value : '',
        label: chartType.value === 'heatmap'
          ? chartLabelColumn.value
          : (chartType.value === 'bar' ? chartDateColumn.value : ''),
        x: chartType.value === 'line' ? chartDateColumn.value : '',
        y: chartType.value === 'line' ? chartValueColumn.value : '',
        series: chartType.value === 'line' ? chartLabelColumn.value : '',
        start: chartRangeRolling.checked ? '' : chartStart.value,
        end: chartRangeRolling.checked ? '' : chartEnd.value,
        lookback: chartType.value === 'heatmap' && chartRangeRolling.checked ? currentLookback() : '',
        output: chartOutput.value.trim(),
        runOnSave: chartRunOnSave.checked,
      };
      try {
        const result = await postJson('/api/chart-config/heatmap', payload);
        selectedChartId = result.id;
        await loadChartList();
        clearChartError();
        statusNode.textContent = result.generated
          ? `Saved and generated chart ${result.id}.`
          : `Saved chart ${result.id}.`;
      } catch (error) {
        showChartError(error instanceof Error ? error.message : 'Chart save failed');
      }
    });

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

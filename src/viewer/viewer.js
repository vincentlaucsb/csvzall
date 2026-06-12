import {
  createDirtyStateEmitter,
  dirtyStateMessage,
  handleUnsavedBeforeUnload,
  installUnsavedChangesBeforeUnload,
  postDirtyState,
} from './modules/dirty-state.mjs';
import { createFallbackContextMenu } from './modules/fallback-menu.mjs';
import { currentGridColumns, rowInsertionIndex } from './modules/grid.mjs';
import {
  createTablerIcon,
  decorateButton,
  decorateIconButton,
  decorateMenuButton,
} from './modules/icons.mjs';
import { rowsToMarkdownTable } from './modules/markdown.mjs';
import { buildSavePayload, saveViewerState } from './modules/save.mjs';
import { createViewStateStore } from './modules/state.mjs';
import { defaultSqlQuery, VIEWER_SQL_TABLE_NAME } from './modules/sql.mjs';

if (typeof globalThis !== 'undefined') {
  globalThis.csvzallViewerInternals = {
    createViewStateStore,
    currentGridColumns,
    defaultSqlQuery,
    rowInsertionIndex,
    rowsToMarkdownTable,
    buildSavePayload,
    saveViewerState,
    dirtyStateMessage,
    postDirtyState,
    createDirtyStateEmitter,
    handleUnsavedBeforeUnload,
    installUnsavedChangesBeforeUnload,
  };
}

async function csvzallViewBootstrap(dependencies = {}) {
  const { createContextMenu, createDropdownMenu } = dependencies;
  const statusNode = document.getElementById('status');
  const summaryNode = document.getElementById('summary');
  const recordCountNode = document.getElementById('record-count');
  const fileNode = document.getElementById('file-name');
  const quickFilterNode = document.getElementById('quick-filter');
  const queryControl = document.getElementById('query-control');
  const searchModeButton = document.getElementById('query-mode-search');
  const sqlModeButton = document.getElementById('query-mode-sql');
  const clearQueryButton = document.getElementById('clear-query');
  const sqlErrorNode = document.getElementById('sql-error');
  const sqlToolbar = document.getElementById('sql-toolbar');
  const runSqlButton = document.getElementById('run-sql');
  const copyMarkdownButton = document.getElementById('copy-markdown');
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

  function setGridRowData(api, rows) {
    if (api.setGridOption) {
      api.setGridOption('rowData', rows);
      return;
    }
    api.setRowData(rows);
  }

  function setGridColumnDefs(api, columns, options = {}) {
    const columnDefs = columns.map((column) => ({
      headerName: column,
      field: column,
      sortable: options.sortable === true,
      filter: options.filter === true,
      editable: options.editable === true,
      resizable: true,
      minWidth: 140,
      flex: 1,
    }));
    if (api.setGridOption) {
      api.setGridOption('columnDefs', columnDefs);
      return;
    }
    api.setColumnDefs(columnDefs);
  }

  function autoResizeQueryInput() {
    quickFilterNode.style.height = 'auto';
    quickFilterNode.style.height = `${Math.min(quickFilterNode.scrollHeight, 144)}px`;
  }

  function showSqlError(message) {
    sqlErrorNode.textContent = message;
    sqlErrorNode.hidden = false;
  }

  function clearSqlError() {
    sqlErrorNode.textContent = '';
    sqlErrorNode.hidden = true;
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

  function populateColumnChecklist(container, columns, selectedColumns = []) {
    const selected = new Set(selectedColumns);
    container.textContent = '';
    columns.forEach((column) => {
      const label = document.createElement('label');
      const checkbox = document.createElement('input');
      checkbox.type = 'checkbox';
      checkbox.value = column;
      checkbox.checked = selected.size === 0 || selected.has(column);
      label.append(checkbox, document.createTextNode(column));
      container.append(label);
    });
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
    const insertMenuButton = document.getElementById('insert-menu');
    const deleteMenuButton = document.getElementById('delete-menu');
    const resetButton = document.getElementById('reset');
    const saveButton = document.getElementById('save');
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
    const chartDialog = document.getElementById('heatmap-chart-dialog');
    const chartForm = document.getElementById('heatmap-chart-form');
    const chartList = document.getElementById('chart-list');
    const newChartButton = document.getElementById('new-chart');
    const generateChartButton = document.getElementById('generate-chart');
    const chartFormTitle = document.getElementById('chart-form-title');
    const chartType = document.getElementById('chart-type');
    const chartId = document.getElementById('chart-id');
    const chartTitleField = document.getElementById('chart-title-field');
    const chartTitle = document.getElementById('chart-title');
    const chartColumnGrid = document.getElementById('chart-column-grid');
    const chartPrimaryColumnLabel = document.getElementById('chart-primary-column-label');
    const chartValueColumnLabel = document.getElementById('chart-value-column-label');
    const chartValueColumn2Label = document.getElementById('chart-value-column-2-label');
    const chartValueColumn2Field = document.getElementById('chart-value-column-2-field');
    const chartLabelColumnLabel = document.getElementById('chart-label-column-label');
    const chartDateColumn = document.getElementById('chart-date-column');
    const chartValueColumn = document.getElementById('chart-value-column');
    const chartValueColumn2 = document.getElementById('chart-value-column-2');
    const chartLabelColumn = document.getElementById('chart-label-column');
    const chartLayoutSection = document.getElementById('chart-layout-section');
    const chartRangeSection = document.getElementById('chart-range-section');
    const chartRangeFixed = document.getElementById('chart-range-fixed');
    const chartRangeRolling = document.getElementById('chart-range-rolling');
    const chartFixedRange = document.getElementById('chart-fixed-range');
    const chartRollingRange = document.getElementById('chart-rolling-range');
    const chartOrientation = document.getElementById('chart-orientation');
    const chartPresentationField = document.getElementById('chart-presentation-field');
    const chartPresentation = document.getElementById('chart-presentation');
    const chartColorSchemeField = document.getElementById('chart-color-scheme-field');
    const chartColorScheme = document.getElementById('chart-color-scheme');
    const chartMarkdownTableSection = document.getElementById('chart-markdown-table-section');
    const chartMarkdownColumns = document.getElementById('chart-markdown-columns');
    const chartMarkdownSql = document.getElementById('chart-markdown-sql');
    const chartStart = document.getElementById('chart-start');
    const chartEnd = document.getElementById('chart-end');
    const chartLookbackCount = document.getElementById('chart-lookback-count');
    const chartLookbackUnit = document.getElementById('chart-lookback-unit');
    const chartOutput = document.getElementById('chart-output');
    const chartRunOnSave = document.getElementById('chart-run-on-save');
    const chartError = document.getElementById('chart-error');
    const cancelChart = document.getElementById('cancel-chart');
    decorateButton(addChartButton, 'chart-bar', 'Charts');
    decorateMenuButton(insertMenuButton, 'plus', 'Insert');
    decorateMenuButton(deleteMenuButton, 'trash', 'Delete');
    decorateButton(resetButton, 'restore', 'Reset');
    decorateButton(saveButton, 'device-floppy', 'Save');
    decorateIconButton(clearQueryButton, 'x', 'Clear filter');
    decorateButton(runSqlButton, 'player-play', 'Run');
    decorateButton(copyMarkdownButton, 'copy', 'Copy Markdown');
    decorateButton(cancelInsertColumn, 'x', 'Cancel');
    decorateButton(insertColumnForm.querySelector('button[type="submit"]'), 'plus', 'Insert');
    decorateButton(cancelRenameColumn, 'x', 'Cancel');
    decorateButton(renameColumnForm.querySelector('button[type="submit"]'), 'pencil', 'Rename');
    decorateButton(newChartButton, 'plus', 'New');
    decorateButton(cancelChart, 'x', 'Cancel');
    decorateButton(generateChartButton, 'chart-bar', 'Create chart');
    decorateButton(chartForm.querySelector('button[type="submit"]'), 'device-floppy', 'Save chart');
    queryControl.hidden = !materialized;
    quickFilterNode.disabled = !materialized;
    editToolbar.hidden = !editable;
    addChartButton.hidden = false;

    const baseName = stripCsvExtension(schema.file);
    const baseSlug = slugify(baseName);
    const sqlTableName = schema.sqlTableName || VIEWER_SQL_TABLE_NAME;
    const sqlDefaultQuery = defaultSqlQuery(sqlTableName);
    chartMarkdownSql.placeholder = `SELECT * FROM ${sqlTableName}`;
    const guessedDateColumn = () => guessColumn(schema.columns, ['date', 'day', 'attendance_date']);
    const guessedValueColumn = () => guessColumn(schema.columns, ['count', 'value', 'total']);
    const guessedLabelColumn = () => guessColumn(schema.columns, ['content', 'label', 'note', 'description']);
    let currentCharts = [];
    let selectedChartId = '';
    populateColumnSelect(chartDateColumn, schema.columns, { preferred: guessedDateColumn() });
    populateColumnSelect(chartValueColumn, schema.columns, { optional: true, preferred: guessedValueColumn() });
    populateColumnSelect(chartValueColumn2, schema.columns, { optional: true });
    populateColumnSelect(chartLabelColumn, schema.columns, { optional: true, preferred: guessedLabelColumn() });
    populateColumnChecklist(chartMarkdownColumns, schema.columns);

    const chartValidationFields = [
      chartType,
      chartId,
      chartDateColumn,
      chartValueColumn,
      chartValueColumn2,
      chartLabelColumn,
      chartOrientation,
      chartPresentation,
      chartColorScheme,
      chartMarkdownColumns,
      chartMarkdownSql,
      chartStart,
      chartEnd,
      chartLookbackCount,
      chartOutput,
    ];
    const clearChartFieldErrors = () => {
      chartValidationFields.forEach((field) => {
        field.removeAttribute('aria-invalid');
        field.removeAttribute('title');
      });
    };
    const chartFieldsForError = (message) => {
      if (message.includes('chart id is required')) return [chartId];
      if (message.includes('chart not found')) return [chartId];
      if (message.includes('unknown chart type')) return [chartType];
      if (message.includes('output path')) return [chartOutput];
      if (message.includes('heatmap: date column')) return [chartDateColumn];
      if (message.includes('orientation')) return [chartOrientation];
      if (message.includes('start and end dates')) return [chartStart, chartEnd];
      if (message.includes('lookback cannot be combined')) return [chartStart, chartLookbackCount];
      if (message.includes('lookback')) return [chartLookbackCount];
      if (message.includes('bar: label column')) return [chartDateColumn];
      if (message.includes('bar: value column')) return [chartValueColumn];
      if (message.includes('presentation')) return [chartPresentation];
      if (message.includes('colorScheme')) return [chartColorScheme];
      if (message.includes('line: x column')) return [chartDateColumn];
      if (message.includes('line: y column')) return [chartValueColumn];
      if (message.includes('series column cannot be combined')) {
        return [chartLabelColumn, chartValueColumn, chartValueColumn2];
      }
      if (message.includes('markdown-table')) return [chartMarkdownSql, chartMarkdownColumns];
      if (message.includes('no such column')) {
        return chartMarkdownSql.value.trim() ? [chartMarkdownSql] : [chartMarkdownColumns, chartMarkdownSql];
      }
      return [];
    };
    const markChartFieldsForError = (message) => {
      const fields = chartFieldsForError(message);
      fields.forEach((field) => {
        field.setAttribute('aria-invalid', 'true');
        field.setAttribute('title', message);
      });
      fields[0]?.focus({ preventScroll: true });
    };
    const showChartError = (message, { markFields = false } = {}) => {
      chartError.textContent = message;
      chartError.hidden = false;
      if (markFields) {
        clearChartFieldErrors();
        markChartFieldsForError(message);
      }
    };
    const clearChartError = () => {
      chartError.textContent = '';
      chartError.hidden = true;
      clearChartFieldErrors();
    };
    chartValidationFields.forEach((field) => {
      const clearFieldError = () => {
        field.removeAttribute('aria-invalid');
        field.removeAttribute('title');
      };
      field.addEventListener('input', clearFieldError);
      field.addEventListener('change', clearFieldError);
    });

    const chartTypeTitle = (type) => {
      if (type === 'bar') return 'Bar';
      if (type === 'line') return 'Line';
      if (type === 'markdown-table') return 'Markdown Table';
      return 'Heatmap';
    };

    const defaultChartValues = (type = chartType.value || 'heatmap') => {
      const markdown = type === 'markdown-table';
      return {
        type,
        id: `${baseSlug}-${type}`,
        title: markdown ? '' : baseName,
        date: guessedDateColumn(),
        value: guessedValueColumn(),
        value2: '',
        label: guessedLabelColumn(),
        orientation: 'months-horizontal',
        presentation: 'stacked',
        colorScheme: 'sequential',
        start: '',
        end: '',
        lookback: markdown ? '' : '1y',
        output: markdown ? `charts/${baseSlug}_${type}.md` : `charts/${baseSlug}_${type}.svg`,
        columns: markdown ? schema.columns : [],
        sql: '',
        runOnSave: true,
      };
    };

    const parseLookback = (lookback) => {
      const match = String(lookback || '').trim().match(/^(\d+)\s*(d|day|days|y|year|years)?$/i);
      if (!match) {
        return { count: '1', unit: 'y' };
      }
      const unit = (match[2] || 'd').toLowerCase().startsWith('y') ? 'y' : 'd';
      return { count: match[1], unit };
    };

    const currentLookback = () => `${chartLookbackCount.value.trim()}${chartLookbackUnit.value}`;

    const setRangeMode = (mode) => {
      const rolling = mode === 'rolling';
      chartRangeFixed.checked = !rolling;
      chartRangeRolling.checked = rolling;
      chartFixedRange.hidden = rolling;
      chartRollingRange.hidden = !rolling;
    };

    const setChartType = (type) => {
      chartType.value = type;
      chartFormTitle.textContent = chartTypeTitle(type);
      const markdown = type === 'markdown-table';
      chartTitleField.hidden = markdown;
      chartColumnGrid.hidden = markdown;
      chartMarkdownTableSection.hidden = !markdown;
      chartLayoutSection.hidden = type !== 'heatmap';
      chartRangeSection.hidden = type !== 'heatmap';
      chartPresentationField.hidden = type !== 'bar';
      chartColorSchemeField.hidden = true;
      chartLabelColumn.closest('label').hidden = type === 'bar';
      if (markdown) {
        return;
      }
      chartValueColumnLabel.textContent = type === 'heatmap' ? 'Weight column' : 'Value column';
      chartValueColumn2Label.textContent = type === 'heatmap' ? 'Weight column 2' : 'Value column 2';
      chartValueColumn2Field.hidden = false;
      if (type === 'heatmap') {
        chartPrimaryColumnLabel.textContent = 'Date column';
        chartLabelColumnLabel.textContent = 'Label column';
      } else if (type === 'bar') {
        chartPrimaryColumnLabel.textContent = 'Label column';
        chartLabelColumnLabel.textContent = 'Unused';
        if (!chartValueColumn.value) chartValueColumn.value = guessedValueColumn() || schema.columns[0] || '';
      } else {
        chartPrimaryColumnLabel.textContent = 'X column';
        chartLabelColumnLabel.textContent = 'Series column';
        if (!chartValueColumn.value) chartValueColumn.value = guessedValueColumn() || schema.columns[0] || '';
      }
    };

    const optionValues = (options, scalarKey) => {
      const explicit = Array.isArray(options?.values)
        ? options.values.map((value) => typeof value === 'string' ? value : value?.column).filter(Boolean)
        : [];
      if (explicit.length > 0) return explicit;
      const scalar = options?.[scalarKey] ?? '';
      return scalar ? [scalar] : [];
    };

    const selectedValueColumns = () => {
      return [chartValueColumn.value, chartValueColumn2.value].filter(Boolean);
    };

    const updateColorSchemeVisibility = () => {
      const supportsColorScheme = chartType.value === 'bar' || chartType.value === 'line';
      chartColorSchemeField.hidden = !supportsColorScheme || selectedValueColumns().length <= 1;
    };

    const selectedMarkdownColumns = () => {
      return Array.from(chartMarkdownColumns.querySelectorAll('input[type="checkbox"]:checked'))
        .map((input) => input.value);
    };

    const updateMarkdownColumnsAvailability = () => {
      const disabled = chartMarkdownSql.value.trim().length > 0;
      chartMarkdownColumns.setAttribute('aria-disabled', String(disabled));
      chartMarkdownColumns.querySelectorAll('input[type="checkbox"]').forEach((input) => {
        input.disabled = disabled;
      });
    };

    const refreshChartColumnOptions = (values = {}) => {
      populateColumnSelect(chartDateColumn, schema.columns, { preferred: values.date ?? guessedDateColumn() });
      populateColumnSelect(chartValueColumn, schema.columns, { optional: true, preferred: values.value ?? guessedValueColumn() });
      populateColumnSelect(chartValueColumn2, schema.columns, { optional: true, preferred: values.value2 ?? '' });
      populateColumnSelect(chartLabelColumn, schema.columns, { optional: true, preferred: values.label ?? guessedLabelColumn() });
      populateColumnChecklist(chartMarkdownColumns, schema.columns, values.columns ?? []);
    };

    const applyChartValues = (values) => {
      refreshChartColumnOptions(values);
      setChartType(values.type ?? 'heatmap');
      chartId.value = values.id ?? '';
      chartTitle.value = values.title ?? '';
      chartDateColumn.value = values.date ?? '';
      chartValueColumn.value = values.value ?? '';
      chartValueColumn2.value = values.value2 ?? '';
      updateColorSchemeVisibility();
      chartLabelColumn.value = values.label ?? '';
      chartOrientation.value = values.orientation ?? 'months-horizontal';
      chartPresentation.value = values.presentation ?? 'stacked';
      chartColorScheme.value = values.colorScheme ?? 'sequential';
      chartMarkdownSql.value = values.sql ?? '';
      updateMarkdownColumnsAvailability();
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
        value: optionValues(chart.options, type === 'line' ? 'y' : 'value')[0] ?? '',
        value2: optionValues(chart.options, type === 'line' ? 'y' : 'value')[1] ?? '',
        label: chart.options?.label ?? chart.options?.series ?? '',
        orientation: chart.options?.orientation ?? 'months-horizontal',
        presentation: chart.options?.presentation ?? 'stacked',
        colorScheme: chart.options?.colorScheme ?? 'sequential',
        columns: Array.isArray(chart.options?.columns) ? chart.options.columns : [],
        sql: chart.options?.sql ?? '',
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
    chartValueColumn.addEventListener('change', updateColorSchemeVisibility);
    chartValueColumn2.addEventListener('change', updateColorSchemeVisibility);
    chartMarkdownSql.addEventListener('input', updateMarkdownColumnsAvailability);
    cancelChart.addEventListener('click', () => {
      chartDialog.close('cancel');
    });
    generateChartButton.addEventListener('click', async () => {
      const id = chartId.value.trim();
      try {
        const result = await postJson('/api/chart-config/generate', { id });
        statusNode.textContent = `Generated chart ${result.id}.`;
        clearChartError();
      } catch (error) {
        showChartError(error instanceof Error ? error.message : 'Chart generation failed', { markFields: true });
      }
    });
    chartForm.addEventListener('submit', async (event) => {
      event.preventDefault();
      const valueColumns = selectedValueColumns();
      const multiValues = valueColumns.length > 1 ? valueColumns : [];
      const markdownSql = chartMarkdownSql.value.trim();
      const payload = {
        type: chartType.value,
        id: chartId.value.trim(),
        title: chartTitle.value.trim(),
        date: chartType.value === 'heatmap' ? chartDateColumn.value : '',
        value: chartType.value !== 'line' && valueColumns.length <= 1 ? (valueColumns[0] ?? '') : '',
        values: multiValues,
        label: chartType.value === 'heatmap'
          ? chartLabelColumn.value
          : (chartType.value === 'bar' ? chartDateColumn.value : ''),
        x: chartType.value === 'line' ? chartDateColumn.value : '',
        y: chartType.value === 'line' && valueColumns.length <= 1 ? (valueColumns[0] ?? '') : '',
        series: chartType.value === 'line' && valueColumns.length <= 1 ? chartLabelColumn.value : '',
        orientation: chartType.value === 'heatmap' ? chartOrientation.value : '',
        presentation: chartType.value === 'bar' ? chartPresentation.value : '',
        colorScheme: (chartType.value === 'bar' || chartType.value === 'line') ? chartColorScheme.value : '',
        columns: chartType.value === 'markdown-table' && !markdownSql ? selectedMarkdownColumns() : [],
        sql: chartType.value === 'markdown-table' ? markdownSql : '',
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
        chartDialog.close('saved');
      } catch (error) {
        showChartError(error instanceof Error ? error.message : 'Chart save failed', { markFields: true });
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
      const viewState = createViewStateStore();
      if (editable) {
        installUnsavedChangesBeforeUnload(viewState);
      }
      const emitDirtyState = editable ? createDirtyStateEmitter(viewState) : () => false;
      emitDirtyState();
      const allRows = schema.totalRows === 0
        ? []
        : rowsToObjects(schema.columns, (await fetchJson('/api/rows', { offset: 0, limit: schema.totalRows })).rows);
      let queryMode = 'search';
      let searchText = '';
      let sqlText = '';
      let sqlResultColumns = [];
      let sqlResultRows = [];
      const refreshToolbarForMode = () => {
        const sqlMode = queryMode === 'sql';
        queryControl.classList.toggle('sql-mode', sqlMode);
        searchModeButton.classList.toggle('active', !sqlMode);
        sqlModeButton.classList.toggle('active', sqlMode);
        searchModeButton.setAttribute('aria-pressed', String(!sqlMode));
        sqlModeButton.setAttribute('aria-pressed', String(sqlMode));
        sqlToolbar.hidden = !sqlMode;
        editToolbar.hidden = sqlMode || !editable;
        addChartButton.hidden = sqlMode;
        quickFilterNode.placeholder = sqlMode
          ? `SELECT * FROM ${sqlTableName}`
          : 'Type to filter loaded table...';
        clearQueryButton.title = sqlMode ? 'Clear query' : 'Clear filter';
        clearQueryButton.setAttribute('aria-label', sqlMode ? 'Clear query' : 'Clear filter');
      };
      const applySearchMode = () => {
        queryMode = 'search';
        quickFilterNode.value = searchText;
        clearSqlError();
        refreshToolbarForMode();
        setGridColumnDefs(api, schema.columns, { sortable: true, filter: true, editable });
        setGridRowData(api, allRows);
        setQuickFilter(api, searchText);
        recordCountNode.textContent = `${schema.totalRows.toLocaleString()} rows, ${schema.columns.length.toLocaleString()} columns`;
        autoResizeQueryInput();
        refreshDirtyUi();
      };
      const applySqlResult = (result) => {
        sqlResultColumns = Array.isArray(result.columns) ? result.columns : [];
        sqlResultRows = rowsToObjects(sqlResultColumns, Array.isArray(result.rows) ? result.rows : []);
        setQuickFilter(api, '');
        setGridColumnDefs(api, sqlResultColumns, { sortable: true, filter: false, editable: false });
        setGridRowData(api, sqlResultRows);
        copyMarkdownButton.disabled = sqlResultColumns.length === 0;
        recordCountNode.textContent =
          `${sqlResultRows.length.toLocaleString()} SQL result row${sqlResultRows.length === 1 ? '' : 's'}, ${sqlResultColumns.length.toLocaleString()} columns`;
        statusNode.textContent = `SQL result: ${sqlResultRows.length.toLocaleString()} row${sqlResultRows.length === 1 ? '' : 's'}.`;
        api.sizeColumnsToFit?.();
      };
      const runSqlQuery = async () => {
        if (queryMode !== 'sql') {
          return;
        }
        runSqlButton.disabled = true;
        copyMarkdownButton.disabled = true;
        statusNode.textContent = 'Running SQL...';
        try {
          const result = await postJson('/api/sql-query', { sql: quickFilterNode.value });
          sqlText = quickFilterNode.value;
          clearSqlError();
          applySqlResult(result);
        } catch (error) {
          copyMarkdownButton.disabled = sqlResultColumns.length === 0;
          showSqlError(error instanceof Error ? error.message : 'SQL query failed');
          statusNode.textContent = 'SQL query failed.';
        } finally {
          runSqlButton.disabled = false;
        }
      };
      const applySqlMode = () => {
        queryMode = 'sql';
        searchText = searchText || '';
        if (!sqlText) {
          sqlText = sqlDefaultQuery;
        }
        quickFilterNode.value = sqlText;
        refreshToolbarForMode();
        autoResizeQueryInput();
        void runSqlQuery();
      };
      const refreshDirtyUi = () => {
        saveButton.disabled = !viewState.dirty;
        resetButton.disabled = !viewState.dirty;
        if (queryMode === 'sql') {
          return;
        }
        statusNode.textContent = viewState.dirty
          ? 'Unsaved changes.'
          : `Loaded ${allRows.length.toLocaleString()} rows for ${editable ? 'editing' : 'client-side sort/filter'}.`;
        emitDirtyState();
      };
      const markDataDirty = () => {
        viewState.markDataDirty();
        refreshDirtyUi();
      };
      setGridRowData(api, allRows);
      quickFilterNode.addEventListener('input', () => {
        autoResizeQueryInput();
        if (queryMode === 'search') {
          searchText = quickFilterNode.value;
          setQuickFilter(api, searchText);
          return;
        }
        clearSqlError();
      });
      quickFilterNode.addEventListener('keydown', (event) => {
        if (queryMode === 'sql' && event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
          event.preventDefault();
          void runSqlQuery();
        }
      });
      searchModeButton.addEventListener('click', applySearchMode);
      sqlModeButton.addEventListener('click', applySqlMode);
      clearQueryButton.addEventListener('click', () => {
        quickFilterNode.value = '';
        autoResizeQueryInput();
        if (queryMode === 'search') {
          searchText = '';
          setQuickFilter(api, '');
          quickFilterNode.focus();
          return;
        }
        sqlText = '';
        void runSqlQuery();
        quickFilterNode.focus();
      });
      runSqlButton.addEventListener('click', () => {
        void runSqlQuery();
      });
      copyMarkdownButton.addEventListener('click', async () => {
        try {
          await navigator.clipboard.writeText(rowsToMarkdownTable(sqlResultColumns, sqlResultRows));
          statusNode.textContent = 'Copied Markdown table.';
        } catch (error) {
          statusNode.textContent = error instanceof Error ? error.message : 'Copy failed';
        }
      });
      refreshToolbarForMode();
      autoResizeQueryInput();
      if (editable) {
        const refreshRows = () => {
          setGridRowData(api, allRows);
        };
        const refreshColumns = () => {
          setGridColumnDefs(api, schema.columns, { sortable: true, filter: true, editable });
        };
        const selectedSourceRow = () => {
          const selected = api.getSelectedRows ? api.getSelectedRows() : [];
          if (Number.isInteger(selected[0]?._csvzallRowId)) {
            return selected[0]._csvzallRowId;
          }
          const focused = api.getFocusedCell ? api.getFocusedCell() : null;
          if (focused && Number.isInteger(focused.rowIndex) && typeof api.getDisplayedRowAtIndex === 'function') {
            const focusedNode = api.getDisplayedRowAtIndex(focused.rowIndex);
            if (Number.isInteger(focusedNode?.data?._csvzallRowId)) {
              return focusedNode.data._csvzallRowId;
            }
          }
          return allRows.length;
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
        let pendingRenameColumn = '';
        const showColumnError = (message) => {
          insertColumnError.textContent = message;
          insertColumnError.hidden = false;
        };
        const clearColumnError = () => {
          insertColumnError.textContent = '';
          insertColumnError.hidden = true;
        };
        const showRenameColumnError = (message) => {
          renameColumnError.textContent = message;
          renameColumnError.hidden = false;
        };
        const clearRenameColumnError = () => {
          renameColumnError.textContent = '';
          renameColumnError.hidden = true;
        };
        const insertColumnAt = async (column, name) => {
          await postJson('/api/insert-column', { column, name, value: '' });
          schema.columns.splice(column, 0, name);
          allRows.forEach((row) => {
            row[name] = '';
          });
          refreshColumns();
          refreshRows();
          markDataDirty();
        };
        const insertRowAt = async (row) => {
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
            markDataDirty();
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Insert failed';
          }
        };
        const insertRowRelativeToSelection = async (offset) => {
          await insertRowAt(rowInsertionIndex(selectedSourceRow(), allRows.length, offset));
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
            markDataDirty();
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Delete failed';
          }
        };
        const moveRowBy = async (row, delta) => {
          const target = row + delta;
          if (row < 0 || row >= allRows.length || target < 0 || target >= allRows.length) {
            return;
          }
          try {
            await postJson('/api/swap-rows', { first: row, second: target });
            const [moved] = allRows.splice(row, 1);
            allRows.splice(target, 0, moved);
            renumberRows(allRows);
            refreshRows();
            api.forEachNode?.((node) => {
              if (node.data?._csvzallRowId === target) {
                node.setSelected?.(true);
              }
            });
            markDataDirty();
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Move failed';
          }
        };
        const deleteColumnByName = async (column) => {
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
            markDataDirty();
          } catch (error) {
            statusNode.textContent = error instanceof Error ? error.message : 'Column delete failed';
          }
        };
        const renameColumnByName = async (column, name) => {
          try {
            await postJson('/api/rename-column', { column, name });
            const columnIndex = schema.columns.indexOf(column);
            if (columnIndex >= 0) {
              schema.columns[columnIndex] = name;
            }
            allRows.forEach((row) => {
              row[name] = row[column] ?? '';
              delete row[column];
            });
            refreshColumns();
            refreshRows();
            markDataDirty();
          } catch (error) {
            throw new Error(error instanceof Error ? error.message : 'Column rename failed');
          }
        };
        const columnIndexFor = (column) => {
          const columnIndex = schema.columns.indexOf(column || focusedColumnName());
          return columnIndex >= 0 ? columnIndex : schema.columns.length;
        };
        const showInsertColumnDialog = (column = '', offset = 0) => {
          const columnIndex = columnIndexFor(column);
          pendingInsertColumn = Math.min(columnIndex + offset, schema.columns.length);
          insertColumnName.value = '';
          clearColumnError();
          insertColumnDialog.showModal();
          insertColumnName.focus();
        };
        const showRenameColumnDialog = (column) => {
          if (!column || !schema.columns.includes(column)) {
            statusNode.textContent = 'Choose a column to rename.';
            return;
          }
          pendingRenameColumn = column;
          renameColumnName.value = column;
          clearRenameColumnError();
          renameColumnDialog.showModal();
          renameColumnName.focus();
          renameColumnName.select();
        };
        const createToolbarDropdown = (button, items, onSelect) => {
          if (typeof createDropdownMenu === 'function') {
            return createDropdownMenu(button, {
              items,
              theme: 'system',
              onSelect,
            });
          }
          if (typeof createContextMenu !== 'function') {
            return null;
          }
          const menu = createContextMenu({
            trigger: 'manual',
            theme: 'system',
            items,
            onSelect,
          });
          button.addEventListener('click', (event) => {
            const rect = button.getBoundingClientRect();
            menu.open({
              x: rect.left,
              y: rect.bottom + 4,
              target: button,
              triggerEvent: event,
            });
          });
          return menu;
        };
        gridOptions.onCellValueChanged = async (event) => {
          if (queryMode === 'sql') {
            return;
          }
          if (!event.colDef.field || event.colDef.field === '_csvzallRowId') {
            return;
          }
          try {
            await postJson('/api/edit-cell', {
              row: event.data._csvzallRowId,
              column: event.colDef.field,
              value: event.newValue ?? '',
            });
            markDataDirty();
          } catch (error) {
            event.node.setDataValue(event.colDef.field, event.oldValue ?? '');
            statusNode.textContent = error instanceof Error ? error.message : 'Edit failed';
          }
        };
        if (api.setGridOption) {
          api.setGridOption('onCellValueChanged', gridOptions.onCellValueChanged);
        }
        gridOptions.onColumnMoved = (event) => {
          if (event.finished !== true) {
            return;
          }
          const columns = currentGridColumns(api, schema.columns);
          if (columns.length !== schema.columns.length) {
            return;
          }
          const changed = !columns.every((column, index) => column === schema.columns[index]);
          viewState.setColumnOrderDirty(changed);
          refreshDirtyUi();
        };
        if (api.setGridOption) {
          api.setGridOption('onColumnMoved', gridOptions.onColumnMoved);
        }
        if (typeof createContextMenu === 'function') {
          const rowMenu = createContextMenu({
            trigger: 'manual',
            theme: 'system',
            items: ({ data }) => {
              const row = data?.row;
              return [
                { id: 'move-up', label: 'Move Up', icon: () => createTablerIcon('arrow-up'), disabled: !Number.isInteger(row) || row <= 0 },
                { id: 'move-down', label: 'Move Down', icon: () => createTablerIcon('arrow-down'), disabled: !Number.isInteger(row) || row >= allRows.length - 1 },
                { type: 'separator' },
                { id: 'insert-row-before', label: 'Insert Row Before', icon: () => createTablerIcon('row-insert-bottom') },
                { id: 'insert-row-after', label: 'Insert Row After', icon: () => createTablerIcon('row-insert-bottom') },
                { type: 'separator' },
                { id: 'delete-row', label: 'Delete Row', icon: () => createTablerIcon('trash'), variant: 'danger' },
                { id: 'delete-column', label: 'Delete Column', icon: () => createTablerIcon('column-remove'), variant: 'danger' },
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
                void insertRowAt(rowInsertionIndex(row, allRows.length, 0));
              }
              if (event.id === 'insert-row-after' && Number.isInteger(row)) {
                void insertRowAt(rowInsertionIndex(row, allRows.length, 1));
              }
              if (event.id === 'delete-row' && Number.isInteger(row)) {
                void deleteRowAt(row);
              }
              if (event.id === 'delete-column' && typeof column === 'string') {
                void deleteColumnByName(column);
              }
            },
          });
          gridOptions.onCellContextMenu = (event) => {
            if (queryMode === 'sql') {
              return;
            }
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
          if (api.setGridOption) {
            api.setGridOption('onCellContextMenu', gridOptions.onCellContextMenu);
          }
          const columnMenu = createContextMenu({
            trigger: 'manual',
            theme: 'system',
            items: [
              { id: 'rename-column', label: 'Rename Column', icon: () => createTablerIcon('pencil') },
              { type: 'separator' },
              { id: 'delete-column', label: 'Delete Column', icon: () => createTablerIcon('column-remove'), variant: 'danger' },
            ],
            onSelect(event) {
              const column = event.context.data?.column;
              if (event.id === 'rename-column' && typeof column === 'string') {
                showRenameColumnDialog(column);
              }
              if (event.id === 'delete-column' && typeof column === 'string') {
                void deleteColumnByName(column);
              }
            },
          });
          gridElement.addEventListener('contextmenu', (event) => {
            if (queryMode === 'sql') {
              return;
            }
            const headerCell = event.target?.closest?.('.ag-header-cell[col-id]');
            const column = headerCell?.getAttribute('col-id') ?? '';
            if (!column || !schema.columns.includes(column)) {
              return;
            }
            event.preventDefault();
            columnMenu.open({
              x: event.clientX,
              y: event.clientY,
              target: headerCell,
              triggerEvent: event,
              context: { column },
            });
          });
        }
        createToolbarDropdown(insertMenuButton, [
          { id: 'insert-row-before', label: 'Row Before', icon: () => createTablerIcon('row-insert-bottom') },
          { id: 'insert-row-after', label: 'Row After', icon: () => createTablerIcon('row-insert-bottom') },
          { type: 'separator' },
          { id: 'insert-column-before', label: 'Column Before', icon: () => createTablerIcon('column-insert-right') },
          { id: 'insert-column-after', label: 'Column After', icon: () => createTablerIcon('column-insert-right') },
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
          if (name !== pendingRenameColumn && schema.columns.includes(name)) {
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
        createToolbarDropdown(deleteMenuButton, [
          { id: 'delete-row', label: 'Row', icon: () => createTablerIcon('row-remove'), variant: 'danger' },
          { id: 'delete-column', label: 'Column', icon: () => createTablerIcon('column-remove'), variant: 'danger' },
        ], (event) => {
          if (event.id === 'delete-row') {
            void deleteRowAt(selectedSourceRow());
          }
          if (event.id === 'delete-column') {
            void deleteColumnByName(focusedColumnName());
          }
        });
        resetButton.addEventListener('click', async () => {
          try {
            resetButton.disabled = true;
            saveButton.disabled = true;
            statusNode.textContent = 'Resetting…';
            await postJson('/api/reset');
            viewState.markClean();
            emitDirtyState();
            window.location.reload();
          } catch (error) {
            resetButton.disabled = !viewState.dirty;
            saveButton.disabled = !viewState.dirty;
            statusNode.textContent = error instanceof Error ? error.message : 'Reset failed';
          }
        });
        saveButton.addEventListener('click', async () => {
          try {
            saveButton.disabled = true;
            statusNode.textContent = 'Saving…';
            const { result, reloadAfterSave } = await saveViewerState({
              viewState,
              postJson,
              getColumns: () => currentGridColumns(api, schema.columns),
            });
            refreshDirtyUi();
            if (reloadAfterSave) {
              statusNode.textContent = 'Saved. Reloading…';
              window.location.reload();
              return;
            }
            if (result.chartError) {
              statusNode.textContent = `Saved ${allRows.length.toLocaleString()} rows. Chart generation failed: ${result.chartError}`;
            } else if (result.chartsGenerated > 0) {
              statusNode.textContent = `Saved ${allRows.length.toLocaleString()} rows. Regenerated ${result.chartsGenerated} chart${result.chartsGenerated === 1 ? '' : 's'}.`;
            } else {
              statusNode.textContent = `Saved ${allRows.length.toLocaleString()} rows.`;
            }
          } catch (error) {
            saveButton.disabled = !viewState.dirty;
            statusNode.textContent = error instanceof Error ? error.message : 'Save failed';
          }
        });
      }
      refreshDirtyUi();
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

async function loadMenuFactories() {
  try {
    const response = await fetch('/assets/popright/index.js', {
      cache: 'force-cache',
      credentials: 'same-origin',
    });
    if (!response.ok) {
      return { createContextMenu: createFallbackContextMenu, createDropdownMenu: null };
    }
    const popright = await import('/assets/popright/index.js');
    return {
      createContextMenu: popright.createContextMenu,
      createDropdownMenu: popright.createDropdownMenu ?? null,
    };
  } catch (error) {
    return { createContextMenu: createFallbackContextMenu, createDropdownMenu: null };
  }
}

if (typeof window !== 'undefined' && typeof document !== 'undefined') {
  loadMenuFactories().then((menuFactories) => {
    void csvzallViewBootstrap(menuFactories);
  });
}

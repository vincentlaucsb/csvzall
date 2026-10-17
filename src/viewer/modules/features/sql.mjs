import { installSqlViewer } from '../sql.mjs';
import { decorateButton, decorateIconButton } from '../icons.mjs';
import { mountFeatureStyle } from './style.mjs';
import { applyHostGridTheme } from '../host-theme.mjs';
export const sqlFeature = {
  id: 'sql',
  mount(shell) {
    const doc = shell.gridElement.ownerDocument;
    const removeStyle = mountFeatureStyle(doc, new URL('./sql.css', import.meta.url));
    doc.getElementById('viewer-tools').insertAdjacentHTML('beforeend', '<button id="open-sql-query" type="button" aria-expanded="false" aria-controls="sqlite-query-toolbar">SQLite query</button>');
    doc.getElementById('feature-panels').insertAdjacentHTML('beforeend', `
    <section id="sqlite-query-toolbar" class="sqlite-query-toolbar" aria-label="SQLite query" hidden>
      <div class="query-input-shell">
        <textarea id="sql-query" rows="3" autocomplete="off" spellcheck="false" placeholder="SELECT * FROM data"></textarea>
        <button id="clear-query" type="button" aria-label="Clear query" title="Clear query">Clear</button>
      </div>
      <div class="query-actions">
        <div class="sql-toolbar">
          <button id="run-sql" type="button">Run query</button>
          <button id="copy-markdown" type="button" disabled>Copy Markdown</button>
          <button id="exit-sql-query" type="button">Exit query</button>
        </div>
        <p class="sql-help">Use SQLite syntax with table <code>data</code>, including unsaved edits. Ctrl/Cmd+Enter to run. Queries load the CSV into memory. <a id="sqlite-documentation" href="https://vincentlaucsb.github.io/csvzall/sqlite_capabilities.html" target="_blank" rel="noopener noreferrer">SQLite reference</a></p>
      </div>
      <p id="sql-error" class="sql-error" role="alert" hidden></p>
    </section>
`);
    shell.gridElement.insertAdjacentHTML('afterend', '<div id="sql-grid" class="ag-theme-alpine-auto-dark" aria-label="SQLite query results" hidden></div>');
    const node = id => doc.getElementById(id);
    applyHostGridTheme(node('sql-grid'), doc.documentElement.dataset.csvzallHostTheme);
    for (const [id, icon, label] of [['open-sql-query','code','SQLite query'],['run-sql','player-play','Run query'],['exit-sql-query','x','Exit query'],['copy-markdown','copy','Copy Markdown']]) decorateButton(node(id),icon,label);
    decorateIconButton(node('clear-query'),'x','Clear query');
    const viewer = installSqlViewer({
      elements: { panel: node('sqlite-query-toolbar'), documentationLink: node('sqlite-documentation'), openButton: node('open-sql-query'), exitButton: node('exit-sql-query'), input: node('sql-query'), clearButton: node('clear-query'), error: node('sql-error'), runButton: node('run-sql'), copyButton: node('copy-markdown'), tableGrid: shell.gridElement, resultGrid: node('sql-grid'), editToolbar: node('edit-toolbar'), status: node('status'), recordCount: node('record-count') },
      tableName: 'data', editable: shell.schema.editable,
      postJson: (path, body) => shell.backend.postJson(path, body),
      beforeQuery: () => shell.finishEditing(), finishEditing: () => { shell.gridApi.stopEditing(); shell.setResultMode(true); },
      restoreTable: () => shell.restoreTable(), createGrid: shell.createGrid,
      writeClipboard: text => navigator.clipboard.writeText(text),
    });
    shell.addColumnAction({ id: 'query-column', label: 'Query column', icon: 'player-play', run: column => viewer.queryColumn(column) });
    return { destroy() {
      viewer.destroy(); removeStyle();
      for (const id of ['open-sql-query', 'sqlite-query-toolbar', 'sql-grid']) node(id)?.remove();
    } };
  },
};

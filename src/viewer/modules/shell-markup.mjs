// Shared by native and WASM. Feature controls are mounted by their modules.
export const shellMarkup = `
  <div class="app-shell">
    <header class="topbar">
      <div class="file-meta">
        <h1 id="file-name">Loading…</h1>
        <span id="summary" class="summary" aria-hidden="true"></span>
      </div>
      <div id="file-tools" class="file-tools" hidden>
        <button id="install-app" type="button" hidden>Install app</button>
        <label class="file-picker"><span>Open CSV/TSV</span><input id="file-input" type="file" accept=".csv,.tsv,text/csv,text/tab-separated-values"></label>
      </div>
      <div class="toolbar-actions">
      <div id="viewer-tools" class="feature-toolbar" role="group" aria-label="CSV tools">
      </div>
      <div id="edit-toolbar" class="edit-toolbar">
        <button id="csv-options" type="button" disabled>Options</button>
        <span class="toolbar-divider" aria-hidden="true"></span>
        <button id="insert-menu" type="button">Insert</button>
        <button id="delete-menu" type="button">Delete</button>
        <button id="reset" type="button" disabled>Reset</button>
        <button id="save" type="button" disabled>Save</button>
      </div>
      <div class="overflow-toolbar"><button id="more-actions" type="button" aria-label="More actions" aria-haspopup="menu" hidden>…</button></div>
      </div>
    </header>
    <div id="feature-panels" style="display: contents"></div>
    <main class="grid-wrap">
      <div id="grid" class="ag-theme-alpine-auto-dark"></div>
    </main>
    <footer class="footer">
      <span id="status">Connecting…</span>
      <span id="record-count"></span>
    </footer>
  </div>
  <dialog id="csv-options-dialog" class="csvzall-dialog" aria-labelledby="csv-options-title">
    <form id="csv-options-form" class="dialog-form">
      <h2 id="csv-options-title">CSV options</h2>
      <p id="csv-options-current"></p>
      <p id="csv-options-headers"></p>
      <label class="csv-options-header-toggle"><input id="csv-options-has-headers" type="checkbox" aria-describedby="csv-options-header-help"><span>First row contains headers</span></label>
      <p id="csv-options-header-help" role="status" hidden></p>
      <p>When headers are off, A, B, C, … are display labels only and are never saved.</p>
      <details>
        <summary>Correct input dialect</summary>
        <fieldset>
          <legend>Read existing bytes</legend>
          <p>Ambiguous quoting keeps double quotes by default. If values look wrong, choose the file’s actual characters and read it again.</p>
          <label><span>Input delimiter (use \\t for tab)</span><input id="csv-input-delimiter" type="text" autocomplete="off"></label>
          <label><span>Input quoting character (empty for none)</span><input id="csv-input-quote" type="text" maxlength="1" autocomplete="off"></label>
          <p id="csv-input-help">Reading changes the view without writing the file.</p>
          <button id="csv-input-read" type="button">Read using these characters</button>
        </fieldset>
      </details>
      <fieldset>
        <legend>Output dialect</legend>
        <p>Change these values to convert the file on its next save.</p>
        <label><span>Delimiter (use \\t for tab)</span><input id="csv-options-delimiter" type="text" autocomplete="off" required></label>
        <label><span>Quoting character (empty for none)</span><input id="csv-options-quote" type="text" maxlength="1" autocomplete="off"></label>
        <button id="csv-options-standard" type="button">Use comma and double quotes</button>
      </fieldset>
      <p id="csv-options-error" class="dialog-error" hidden></p>
      <div class="dialog-actions">
        <button id="csv-options-cancel" type="button">Cancel</button>
        <button id="csv-options-apply" type="submit">Apply</button>
      </div>
    </form>
  </dialog>
  <dialog id="insert-column-dialog" class="csvzall-dialog">
    <form id="insert-column-form" class="dialog-form">
      <h2>Insert column</h2>
      <label>
        <span>Column name</span>
        <input id="insert-column-name" type="text" autocomplete="off" required>
      </label>
      <p id="insert-column-error" class="dialog-error" hidden></p>
      <div class="dialog-actions">
        <button id="cancel-insert-column" type="button">Cancel</button>
        <button type="submit">Insert</button>
      </div>
    </form>
  </dialog>
  <dialog id="rename-column-dialog" class="csvzall-dialog">
    <form id="rename-column-form" class="dialog-form">
      <h2>Rename column</h2>
      <label>
        <span>Column name</span>
        <input id="rename-column-name" type="text" autocomplete="off" required>
      </label>
      <p id="rename-column-error" class="dialog-error" hidden></p>
      <div class="dialog-actions">
        <button id="cancel-rename-column" type="button">Cancel</button>
        <button type="submit">Rename</button>
      </div>
    </form>
  </dialog>
  <div id="drop-overlay" class="drop-overlay" role="status" aria-live="polite" aria-hidden="true" hidden>
    <div class="drop-overlay-content">
      <strong>Drop one .csv or .tsv file to open</strong>
      <span>Only files ending in .csv or .tsv are supported.</span>
    </div>
  </div>
  <dialog id="loading-dialog" aria-labelledby="loading-title" aria-describedby="loading-detail">
    <div class="loading-panel">
      <div class="spinner" aria-hidden="true"></div>
      <div>
        <h2 id="loading-title">Loading CSV Engine</h2>
        <p id="loading-detail">The parser is starting in a background worker.</p>
      </div>
    </div>
  </dialog>
`;
export function mountViewerShell(root = document.body) {
  const template = root.ownerDocument.createElement('template');
  template.innerHTML = shellMarkup;
  root.append(template.content.cloneNode(true));
}

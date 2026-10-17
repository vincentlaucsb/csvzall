// Authenticated native HTTP is adapted to the same operations as the worker.
export function createNativeAdapter({
  windowRef = window, fetchRef = fetch,
  token = new URLSearchParams(windowRef.location.search).get('token'),
} = {}) {
  const headers = { 'X-Session-Token': token };
  let current;
  async function json(path, body, params = {}) {
    if (!token) throw new Error('Missing session token. Open the viewer from csvzall.');
    const url = new URL(path, windowRef.location.origin);
    for (const [key, value] of Object.entries(params)) url.searchParams.set(key, String(value));
    const response = await fetchRef(url, body === undefined ? { headers } : {
      method: 'POST', headers: { ...headers, 'Content-Type': 'application/json' }, body: JSON.stringify(body),
    });
    const text = await response.text();
    if (!response.ok) throw new Error(text.trim() || `HTTP ${response.status}`);
    return text ? JSON.parse(text) : {};
  }
  async function schema() {
    const result = await json('/api/schema');
    current = { ...result, rowCount: result.totalRows };
    return { ...current, columns: [...current.columns] };
  }
  const operations = {
    editCell: 'edit-cell', insertRow: 'insert-row', deleteRow: 'delete-row', swapRows: 'swap-rows',
    insertColumn: 'insert-column', deleteColumn: 'delete-column', renameColumn: 'rename-column', reset: 'reset',
  };
  return {
    capabilities: { localFiles: false, columnOrder: true },
    fetchJson: (path, params) => json(path, undefined, params),
    postJson: (path, body = {}) => json(path, body),
    async request(type, payload = {}) {
      if (type === 'init') return schema();
      if (type === 'readRows') return json('/api/rows', undefined, payload);
      if (!operations[type]) throw new Error(`Unsupported native operation: ${type}`);
      const body = type === 'editCell'
        ? { row: payload.rowIndex, column: payload.column, value: payload.value } : payload;
      const result = await json(`/api/${operations[type]}`, body);
      return ['editCell', 'swapRows'].includes(type) ? result : schema();
    },
    async save({ columns }) {
      const reordered = !columns.every((column, index) => current.columns[index] === column);
      const result = await json('/api/save', reordered ? { columns } : {});
      return { ...result, schema: await schema() };
    },
    async persist() { return true; },
  };
}

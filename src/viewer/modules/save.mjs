export function buildSavePayload(viewState, getColumns) {
  if (!viewState.columnOrderDirty) {
    return {};
  }
  return { columns: getColumns() };
}

export async function saveViewerState({ viewState, postJson, getColumns }) {
  const reloadAfterSave = viewState.columnOrderDirty;
  const result = await postJson('/api/save', buildSavePayload(viewState, getColumns));
  viewState.markClean();
  return { result, reloadAfterSave };
}

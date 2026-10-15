export function createViewStateStore() {
  let dataDirty = false;
  let columnOrderDirty = false;
  let revision = 0;

  return {
    get revision() { return revision; },
    get dataDirty() {
      return dataDirty;
    },
    get columnOrderDirty() {
      return columnOrderDirty;
    },
    get dirty() {
      return dataDirty || columnOrderDirty;
    },
    markDataDirty() {
      revision += 1;
      dataDirty = true;
    },
    markColumnOrderDirty() {
      revision += 1;
      columnOrderDirty = true;
    },
    setColumnOrderDirty(value) {
      revision += 1;
      columnOrderDirty = value === true;
    },
    markClean(expectedRevision) {
      if (expectedRevision !== undefined && expectedRevision !== revision) return false;
      dataDirty = false;
      columnOrderDirty = false;
      return true;
    },
  };
}

export function createViewStateStore() {
  let dataDirty = false;
  let columnOrderDirty = false;

  return {
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
      dataDirty = true;
    },
    markColumnOrderDirty() {
      columnOrderDirty = true;
    },
    setColumnOrderDirty(value) {
      columnOrderDirty = value === true;
    },
    markClean() {
      dataDirty = false;
      columnOrderDirty = false;
    },
  };
}

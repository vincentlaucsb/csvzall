export const HOST_SOURCE = 'obsidian-csvzall';
export const VIEWER_SOURCE = 'csvzall-wasm-viewer';

function isArrayBuffer(value) {
  return value instanceof ArrayBuffer;
}

function cloneOpenFileMessage(data) {
  if (!data || data.source !== HOST_SOURCE || data.type !== 'open-file') {
    return null;
  }
  if (typeof data.name !== 'string' || !isArrayBuffer(data.buffer)) {
    return null;
  }
  return {
    name: data.name || 'input.csv',
    buffer: data.buffer,
  };
}

export function createHostBridge({
  windowRef = window,
  onOpenFile,
  onHostModeChange = () => {},
} = {}) {
  if (typeof onOpenFile !== 'function') {
    throw new TypeError('createHostBridge requires onOpenFile');
  }

  const queuedOpenFiles = [];
  const targetWindow = windowRef.parent && windowRef.parent !== windowRef
    ? windowRef.parent
    : null;
  let initialized = false;
  let hostMode = false;
  let lastDirty;
  let runningOpen = Promise.resolve();

  function post(message, transfer = []) {
    if (!targetWindow) {
      return;
    }
    targetWindow.postMessage({
      source: VIEWER_SOURCE,
      ...message,
    }, '*', transfer);
  }

  function enterHostMode() {
    if (hostMode) {
      return;
    }
    hostMode = true;
    onHostModeChange(true);
  }

  function enqueueOpenFile(file) {
    enterHostMode();
    if (!initialized) {
      queuedOpenFiles.push(file);
      return;
    }
    runningOpen = runningOpen.then(() => onOpenFile(file));
  }

  async function flushQueuedOpenFiles() {
    while (queuedOpenFiles.length > 0) {
      const file = queuedOpenFiles.shift();
      await onOpenFile(file);
    }
  }

  function handleMessage(event) {
    const file = cloneOpenFileMessage(event.data);
    if (!file) {
      return;
    }
    enqueueOpenFile(file);
  }

  return {
    start() {
      windowRef.addEventListener('message', handleMessage);
    },

    destroy() {
      windowRef.removeEventListener('message', handleMessage);
    },

    async markReady() {
      initialized = true;
      post({ type: 'ready' });
      await flushQueuedOpenFiles();
    },

    emitDirtyState(dirty) {
      if (!hostMode || lastDirty === dirty) {
        return;
      }
      lastDirty = dirty;
      post({ type: 'dirty-state', dirty });
    },

    async saveFile({ name, result }) {
      if (!hostMode) {
        return false;
      }
      const buffer = result.buffer;
      post({
        type: 'save-file',
        name,
        buffer,
        byteOffset: result.byteOffset ?? 0,
        byteLength: result.byteLength ?? buffer.byteLength,
      }, [buffer]);
      return true;
    },

    isHostMode() {
      return hostMode;
    },
  };
}

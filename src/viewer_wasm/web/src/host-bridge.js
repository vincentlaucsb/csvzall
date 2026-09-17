export const HOST_SOURCE = 'obsidian-csvzall';
export const VIEWER_SOURCE = 'csvzall-wasm-viewer';
export const HOST_CAPABILITIES = Object.freeze([
  'csvzall-host-integration-v1',
  'csvzall-save-ack-v1',
  'csvzall-edit-revision-v1',
  'csvzall-obsidian-keyboard-lifecycle-v2',
  'csvzall-obsidian-viewport-resize-v2',
]);

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
  onViewportResize = () => {},
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
  const pendingSaves = new Map();
  let nextSaveId = 0;

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
    if (!targetWindow || event.source !== targetWindow || event.data?.source !== HOST_SOURCE) {
      return;
    }
    const data = event.data;
    if (data.type === 'save-result') {
      const pending = pendingSaves.get(data.requestId);
      if (pending && typeof data.success === 'boolean') {
        pendingSaves.delete(data.requestId);
        if (data.success) pending.resolve(true);
        else pending.reject(new Error(data.error || 'Save failed'));
      }
      return;
    }
    if (data.type === 'viewport-resized') {
      if (hostMode) onViewportResize();
      return;
    }
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
      for (const pending of pendingSaves.values()) {
        pending.reject(new Error('Host bridge closed before save completed'));
      }
      pendingSaves.clear();
    },

    async markReady() {
      initialized = true;
      post({ type: 'ready', capabilities: HOST_CAPABILITIES });
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
      return new Promise((resolve, reject) => {
        const requestId = ++nextSaveId;
        pendingSaves.set(requestId, { resolve, reject });
        try {
          post({
            type: 'save-file', requestId, name, buffer,
            byteOffset: result.byteOffset ?? 0,
            byteLength: result.byteLength ?? buffer.byteLength,
          }, [buffer]);
        } catch (error) {
          pendingSaves.delete(requestId);
          reject(error);
        }
      });
    },

    isHostMode() {
      return hostMode;
    },
  };
}

function defaultParentWindow() {
  if (typeof window === 'undefined' || window.parent === window) {
    return null;
  }
  return window.parent;
}

export function dirtyStateMessage(dirty) {
  return {
    source: 'csvzall-viewer',
    type: 'dirty-state',
    dirty: dirty === true,
  };
}

export function postDirtyState(dirty, targetWindow = defaultParentWindow(), targetOrigin = '*') {
  if (!targetWindow || typeof targetWindow.postMessage !== 'function') {
    return false;
  }
  targetWindow.postMessage(dirtyStateMessage(dirty), targetOrigin);
  return true;
}

export function createDirtyStateEmitter(viewState, targetWindow = defaultParentWindow(), targetOrigin = '*') {
  let lastDirty;
  return () => {
    const dirty = viewState.dirty;
    if (dirty === lastDirty) {
      return false;
    }
    lastDirty = dirty;
    return postDirtyState(dirty, targetWindow, targetOrigin);
  };
}

export function handleUnsavedBeforeUnload(viewState, event) {
  if (!viewState.dirty) {
    return undefined;
  }
  event.preventDefault();
  event.returnValue = '';
  return '';
}

export function installUnsavedChangesBeforeUnload(viewState, win = window) {
  const listener = (event) => handleUnsavedBeforeUnload(viewState, event);
  win.addEventListener('beforeunload', listener);
  return () => win.removeEventListener('beforeunload', listener);
}

export const DROP_ONE_FILE_MESSAGE =
  'Drag and drop one file at a time. Only files ending in .csv or .tsv are supported.';
export const DROP_EXTENSION_MESSAGE =
  'Drag and drop supports only files ending in .csv or .tsv.';

export function isSupportedDroppedFileName(name) {
  return /\.(csv|tsv)$/i.test(String(name || ''));
}

export function validateDroppedFiles(files) {
  const droppedFiles = Array.from(files ?? []);
  if (droppedFiles.length !== 1) {
    return { error: DROP_ONE_FILE_MESSAGE };
  }
  if (!isSupportedDroppedFileName(droppedFiles[0].name)) {
    return { error: DROP_EXTENSION_MESSAGE };
  }
  return { file: droppedFiles[0] };
}

function isFileDrag(event) {
  return Array.from(event.dataTransfer?.types ?? []).includes('Files');
}

export function setupFileDrop({
  target = document,
  overlay,
  enabled = () => true,
  onFile,
  onStatus,
}) {
  let dragDepth = 0;

  function setOverlayVisible(visible) {
    overlay.hidden = !visible;
    overlay.setAttribute('aria-hidden', String(!visible));
  }

  function onDragEnter(event) {
    if (!isFileDrag(event)) {
      return;
    }
    event.preventDefault();
    if (!enabled()) {
      return;
    }
    dragDepth += 1;
    setOverlayVisible(true);
  }

  function onDragOver(event) {
    if (!isFileDrag(event)) {
      return;
    }
    event.preventDefault();
    if (enabled() && event.dataTransfer) {
      event.dataTransfer.dropEffect = 'copy';
    }
  }

  function onDragLeave(event) {
    if (!isFileDrag(event) || !enabled()) {
      return;
    }
    dragDepth = Math.max(0, dragDepth - 1);
    if (dragDepth === 0) {
      setOverlayVisible(false);
    }
  }

  async function onDrop(event) {
    if (!isFileDrag(event)) {
      return;
    }
    event.preventDefault();
    dragDepth = 0;
    setOverlayVisible(false);
    if (!enabled()) {
      return;
    }

    const result = validateDroppedFiles(event.dataTransfer?.files);
    if (result.error) {
      onStatus(result.error);
      return;
    }
    try {
      await onFile(result.file);
    } catch (error) {
      onStatus(error instanceof Error ? error.message : 'Could not open the dropped file.');
    }
  }

  target.addEventListener('dragenter', onDragEnter);
  target.addEventListener('dragover', onDragOver);
  target.addEventListener('dragleave', onDragLeave);
  target.addEventListener('drop', onDrop);

  return () => {
    target.removeEventListener('dragenter', onDragEnter);
    target.removeEventListener('dragover', onDragOver);
    target.removeEventListener('dragleave', onDragLeave);
    target.removeEventListener('drop', onDrop);
  };
}

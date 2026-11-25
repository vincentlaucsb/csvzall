/** Use the browser's modal focus and Escape handling; treat backdrop clicks like Cancel. */
export function createDialog(dialog, { dismissible = true } = {}) {
  let pressedOutside = false;
  const outside = event => {
    const rect = dialog.getBoundingClientRect();
    return event.target === dialog && (event.clientX < rect.left || event.clientX > rect.right
      || event.clientY < rect.top || event.clientY > rect.bottom);
  };
  const reset = () => { pressedOutside = false; };
  const onPointerDown = event => {
    pressedOutside = dialog.open && event.button === 0 && event.isPrimary !== false && outside(event);
  };
  const onClick = event => {
    const dismiss = pressedOutside && dialog.open && event.button === 0 && outside(event);
    reset();
    if (!dismiss) return;
    const cancel = new dialog.ownerDocument.defaultView.Event('cancel', { cancelable: true });
    if (dialog.dispatchEvent(cancel)) close();
  };
  const blockCancel = event => event.preventDefault();
  if (dismissible) {
    dialog.addEventListener('pointerdown', onPointerDown);
    dialog.addEventListener('click', onClick);
    dialog.addEventListener('pointercancel', reset);
    dialog.addEventListener('close', reset);
  } else {
    dialog.addEventListener('cancel', blockCancel);
  }

  function show(focusTarget) {
    if (dialog.open) return;
    reset();
    // Native Escape closes without assigning a new return value.
    dialog.returnValue = 'cancel';
    dialog.showModal();
    focusTarget?.focus();
  }

  function close(value = 'cancel') {
    if (dialog.open) dialog.close(value);
  }

  return {
    get open() { return dialog.open; },
    show,
    showForResult(focusTarget) {
      show(focusTarget);
      return new Promise(resolve => {
        dialog.addEventListener('close', () => resolve(dialog.returnValue), { once: true });
      });
    },
    close,
    destroy() {
      close();
      dialog.removeEventListener('pointerdown', onPointerDown);
      dialog.removeEventListener('click', onClick);
      dialog.removeEventListener('pointercancel', reset);
      dialog.removeEventListener('close', reset);
      dialog.removeEventListener('cancel', blockCancel);
    },
  };
}

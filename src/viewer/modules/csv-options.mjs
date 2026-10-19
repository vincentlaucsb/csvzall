/** Reflect the engine's header safety constraint without changing file data. */
export function updateHeaderToggle(checkbox, message, options, banner = message) {
  const reason = options?.headersDisabledReason ?? '';
  checkbox.checked = !reason && options?.hasHeaders === true;
  checkbox.disabled = Boolean(reason);
  message.textContent = reason;
  banner.hidden = !reason;
}

/** Mobile hosts preserve the dialect automatically without exposing controls. */
export function applyCsvOptionsHostUi(button, dialog, hosted, hostUi = {}) {
  button.hidden = hosted && hostUi.mobile === true;
  if (button.hidden && dialog.open) dialog.close('cancel');
}

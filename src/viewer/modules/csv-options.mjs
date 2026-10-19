/** Reflect the engine's header safety constraint without changing file data. */
export function updateHeaderToggle(checkbox, message, options) {
  const reason = options?.headersDisabledReason ?? '';
  checkbox.checked = !reason && options?.hasHeaders === true;
  checkbox.disabled = Boolean(reason);
  message.textContent = reason;
  message.hidden = !reason;
}

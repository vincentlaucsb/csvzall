/** Reflect the engine's header safety constraint without changing file data. */
export function updateHeaderToggle(checkbox, message, options, banner = message) {
  const reason = options?.headersDisabledReason ?? '';
  checkbox.checked = !reason && options?.hasHeaders === true;
  checkbox.disabled = Boolean(reason);
  message.textContent = reason;
  banner.hidden = !reason;
}

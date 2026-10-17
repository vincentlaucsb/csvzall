// csvzall-compact-layout-v1: measure actions without moving the focused editor.
export const COMPACT_BREAKPOINT = 600;

export function compactViewport(width, height) {
  return width <= height ? width <= COMPACT_BREAKPOINT : height <= COMPACT_BREAKPOINT;
}

export function createCompactLayout({ windowRef = window, toolbar, moreButton, createMenu, dropdowns, onChange = () => {} }) {
  const doc = toolbar.ownerDocument;
  let compact = false, editing = false, disposed = false, pending = 0, settleTimer = 0;
  let overflow = [];
  const available = button => !button.hidden && !button.closest('[hidden], [aria-hidden="true"]');
  const buttons = () => [...toolbar.querySelectorAll('.feature-toolbar > button, .edit-toolbar > button')].filter(available);
  const label = button => button.getAttribute('aria-label') || button.textContent.trim();
  const menu = createMenu(moreButton, () => overflow.filter(available).map(button => {
    const dropdown = dropdowns.get(button);
    if (dropdown) return { type: 'submenu', id: button.id, label: label(button), disabled: button.disabled,
      items: (typeof dropdown.items === 'function' ? dropdown.items({}) : dropdown.items).map(item => item.type === 'separator' ? item : {
        ...item, onSelect(event) { event.close(); if (!button.disabled) dropdown.onSelect?.(event); },
      }) };
    return { id: button.id, label: label(button), disabled: button.disabled,
      onSelect(event) { event.close(); if (!button.disabled) button.click(); } };
  }));

  function update() {
    pending = 0;
    if (disposed || editing || [...dropdowns.values()].some(dropdown => dropdown.menu?.isOpen)) return;
    compact = compactViewport(windowRef.innerWidth, windowRef.innerHeight);
    doc.body.toggleAttribute('data-compact', compact);
    for (const button of toolbar.querySelectorAll('[data-overflowed]')) button.removeAttribute('data-overflowed');
    overflow = [];
    moreButton.hidden = true;
    if (compact) {
      const candidates = buttons().filter(button => button.id !== 'save');
      // Reserve Save, then keep the earliest actions that fit. Overflow remains
      // in its original DOM position and is invoked through the same handlers.
      const fits = () => [...toolbar.children].filter(node => !node.hidden && node.getBoundingClientRect().width > 0)
        .reduce((total, node) => total + node.getBoundingClientRect().width, 0) +
        Math.max(0, [...toolbar.children].filter(node => !node.hidden && node.getBoundingClientRect().width > 0).length - 1) *
          (parseFloat(windowRef.getComputedStyle(toolbar).columnGap) || 0) <= toolbar.clientWidth + .5;
      if (!fits()) {
        moreButton.hidden = false;
        for (const button of candidates.reverse()) {
          button.setAttribute('data-overflowed', '');
          overflow.unshift(button);
          if (fits()) break;
        }
      }
    }
    onChange(compact);
  }
  function schedule() {
    if (!disposed && !pending) pending = windowRef.requestAnimationFrame(update);
  }
  menu.update({ onClose: schedule, onOpen({ root }) {
    // In a narrow pane a child menu can overlap its parent. Hover-open would
    // put a child action under the pointer before the original click/tap lands.
    // Overflow submenus require explicit click/tap or keyboard activation.
    const hover = event => event.stopImmediatePropagation();
    root.addEventListener('pointerenter', hover, true);
    root.addEventListener('pointermove', hover, true);
  } });
  const resize = windowRef.ResizeObserver ? new windowRef.ResizeObserver(schedule) : null;
  resize?.observe(toolbar);
  const changes = new windowRef.MutationObserver(records => {
    if (records.some(record => record.target !== moreButton)) schedule();
  });
  changes.observe(toolbar, { subtree: true, childList: true, attributes: true, attributeFilter: ['hidden', 'disabled', 'aria-hidden', 'aria-label'] });
  if (doc.documentElement) changes.observe(doc.documentElement, { attributes: true, attributeFilter: ['style', 'class'] });
  doc.fonts?.addEventListener('loadingdone', schedule);
  windowRef.addEventListener('resize', schedule);
  update();
  return {
    get compact() { return compact; },
    refresh: schedule,
    onCellEditingStarted() {
      editing = true;
      windowRef.clearTimeout(settleTimer);
    },
    onCellEditingStopped() {
      // Let the keyboard and any newly focused menus settle before a reflow.
      settleTimer = windowRef.setTimeout(() => { editing = false; schedule(); }, 350);
    },
    destroy() {
      disposed = true;
      resize?.disconnect(); changes.disconnect();
      doc.fonts?.removeEventListener('loadingdone', schedule);
      windowRef.removeEventListener('resize', schedule);
      windowRef.cancelAnimationFrame(pending); windowRef.clearTimeout(settleTimer);
      menu.destroy();
    },
  };
}

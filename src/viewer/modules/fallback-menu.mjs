export function createFallbackContextMenu(options) {
  let root = null;
  let currentInput = {};

  const close = () => {
    if (!root) {
      return;
    }
    root.remove();
    root = null;
    document.removeEventListener('pointerdown', handlePointerDown, true);
    document.removeEventListener('keydown', handleKeyDown, true);
  };

  const handlePointerDown = (event) => {
    if (root && !root.contains(event.target)) {
      close();
    }
  };

  const handleKeyDown = (event) => {
    if (event.key === 'Escape') {
      close();
    }
  };

  const open = (input = {}) => {
    close();
    currentInput = input;
    const items = typeof options.items === 'function'
      ? options.items({ data: input.context, target: input.target, triggerEvent: input.triggerEvent })
      : options.items;
    const visibleItems = (items || []).filter((item) => !item.hidden);
    if (visibleItems.length === 0) {
      return;
    }

    root = document.createElement('div');
    root.setAttribute('data-popright-menu', '');
    root.setAttribute('role', 'menu');
    root.toggleAttribute('data-popright-has-icons', visibleItems.some((item) => item.icon));
    root.tabIndex = -1;
    root.style.position = 'fixed';
    root.style.left = `${input.x ?? 0}px`;
    root.style.top = `${input.y ?? 0}px`;

    visibleItems.forEach((item) => {
      if (item.type === 'separator') {
        const separator = document.createElement('div');
        separator.setAttribute('data-popright-separator', '');
        root.append(separator);
        return;
      }

      const button = document.createElement('button');
      button.type = 'button';
      button.setAttribute('data-popright-item', '');
      button.setAttribute('role', 'menuitem');
      if (item.icon) {
        const icon = document.createElement('span');
        icon.setAttribute('data-popright-icon', '');
        icon.append(typeof item.icon === 'function' ? item.icon({ item, context: currentInput.context }) : item.icon);
        button.append(icon);
      }
      const label = document.createElement('span');
      label.setAttribute('data-popright-label-text', '');
      label.textContent = item.label ?? item.id ?? '';
      button.append(label);
      if (item.variant) {
        button.dataset.variant = item.variant;
      }
      if (item.disabled) {
        button.disabled = true;
        button.setAttribute('data-disabled', '');
      }
      button.addEventListener('pointerenter', () => {
        root.querySelectorAll('[data-active]').forEach((active) => active.removeAttribute('data-active'));
        button.setAttribute('data-active', '');
      });
      button.addEventListener('click', (event) => {
        const selectEvent = {
          id: item.id,
          item,
          nativeEvent: event,
          context: {
            data: currentInput.context,
            target: currentInput.target,
            triggerEvent: currentInput.triggerEvent,
            x: currentInput.x,
            y: currentInput.y,
          },
          close,
          preventClose() {},
        };
        item.onSelect?.(selectEvent);
        options.onSelect?.(selectEvent);
        close();
      });
      root.append(button);
    });

    document.body.append(root);
    const rect = root.getBoundingClientRect();
    const left = Math.min(input.x ?? 0, Math.max(0, window.innerWidth - rect.width - 4));
    const top = Math.min(input.y ?? 0, Math.max(0, window.innerHeight - rect.height - 4));
    root.style.left = `${left}px`;
    root.style.top = `${top}px`;
    root.focus({ preventScroll: true });
    document.addEventListener('pointerdown', handlePointerDown, true);
    document.addEventListener('keydown', handleKeyDown, true);
  };

  return {
    open,
    close,
    update(nextOptions) {
      Object.assign(options, nextOptions);
    },
    destroy: close,
    get isOpen() {
      return root !== null;
    },
    get root() {
      return root;
    },
  };
}

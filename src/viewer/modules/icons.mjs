const TABLER_ICON_PATHS = {
  'arrow-down': ['M12 5l0 14', 'M18 13l-6 6', 'M6 13l6 6'],
  'arrow-up': ['M12 5l0 14', 'M18 11l-6 -6', 'M6 11l6 -6'],
  'chart-bar': ['M3 12m0 1a1 1 0 0 1 1 -1h4a1 1 0 0 1 1 1v6a1 1 0 0 1 -1 1h-4a1 1 0 0 1 -1 -1z', 'M9 8m0 1a1 1 0 0 1 1 -1h4a1 1 0 0 1 1 1v10a1 1 0 0 1 -1 1h-4a1 1 0 0 1 -1 -1z', 'M15 4m0 1a1 1 0 0 1 1 -1h4a1 1 0 0 1 1 1v14a1 1 0 0 1 -1 1h-4a1 1 0 0 1 -1 -1z', 'M4 20l14 0'],
  'chevron-down': ['M6 9l6 6l6 -6'],
  'column-insert-right': ['M4 6a2 2 0 0 1 2 -2h4a2 2 0 0 1 2 2v12a2 2 0 0 1 -2 2h-4a2 2 0 0 1 -2 -2z', 'M16 12h6', 'M19 9v6'],
  'column-remove': ['M4 6a2 2 0 0 1 2 -2h4a2 2 0 0 1 2 2v12a2 2 0 0 1 -2 2h-4a2 2 0 0 1 -2 -2z', 'M16 12h6'],
  'copy': ['M8 8m0 2a2 2 0 0 1 2 -2h8a2 2 0 0 1 2 2v8a2 2 0 0 1 -2 2h-8a2 2 0 0 1 -2 -2z', 'M16 8v-2a2 2 0 0 0 -2 -2h-8a2 2 0 0 0 -2 2v8a2 2 0 0 0 2 2h2'],
  'device-floppy': ['M6 4h10l4 4v10a2 2 0 0 1 -2 2h-12a2 2 0 0 1 -2 -2v-12a2 2 0 0 1 2 -2', 'M12 14m-2 0a2 2 0 1 0 4 0a2 2 0 1 0 -4 0', 'M14 4l0 4l-6 0l0 -4'],
  'pencil': ['M4 20h4l10.5 -10.5a2.828 2.828 0 1 0 -4 -4l-10.5 10.5v4', 'M13.5 6.5l4 4'],
  'player-play': ['M7 4v16l13 -8z'],
  'plus': ['M12 5l0 14', 'M5 12l14 0'],
  'restore': ['M3.06 13a9 9 0 1 0 3.59 -7.36', 'M3 4v6h6'],
  'row-insert-bottom': ['M4 6a2 2 0 0 1 2 -2h12a2 2 0 0 1 2 2v4a2 2 0 0 1 -2 2h-12a2 2 0 0 1 -2 -2z', 'M12 15v6', 'M9 18h6'],
  'row-remove': ['M4 6a2 2 0 0 1 2 -2h12a2 2 0 0 1 2 2v4a2 2 0 0 1 -2 2h-12a2 2 0 0 1 -2 -2z', 'M9 18h6'],
  'trash': ['M4 7h16', 'M10 11v6', 'M14 11v6', 'M5 7l1 12a2 2 0 0 0 2 2h8a2 2 0 0 0 2 -2l1 -12', 'M9 7v-3h6v3'],
  'x': ['M18 6l-12 12', 'M6 6l12 12'],
};

export function createTablerIcon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('fill', 'none');
  svg.setAttribute('stroke', 'currentColor');
  svg.setAttribute('stroke-width', '2');
  svg.setAttribute('stroke-linecap', 'round');
  svg.setAttribute('stroke-linejoin', 'round');
  svg.setAttribute('aria-hidden', 'true');
  svg.classList.add('csvzall-icon');
  (TABLER_ICON_PATHS[name] || []).forEach((d) => {
    const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    path.setAttribute('d', d);
    svg.append(path);
  });
  return svg;
}

function actionContent(iconName, label) {
  const wrapper = document.createElement('span');
  wrapper.className = 'action-content';
  if (iconName) {
    wrapper.append(createTablerIcon(iconName));
  }
  const text = document.createElement('span');
  text.textContent = label;
  wrapper.append(text);
  return wrapper;
}

export function decorateButton(button, iconName, label = button.textContent.trim()) {
  button.replaceChildren(actionContent(iconName, label));
  button.setAttribute('aria-label', label);
  button.title = label;
}

export function decorateIconButton(button, iconName, label) {
  button.replaceChildren(createTablerIcon(iconName));
  button.setAttribute('aria-label', label);
  button.title = label;
}

export function decorateMenuButton(button, iconName, label = button.textContent.trim()) {
  const content = actionContent(iconName, label);
  const chevron = createTablerIcon('chevron-down');
  chevron.classList.add('dropdown-chevron');
  content.append(chevron);
  button.replaceChildren(content);
  button.setAttribute('aria-label', label);
  button.setAttribute('aria-haspopup', 'menu');
  button.title = label;
}

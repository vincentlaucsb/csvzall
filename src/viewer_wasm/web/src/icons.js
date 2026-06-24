const paths = {
  'arrow-up': ['M12 19V5', 'M5 12l7-7 7 7'],
  'arrow-down': ['M12 5v14', 'M19 12l-7 7-7-7'],
  'chevron-down': ['M6 9l6 6 6-6'],
  'column-insert-right': ['M5 6h6v12H5z', 'M15 12h6', 'M18 9v6'],
  'column-remove': ['M5 6h6v12H5z', 'M15 12h6'],
  'pencil': ['M4 20h4l10.5-10.5a2.1 2.1 0 0 0-3-3L5 17v3z', 'M13.5 6.5l4 4'],
  'row-insert-bottom': ['M4 5h16', 'M4 10h16', 'M4 15h8', 'M17 14v6', 'M14 17h6'],
  'row-remove': ['M4 5h16', 'M4 10h16', 'M4 15h8', 'M14 17h6'],
  trash: ['M4 7h16', 'M10 11v6', 'M14 11v6', 'M6 7l1 14h10l1-14', 'M9 7V4h6v3'],
};

export function createIcon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('width', '16');
  svg.setAttribute('height', '16');
  svg.setAttribute('fill', 'none');
  svg.setAttribute('stroke', 'currentColor');
  svg.setAttribute('stroke-width', '2');
  svg.setAttribute('stroke-linecap', 'round');
  svg.setAttribute('stroke-linejoin', 'round');
  svg.setAttribute('aria-hidden', 'true');
  for (const d of paths[name] ?? []) {
    const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    path.setAttribute('d', d);
    svg.append(path);
  }
  return svg;
}

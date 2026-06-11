function markdownCell(value) {
  return String(value ?? '')
    .replace(/\|/g, '\\|')
    .replace(/\r?\n/g, '<br>');
}

export function rowsToMarkdownTable(columns, rows) {
  const safeColumns = Array.isArray(columns) ? columns : [];
  const safeRows = Array.isArray(rows) ? rows : [];
  const header = `| ${safeColumns.map(markdownCell).join(' | ')} |`;
  const divider = `| ${safeColumns.map(() => '---').join(' | ')} |`;
  const body = safeRows.map((row) => {
    const values = safeColumns.map((column) => markdownCell(row?.[column] ?? ''));
    return `| ${values.join(' | ')} |`;
  });
  return [header, divider, ...body].join('\n');
}

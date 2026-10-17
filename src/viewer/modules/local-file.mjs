export async function readLocalFile(file, { onStart = () => {} } = {}) {
  const name = file.name || 'input.csv';
  onStart(name);
  return {
    name,
    buffer: await file.arrayBuffer(),
  };
}

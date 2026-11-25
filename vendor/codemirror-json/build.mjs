import { build } from 'esbuild';
import { readFileSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
const options = { entryPoints: ['editor.mjs'], bundle: true, format: 'esm', minify: true, target: 'es2020' };
const graph = await build({ ...options, write: false, metafile: true });
const notices = new Set();
for (const path of Object.keys(graph.metafile.inputs).filter(path => path.startsWith('node_modules/'))) {
  let dir = dirname(path);
  while (!existsSync(join(dir, 'package.json'))) dir = dirname(dir);
  const name = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8')).name;
  const license = ['LICENSE', 'LICENSE.txt', 'LICENSE-MIT', 'LICENSE.md'].find(file => existsSync(join(dir, file)));
  if (!license) throw new Error('Missing license for ' + name);
  notices.add(name + '\n' + readFileSync(join(dir, license), 'utf8').trim());
}
await build({ ...options, banner: { js: '/*!\n' + [...notices].sort().join('\n\n') + '\n*/' },
  outfile: '../../src/viewer/modules/codemirror-json.mjs' });

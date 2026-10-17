import { fileURLToPath } from 'node:url';

// Obsidian desktop does not serve executable vault resources. The embedded
// build includes its worker and WASM bytes in the entry bundle, which a host
// can load directly from the installed plugin. Standalone releases stay split.
export function embeddedViewerConfig(mode) {
  if (mode !== 'embedded') return {};
  return {
    // Electron can expose process inside browser workers. This is a web-only
    // WASM build, so remove Node detection at source compilation time.
    define: { process: 'undefined', 'globalThis.process': 'undefined' },
    resolve: {
      alias: [{
        find: /^\.\/csv-worker\.js\?worker$/,
        replacement: fileURLToPath(new URL('../src/csv-worker.js', import.meta.url)) + '?worker&inline',
      }],
    },
  };
}

import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
import { pwaPlugin } from './scripts/pwa-plugin.mjs';
import { embeddedViewerConfig } from './scripts/embedded-viewer.mjs';

const publicDir = fileURLToPath(new URL('./public', import.meta.url));

export default defineConfig(({ mode }) => ({
  base: './',
  ...embeddedViewerConfig(mode),
  plugins: [pwaPlugin({ publicDir }), ...(mode === 'embedded' ? [{
    name: 'csvzall-embedded-assets',
    transformIndexHtml: () => [{ tag: 'meta', attrs: { name: 'csvzall-embedded-assets', content: 'v1' } }],
  }] : [])],
  build: {
    outDir: mode === 'embedded' ? 'dist-embedded' : 'dist',
    assetsInlineLimit: mode === 'embedded' ? Number.MAX_SAFE_INTEGER : 4096,
    emptyOutDir: true,
  },
  server: {
    port: 5174,
    strictPort: false,
  },
}));

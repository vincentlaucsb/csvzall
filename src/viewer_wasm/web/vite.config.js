import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
import { pwaPlugin } from './scripts/pwa-plugin.mjs';

const publicDir = fileURLToPath(new URL('./public', import.meta.url));

export default defineConfig({
  base: './',
  plugins: [pwaPlugin({ publicDir })],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
  server: {
    port: 5174,
    strictPort: false,
  },
});

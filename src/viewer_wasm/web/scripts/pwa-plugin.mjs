import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const publicAssets = [
  'manifest.webmanifest',
  'icon-192.png',
  'icon-512.png',
];

function bundleContent(output) {
  if (output.type === 'chunk') {
    return output.code;
  }
  return typeof output.source === 'string' ? output.source : Buffer.from(output.source);
}

function serviceWorkerSource(cacheName, precacheUrls) {
  return `const CACHE_NAME = ${JSON.stringify(cacheName)};
const PRECACHE_URLS = ${JSON.stringify(precacheUrls, null, 2)};

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then((cache) => cache.addAll(PRECACHE_URLS))
      .then(() => self.skipWaiting()),
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys()
      .then((names) => Promise.all(
        names.filter((name) => name.startsWith('csvzall-viewer-') && name !== CACHE_NAME)
          .map((name) => caches.delete(name)),
      ))
      .then(() => self.clients.claim()),
  );
});

async function navigationResponse(request) {
  try {
    const response = await fetch(request);
    if (response.ok) {
      const cache = await caches.open(CACHE_NAME);
      await cache.put(request, response.clone());
    }
    return response;
  } catch {
    return (await caches.match(request))
      || (await caches.match('./index.html'))
      || (await caches.match('./'));
  }
}

async function assetResponse(request) {
  const cached = await caches.match(request);
  if (cached) {
    return cached;
  }
  const response = await fetch(request);
  if (response.ok) {
    const cache = await caches.open(CACHE_NAME);
    await cache.put(request, response.clone());
  }
  return response;
}

self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url);
  if (event.request.method !== 'GET' || url.origin !== self.location.origin) {
    return;
  }
  event.respondWith(
    event.request.mode === 'navigate'
      ? navigationResponse(event.request)
      : assetResponse(event.request),
  );
});
`;
}

export function pwaPlugin({ publicDir }) {
  return {
    name: 'csvzall-pwa',
    apply: 'build',
    generateBundle(_outputOptions, bundle) {
      const hash = createHash('sha256');
      const bundleFiles = Object.values(bundle).sort((left, right) =>
        left.fileName.localeCompare(right.fileName));

      for (const output of bundleFiles) {
        hash.update(output.fileName);
        hash.update(bundleContent(output));
      }
      for (const asset of publicAssets) {
        hash.update(asset);
        hash.update(readFileSync(resolve(publicDir, asset)));
      }

      const precacheUrls = [
        './',
        './index.html',
        ...publicAssets.map((asset) => `./${asset}`),
        ...bundleFiles.map((output) => `./${output.fileName}`),
      ];
      const cacheName = `csvzall-viewer-${hash.digest('hex').slice(0, 12)}`;
      this.emitFile({
        type: 'asset',
        fileName: 'sw.js',
        source: serviceWorkerSource(cacheName, [...new Set(precacheUrls)]),
      });
    },
  };
}

// The host rewrites relative HTML references to plugin-owned Blob URLs. Let
// every iframe construct its own worker from the same hosted script URL.
const workerAsset = /^assets\/csv-worker-[\w-]+\.js$/;

export function embeddedViewerConfig(mode) {
  if (mode !== 'embedded') return {};
  return {
    // Electron can expose process inside browser workers. This is a web-only
    // WASM build, so remove Node detection at source compilation time.
    define: { process: 'undefined', 'globalThis.process': 'undefined' },
    experimental: {
      renderBuiltUrl(filename, { hostType }) {
        if (hostType === 'js' && workerAsset.test(filename)) {
          return { runtime: 'document.querySelector(\'link[rel="csvzall-worker"]\').href' };
        }
      },
    },
  };
}

export function embeddedAssetTags(_html, { bundle }) {
  const worker = Object.keys(bundle).find(name => workerAsset.test(name));
  if (!worker) throw new Error('Missing embedded CSV worker asset');
  return [
    { tag: 'meta', attrs: { name: 'csvzall-embedded-assets', content: 'v2' } },
    { tag: 'link', attrs: { rel: 'csvzall-worker', href: './' + worker } },
  ];
}

// Focused real-engine check, independent of the broader edit/focus smoke suite.
const assert = require('node:assert/strict');
const path = require('node:path');
const { mkdirSync } = require('node:fs');
const { createRequire } = require('node:module');
const requireWeb = createRequire(path.resolve(__dirname, '../src/viewer_wasm/web/package.json'));
const { chromium } = process.env.CSVZALL_PLAYWRIGHT_MODULE ? require(process.env.CSVZALL_PLAYWRIGHT_MODULE) : requireWeb('playwright');
const options = Object.fromEntries(process.argv.slice(2).map(arg => {
  const match = /^--(wasm-url|viewer-path|browser|output-dir)=(.+)$/.exec(arg);
  if (!match) throw new Error('Expected --wasm-url, --viewer-path, --browser, or --output-dir.');
  return [match[1], match[2]];
}));
const base = (options['wasm-url'] || 'http://127.0.0.1:5174').replace(/\/$/, '');
const viewerPath = options['viewer-path'] || '/index.html';
const output = path.resolve(options['output-dir'] || 'out/build/mobile-csv-options-smoke');
const csv = 'a\tb\n1\t2\n3\t4\tEXTRA\n5\t6\n';
mkdirSync(output, { recursive: true });

(async () => {
  const browser = await chromium.launch({ headless: true, ...(options.browser ? { channel: options.browser } : {}) });
  const errors = [];
  try {
    const page = await browser.newPage({ viewport: { width: 320, height: 740 }, hasTouch: true });
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(base + '/host-harness.html');
    await page.locator('#viewer').evaluate((frame, source) => new Promise(resolve => {
      frame.onload = resolve; frame.src = source;
    }), viewerPath);
    const frame = page.frameLocator('#viewer');
    await frame.locator('#csv-options').waitFor({ state: 'attached' });
    const open = mobile => page.evaluate(({ csv, mobile }) => {
      const buffer = new TextEncoder().encode(csv).buffer;
      document.getElementById('viewer').contentWindow.postMessage({ source: 'obsidian-csvzall', type: 'open-file',
        name: 'uneven.tsv', buffer, hostUi: { version: 1, mobile } }, '*', [buffer]);
    }, { csv, mobile });

    await open(true);
    await frame.getByRole('gridcell', { name: 'EXTRA', exact: true }).waitFor();
    assert.match(await frame.locator('#record-count').textContent(), /^4 rows, 3 columns$/);
    assert.equal(await frame.locator('.ag-center-cols-container .ag-row').count(), 4);
    await frame.getByRole('gridcell', { name: 'a', exact: true }).waitFor();
    await frame.getByRole('gridcell', { name: '5', exact: true }).waitFor();
    assert.equal(await frame.locator('#csv-options').evaluate(button => button.hidden), true);
    assert.equal(await frame.locator('#csv-options').isVisible(), false);
    assert.equal(await frame.locator('#csv-options + .toolbar-divider').isVisible(), false);
    await frame.locator('#csv-options').evaluate(button => button.click());
    assert.equal(await frame.locator('#csv-options-dialog').evaluate(dialog => dialog.open), false);
    await page.setViewportSize({ width: 200, height: 740 });
    await frame.locator('#more-actions').waitFor({ state: 'visible' });
    await frame.locator('#more-actions').click();
    assert.equal(await frame.getByRole('menuitem', { name: 'Options', exact: true }).count(), 0);
    await frame.locator('#record-count').click();
    await page.setViewportSize({ width: 320, height: 740 });
    await page.screenshot({ path: path.join(output, 'mobile-hidden-options.png') });

    await page.setViewportSize({ width: 1000, height: 900 });
    await open(false);
    const desktopOptions = frame.locator('#csv-options:not([hidden]):not(:disabled)');
    await desktopOptions.waitFor();
    await desktopOptions.click();
    await frame.locator('#csv-options-dialog').waitFor();
    assert.equal(await frame.locator('#csv-options-has-headers').isChecked(), false);
    assert.equal(await frame.locator('#csv-options-has-headers').isDisabled(), true);
    assert.equal(await frame.locator('#csv-options-header-warning').isVisible(), true);
    await frame.locator('#csv-options-cancel').click();

    await page.goto(base + viewerPath);
    await page.locator('#file-input:not(:disabled)').waitFor();
    await page.locator('#file-input').setInputFiles({ name: 'standalone.tsv', mimeType: 'text/tab-separated-values', buffer: Buffer.from(csv) });
    await page.getByRole('gridcell', { name: 'EXTRA', exact: true }).waitFor();
    assert.equal(await page.locator('#csv-options').evaluate(button => button.hidden), false);
    await page.locator('#csv-options').click();
    await page.locator('#csv-options-dialog').waitFor();
    assert.deepEqual(errors, []);
    console.log('Mobile Options: hidden button/divider, no dialog or overflow action, four uneven rows preserved; desktop host and standalone controls retained.');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });

// Optional end-to-end check; Playwright is a development tool, never a viewer dependency.
const path = require('node:path');
const { createRequire } = require('node:module');
const requireWeb = createRequire(path.resolve(__dirname, '../src/viewer_wasm/web/package.json'));
const args = Object.fromEntries(process.argv.slice(2).map(arg => {
  const match = arg.match(/^--(native|wasm-url|browser|output-dir)=(.+)$/);
  if (!match) throw new Error('Usage: node scripts/smoke_shared_viewer.cjs --native=<csvzall executable> [--wasm-url=http://127.0.0.1:5174] [--browser=msedge] [--output-dir=<directory>]');
  return [match[1], match[2]];
}));
if (!args.native) throw new Error('--native must identify the newly built csvzall executable');
const wasmUrl = (args['wasm-url'] || 'http://127.0.0.1:5174').replace(/\/$/, '');
const output = path.resolve(args['output-dir'] || 'out/build/viewer-browser-smoke');
const { chromium } = process.env.CSVZALL_PLAYWRIGHT_MODULE ? require(process.env.CSVZALL_PLAYWRIGHT_MODULE) : requireWeb('playwright');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const assert = require('node:assert/strict');
fs.mkdirSync(output, { recursive: true });
const workspace = fs.mkdtempSync(path.join(output, 'session-'));
const fixture = path.join(workspace, 'smoke-native.csv');
const source = 'id,title\n1,hello\n2,world\n';
const processes = [];
async function nativeUrl(editable) {
  fs.writeFileSync(fixture, source);
  const child = spawn(path.resolve(args.native), ['view',fixture,'--no-open','--startup-json',...(editable?['--edit']:[])],{windowsHide:true});
  processes.push(child);
  return new Promise((resolve,reject)=>{
    let stdout=''; const timeout=setTimeout(()=>reject(new Error('native startup timed out')),15000);
    child.on('error',reject);
    child.stdout.on('data',chunk=>{stdout+=chunk;for(const line of stdout.split('\n')){try{const value=JSON.parse(line);if(value.url){clearTimeout(timeout);resolve(value.url);return;}}catch{}}});
    child.on('exit',code=>{clearTimeout(timeout);reject(new Error('native exited '+code+' '+stdout));});
  });
}
async function edit(page,value) {
  await page.locator('#grid .ag-row[row-index="0"] .ag-cell[col-id="title"]').dblclick();
  await page.locator('#grid .ag-cell-inline-editing input').fill(value);
  await page.keyboard.press('Enter');
  await page.waitForFunction(()=>!document.getElementById('save').disabled);
}
async function commonActions(page) {
  await page.locator('#insert-menu').click();
  await page.getByText('Row After',{exact:true}).click();
  await page.waitForFunction(()=>document.getElementById('record-count').textContent.startsWith('3 rows'));
  await page.locator('#reset').click();
  await page.waitForFunction(()=>document.getElementById('record-count').textContent.startsWith('2 rows'));
  await page.locator('#grid .ag-header-cell[col-id="title"]').click({button:'right'});
  await page.getByText('Rename Column',{exact:true}).click();
  await page.locator('#rename-column-name').fill('description');
  await page.locator('#rename-column-form button[type=submit]').click();
  await page.locator('#grid .ag-header-cell[col-id="description"]').waitFor();
  await page.locator('#reset').click();
  await page.locator('#grid .ag-header-cell[col-id="title"]').waitFor();
  await page.waitForTimeout(200);
}
(async()=>{
 const browser=await chromium.launch({...(args.browser ? {channel:args.browser} : {}),headless:true});
 const errors=[];
 try {
  const page=await browser.newPage({viewport:{width:1200,height:700}});
  page.on('pageerror',error=>errors.push(error.message));
  page.on('console',message=>{ if(message.type()==='error') console.log('Browser console:',message.text()); });
  page.on('response',response=>{if(response.status()>=400)console.log('HTTP failure:',response.status(),response.url());});
  await page.goto(wasmUrl);
  await page.locator('#file-input').setInputFiles({name:'smoke.csv',mimeType:'text/csv',buffer:Buffer.from(source)});
  await page.locator('#grid .ag-cell').filter({hasText:'hello'}).waitFor();
  assert.equal(await page.locator('#open-sql-query,#add-chart').count(),0);
  await commonActions(page);
  await edit(page,'WASM updated');
  const downloaded=page.waitForEvent('download');await page.locator('#save').click();
  const stream=await (await downloaded).createReadStream();let text='';for await(const chunk of stream)text+=chunk;
  assert.match(text,/WASM updated/);
  await page.waitForFunction(()=>document.getElementById('save').disabled);
  await page.screenshot({path:path.join(output,'wasm-shell.png')});
  const wasmSelected = await page.locator('#grid').evaluate(el=>getComputedStyle(el).getPropertyValue('--ag-selected-row-background-color'));
  console.log('WASM: real engine open, insert/reset, rename/reset, edit/download, clean state passed');
  await page.setViewportSize({width:390,height:844});
  await page.goto(wasmUrl + '/host-harness.html');
  await page.locator('#open-file-early').click();
  const hosted = page.frameLocator('#viewer');
  await hosted.locator('#grid .ag-cell').filter({hasText:'Ada'}).waitFor();
  assert.equal(await hosted.locator('#file-tools').isVisible(),false);
  await hosted.locator('#grid .ag-row[row-index="0"] .ag-cell[col-id="name"]').dblclick();
  const editor = hosted.locator('#grid .ag-cell-inline-editing input');
  await editor.fill('Hosted updated');
  const gridBefore = await hosted.locator('#grid').boundingBox();
  const inputHandle = await editor.elementHandle();
  await page.evaluate(()=>{
    const target=document.getElementById('viewer').contentWindow;
    target.postMessage({source:'obsidian-csvzall',type:'theme',version:1,mode:'dark',variables:{'--background-primary':'#223344','--background-secondary':'#192a38','--background-modifier-hover':'#30475b','--background-modifier-border':'#445566','--text-normal':'#eeeeee','--text-muted':'#bfcad6','--interactive-accent':'#ff9900'}},'*');
    target.postMessage({source:'obsidian-csvzall',type:'viewport-resized'},'*');
  });
  await page.waitForTimeout(200);
  assert.equal(await inputHandle.evaluate(el=>el===document.activeElement),true);
  assert.equal(await editor.inputValue(),'Hosted updated');
  assert.deepEqual(await hosted.locator('#grid').boundingBox(),gridBefore);
  assert.ok(gridBefore.x>=0 && gridBefore.width<=390);
  await page.keyboard.press('Enter');
  await hosted.locator('#save').click();
  await page.locator('#saved').filter({hasText:'Last save:'}).waitFor();
  assert.match(await page.evaluate(()=>new TextDecoder().decode(window.csvzallHostHarness.savedBytes())),/Hosted updated/);
  await hosted.locator('#save:disabled').waitFor();
  await hosted.locator('#grid .ag-cell').filter({hasText:'Hosted updated'}).waitFor();
  await page.screenshot({path:path.join(output,'hosted-mobile-shell.png')});
  console.log('Hosted WASM: early file queue, mobile layout, live theme with active editor, stable grid size, acknowledged save passed');
  await page.setViewportSize({width:1200,height:700});
  await page.goto(await nativeUrl(true));
  await page.locator('#grid .ag-cell').filter({hasText:'hello'}).waitFor();
  await page.locator('#open-sql-query').waitFor();await page.locator('#add-chart').waitFor();
  await commonActions(page);
  await edit(page,'native updated');
  await page.locator('#open-sql-query').click();
  await page.locator('#sql-query').fill('SELECT title FROM data WHERE id = 1');
  await page.locator('#run-sql').click();
  await page.locator('#sql-grid .ag-cell').filter({hasText:'native updated'}).waitFor();
  await page.setViewportSize({width:390,height:844});
  await page.locator('body[data-compact]').waitFor();
  assert.equal(await page.locator('.toolbar-actions').evaluate(el=>el.scrollWidth<=el.clientWidth+1),true);
  assert.equal(await page.locator('#edit-toolbar').isVisible(),false);
  await page.setViewportSize({width:1200,height:700});
  await page.locator('body:not([data-compact])').waitFor();
  await page.locator('#exit-sql-query').click();
  await page.locator('#save').click();
  await page.waitForFunction(()=>document.getElementById('save').disabled);
  assert.match(fs.readFileSync(fixture,'utf8'),/native updated/);
  await page.locator('#add-chart').click();
  await page.locator('#heatmap-chart-dialog[open] #chart-id').waitFor();
  await page.locator('#cancel-chart').click();
  await page.screenshot({path:path.join(output,'native-shell.png')});
  assert.equal(await page.locator('#grid').evaluate(el=>getComputedStyle(el).getPropertyValue('--ag-selected-row-background-color')),wasmSelected);
  console.log('Native: shared editing/reset/rename, SQL over unsaved edits, atomic save, optional chart form passed');
  const firstHeader=await page.locator('#grid .ag-header-cell[col-id="id"]').boundingBox();
  const lastHeader=await page.locator('#grid .ag-header-cell[col-id="title"]').boundingBox();
  await page.mouse.move(firstHeader.x+firstHeader.width/2,firstHeader.y+firstHeader.height/2);
  await page.mouse.down();await page.mouse.move(lastHeader.x+lastHeader.width-20,lastHeader.y+lastHeader.height/2,{steps:20});await page.mouse.up();
  await page.waitForFunction(()=>!document.getElementById('save').disabled);
  await page.locator('#save').click();await page.locator('#save:disabled').waitFor();
  assert.match(fs.readFileSync(fixture,'utf8'),/^title,id/);
  console.log('Native column-order save passed without reloading the document');
  await page.goto(await nativeUrl(false));
  await page.locator('#grid .ag-cell').filter({hasText:'hello'}).waitFor();
  assert.equal(await page.locator('#edit-toolbar').isVisible(),false);
  await page.locator('#grid .ag-header-cell[col-id="title"]').click({button:'right'});
  await page.getByText('Query column',{exact:true}).waitFor();
  assert.equal(await page.getByText('Rename Column',{exact:true}).count(),0);
  console.log('Native read-only: edit controls absent; query-only column menu passed');
  assert.deepEqual(errors,[]);
 } catch (error) { for (const p of browser.contexts().flatMap(c=>c.pages())) { console.log(await p.locator('body').innerText()); await p.screenshot({path:path.join(output,'smoke-failure.png')}); } console.log('Page errors:',errors); throw error; }
 finally {await browser.close();for(const child of processes)child.kill();}
})().catch(error=>{console.error(error);process.exitCode=1});

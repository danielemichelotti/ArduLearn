// node cdp.mjs plan.mjs  -> esegue il piano (export default async ({go, js, shot, wait}) => ...)
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const list = await (await fetch('http://127.0.0.1:9333/json')).json();
const page = list.find(t => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(r => ws.addEventListener('open', r));
let id = 0; const pend = new Map();
ws.addEventListener('message', e => { const m = JSON.parse(e.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); } });
const send = (method, params = {}) => new Promise(r => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const wait = ms => new Promise(r => setTimeout(r, ms));
await send('Page.enable');
await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 860, deviceScaleFactor: 2, mobile: false });
const dir = path.join(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1')), 'shots');
const api = {
  wait, send,
  async go(url) { await send('Page.navigate', { url }); await wait(2500); },
  async js(expr) { const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.result?.exceptionDetails) console.log('ERR', JSON.stringify(r.result.exceptionDetails).slice(0, 400)); return r.result?.result?.value; },
  async shot(name, clip) {
    const r = await send('Page.captureScreenshot', { format: 'png', ...(clip ? { clip: { ...clip, scale: 1 } } : {}) });
    fs.writeFileSync(path.join(dir, name + '.png'), Buffer.from(r.result.data, 'base64')); console.log('shot', name);
  },
};
const plan = (await import(pathToFileURL(path.resolve(process.argv[2])).href)).default;
await plan(api);
ws.close();

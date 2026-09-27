// stampa manuale.html in PDF con Chrome (usato da crea_manuale.py)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const QUI = path.dirname(fileURLToPath(import.meta.url));
export default async ({ go, send, wait, js }) => {
  await send('Emulation.clearDeviceMetricsOverride');
  await go(pathToFileURL(path.join(QUI, 'manuale.html')).href); await wait(1500);
  await js('document.fonts.ready');
  const mancanti = await js('[...document.querySelectorAll(".fig")].filter(e=>/MANCANTE/.test(e.textContent)).map(e=>e.dataset.f).join(",")');
  if (mancanti) console.log('figure mancanti:', mancanti);
  const foot = '<div style="width:100%;font-size:8px;color:#6b7280;padding:0 17mm;display:flex;justify-content:space-between;font-family:Segoe UI,Arial"><span>ArduLearn · Manuale del docente</span><span class="pageNumber"></span></div>';
  const r = await send('Page.printToPDF', { printBackground: true, preferCSSPageSize: true, displayHeaderFooter: true, headerTemplate: '<span></span>', footerTemplate: foot });
  fs.writeFileSync(path.join(QUI, '..', 'ArduLearn_manuale_docente.pdf'), Buffer.from(r.result.data, 'base64'));
  console.log('PDF scritto in docs/ArduLearn_manuale_docente.pdf');
};

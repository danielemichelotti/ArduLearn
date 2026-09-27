// Manuale del docente da leggere nel sito: le stesse pagine del PDF (docs/manuale/p1..p5.html)
import fs from "node:fs";

export const data = { permalink: "/manuale/leggi/index.html", eleventyExcludeFromCollections: true };

export function render() {
  const html = [1, 2, 3, 4, 5].map(i => fs.readFileSync(`../docs/manuale/p${i}.html`, "utf-8")).join("");
  const schermo = `<style>
@media screen {
  html { background: #e9edf5; }
  body { max-width: 900px; margin: 0 auto; padding: 70px 34px 40px; background: #fff; box-shadow: 0 0 40px rgba(15,23,42,.12); }
  .cover { width: auto !important; max-width: 100%; height: auto !important; min-height: 1000px; margin: 0 -34px 30px; }
  h1.cap { margin-top: 46px; }
  .barra-sito { position: fixed; top: 0; left: 0; right: 0; z-index: 50; background: #0b1530; color: #c7d2fe; font: 15px "Segoe UI", system-ui, sans-serif; display: flex; gap: 16px; align-items: center; padding: 10px 20px; }
  .barra-sito a { color: #fff; text-decoration: none; } .barra-sito .sp { flex: 1; }
  .barra-sito .pdf { background: #ffd43b; color: #3b2f00; padding: 5px 12px; border-radius: 8px; font-weight: 700; }
}
@media screen and (max-width: 700px) { body { padding: 64px 12px 30px; } .two, .two.w { grid-template-columns: 1fr; } .postit { float: none; width: auto; margin: 10px 0; } }
@media print { .barra-sito { display: none; } }
</style>`;
  const barra = `<div class="barra-sito"><a href="/">← ArduLearn</a><span>Manuale del docente</span><span class="sp"></span><a class="pdf" href="/scarica/ArduLearn_manuale_docente.pdf">Scarica il PDF</a></div>`;
  // niente collegamenti a Google Fonts: il carattere Caveat è sul sito
  const pulito = html.replace(/<link rel="preconnect"[^>]*>\s*/g, "");
  return pulito.replace("</head>", schermo + "</head>").replace("<body>", "<body>" + barra);
}

// Minifica gli script dentro una pagina HTML (usato da build_web.py se Node.js e terser ci sono).
// I nomi globali restano uguali (servono tra uno script e l'altro e negli attributi HTML):
// si accorciano solo le variabili locali e si tolgono spazi e commenti.
//   node minify_js.mjs  <  pagina.html  >  pagina.min.html
import { minify } from 'terser';

const chunks = [];
for await (const c of process.stdin) chunks.push(c);
const html = Buffer.concat(chunks).toString('utf8');
const re = /<script>([\s\S]*?)<\/script>/g;
let out = '', last = 0, m;
while ((m = re.exec(html))) {
  const r = await minify(m[1], { ecma: 2020, module: false, toplevel: false,
    compress: { passes: 2, keep_fargs: true }, mangle: { toplevel: false }, format: { comments: false } });
  out += html.slice(last, m.index) + '<script>' + r.code + '</script>';
  last = m.index + m[0].length;
}
process.stdout.write(out + html.slice(last));

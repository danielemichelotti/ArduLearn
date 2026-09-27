// Registra e monta un video tutorial di ArduLearn.
//   node gira.mjs 01                     registra dal simulatore in web/ e monta
//   node gira.mjs 01 --monta             rimonta soltanto (dopo aver cambiato scritte, cartelli, audio)
//   node gira.mjs 01 --monta --voce voce.wav [--musica musica.mp3]
//   node gira.mjs 05 --pezzo B           registra un solo pezzo di un video fatto di pezzi
//   node gira.mjs 01 --provini           griglia con un fotogramma per scena, per controllare il video
// I file finiti vanno in tools/video/out/<nome-video>/ (cartella esclusa dal repository).
import fs from 'node:fs';
import path from 'node:path';
import { avviaServer } from './lib/server.mjs';
import { registra } from './lib/regia.mjs';
import { monta, provini } from './lib/montaggio.mjs';
import { schedaSito } from './lib/testi.mjs';

const args = process.argv.slice(2);
const opz = n => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : undefined; };
const quale = args.find(a => /^\d+$/.test(a));
const qui = import.meta.dirname;
const file = fs.readdirSync(path.join(qui, 'video')).find(f => quale && f.startsWith(quale.padStart(2, '0') + '-'));
if (!file) {
  console.log('Uso: node gira.mjs <numero> [--monta] [--voce file] [--musica file]\nVideo disponibili:\n  ' + fs.readdirSync(path.join(qui, 'video')).join('\n  '));
  process.exit(1);
}
const video = (await import('./video/' + file)).default;
const cartella = path.join(qui, 'out', video.file);
fs.mkdirSync(cartella, { recursive: true });

// video fatti di pezzi (per esempio con la scheda vera): ogni pezzo della pagina si registra a parte
if (opz('--pezzo')) {
  const nome = opz('--pezzo'), pz = video.pezzi?.[nome];
  if (!pz?.url) { console.log('Pezzi registrabili: ' + Object.keys(video.pezzi || {}).filter(k => video.pezzi[k].url).join(', ')); process.exit(1); }
  const dir = path.join(cartella, nome);
  fs.mkdirSync(dir, { recursive: true });
  console.log(`Registro il pezzo ${nome} da ${pz.url}…`);
  await registra({ url: pz.url, cartella: dir, scene: s => pz.scene(s), finestra: pz.finestra });
  console.log('Pezzo registrato in ' + dir);
  process.exit(0);
}

if (args.includes('--provini')) { console.log(provini(video, cartella)); process.exit(0); }

if (video.pezzi) {
  // video fatti di pezzi: i pezzi si registrano uno a uno con --pezzo, qui si uniscono
  const { uniscePezzi } = await import('./lib/pezzi.mjs');
  console.log('Unisco i pezzi…');
  await uniscePezzi(video, cartella);
} else if (!args.includes('--monta')) {
  console.log(`Registro "${video.titolo}"…`);
  const srv = await avviaServer(path.join(qui, '../../web'));
  try { await registra({ url: srv.url + video.url, cartella, scene: s => video.scene(s) }); }
  finally { srv.chiudi(); }
}

console.log('Monto…');
const { uscita, totale, capitoli } = await monta(video, cartella, { voce: opz('--voce'), musica: opz('--musica') });
fs.writeFileSync(path.join(cartella, `${video.file}-youtube.txt`), `TITOLO\n${video.youtube.titolo}\n\nDESCRIZIONE\n${video.youtube.descrizione.replace('{capitoli}', capitoli)}\n\nTAG\n${video.youtube.tag.join(', ')}\n`);
fs.writeFileSync(path.join(cartella, `${video.file}.md`), schedaSito(video));
console.log(`Fatto: ${uscita}  (${Math.floor(totale / 60)}:${String(Math.floor(totale % 60)).padStart(2, "0")})`);
if (totale > 180) console.warn('ATTENZIONE: il video supera i 3 minuti');

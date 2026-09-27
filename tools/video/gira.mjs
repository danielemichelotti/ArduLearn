// Registra e monta un video tutorial di ArduLearn.
//   node gira.mjs 01                     registra dal simulatore in web/ e monta
//   node gira.mjs 01 --monta             rimonta soltanto (dopo aver cambiato scritte, cartelli, audio)
//   node gira.mjs 01 --monta --voce voce.wav [--musica musica.mp3]
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

if (args.includes('--provini')) { console.log(provini(video, cartella)); process.exit(0); }

if (!args.includes('--monta')) {
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

// Montaggio con ffmpeg: fotogrammi -> video a 30 fps, scritte in sovrimpressione (ASS),
// intro e chiusura col logo, traccia audio (voce e musica facoltative), sottotitoli e copione.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { chromium } from 'playwright';
import { VISTA, SCALA } from './regia.mjs';

const FPS = 30, W = VISTA.width * SCALA, H = VISTA.height * SCALA;
export const DURATA_INTRO = 4, DURATA_CHIUSURA = 5;
const RADICE = path.resolve(import.meta.dirname, '../../..');

// ffmpeg installato con winget per l'utente: se non è nel PATH lo cerca nella cartella dei pacchetti
function trovaFfmpeg() {
  try { execFileSync('ffmpeg', ['-version'], { stdio: 'ignore' }); return 'ffmpeg'; } catch { }
  const base = path.join(process.env.LOCALAPPDATA || '', 'Microsoft/WinGet/Packages');
  for (const d of fs.existsSync(base) ? fs.readdirSync(base) : []) {
    if (!d.startsWith('Gyan.FFmpeg')) continue;
    for (const s of fs.readdirSync(path.join(base, d))) {
      const f = path.join(base, d, s, 'bin', 'ffmpeg.exe');
      if (fs.existsSync(f)) return f;
    }
  }
  throw new Error('ffmpeg non trovato: installalo con  winget install Gyan.FFmpeg --scope user');
}
const FFMPEG = trovaFfmpeg();
const ffmpeg = (args, cwd) => execFileSync(FFMPEG, ['-hide_banner', '-loglevel', 'error', '-y', ...args], { cwd, stdio: 'inherit' });

const tempo = (s, sep = ',') => {
  const ms = Math.max(0, Math.round(s * 1000));
  const h = Math.floor(ms / 3600000), m = Math.floor(ms / 60000) % 60, ss = Math.floor(ms / 1000) % 60;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(ss).padStart(2, '0')}${sep}${String(ms % 1000).padStart(3, '0')}`;
};
const mmss = s => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;

// ---------- elenco dei fotogrammi con la loro durata ----------
function listaFotogrammi(reg, cartella) {
  const rel = reg.fotogrammi.map(f => ({ file: f.file, t: f.t - reg.t0 }));
  let i0 = 0;
  while (i0 + 1 < rel.length && rel[i0 + 1].t <= 0) i0++;
  const usati = rel.slice(i0);
  usati[0].t = 0;
  const fine = reg.tFine - reg.t0;
  let txt = 'ffconcat version 1.0\n';
  usati.forEach((f, i) => {
    const d = (i + 1 < usati.length ? usati[i + 1].t : fine) - f.t;
    txt += `file 'fotogrammi/${f.file}'\nduration ${Math.max(d, 0.001).toFixed(4)}\n`;
  });
  txt += `file 'fotogrammi/${usati.at(-1).file}'\n`;
  fs.writeFileSync(path.join(cartella, 'fotogrammi.ffconcat'), txt);
  return fine;
}

// ---------- scritte in sovrimpressione ----------
function fileAss(scene, cartella) {
  const esc = s => s.replace(/[{}]/g, '').replace(/\n/g, '\\N');
  let txt = `[Script Info]
ScriptType: v4.00+
PlayResX: ${W}
PlayResY: ${H}
WrapStyle: 0

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Scritta,Segoe UI,44,&H00FFFFFF,&H00FFFFFF,&H28201812,&H00000000,1,0,0,0,100,100,0,0,3,18,0,8,0,0,0,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
`;
  // area del foglio: da x=315 a x=1485 (pixel del video); la scritta sta in alto al centro del foglio,
  // oppure più in basso quando in alto c'è qualcosa da leggere (per esempio il banner del simulatore)
  for (const s of scene) if (s.scritta)
    txt += `Dialogue: 0,${tempo(s.inizio, '.').slice(1, -1)},${tempo(s.fine, '.').slice(1, -1)},Scritta,,0,0,0,,{\\an8\\pos(900,${s.basso ? 760 : 248})\\fad(250,250)}${esc(s.scritta)}\n`;
  fs.writeFileSync(path.join(cartella, 'scritte.ass'), txt);
}

// ---------- sottotitoli: la voce spezzata in righe brevi, con tempi proporzionali ----------
// Unisce i pezzi di frase (tagliati a punti e virgole) fino a due righe; va a capo dopo
// un punto se il sottotitolo è già abbastanza lungo, così ogni sottotitolo è una frase compiuta.
function spezza(testo, max = 84) {
  const parti = [];
  for (const c of testo.match(/[^.!?:;,]+[.!?:;,]*/g).map(s => s.trim()).filter(Boolean)) {
    if (c.length <= max) { parti.push(c); continue; }
    const parole = c.split(' '), n = Math.ceil(c.length / max), k = Math.ceil(parole.length / n);
    for (let i = 0; i < parole.length; i += k) parti.push(parole.slice(i, i + k).join(' '));
  }
  const pezzi = [];
  for (const c of parti) {
    const ult = pezzi.at(-1);
    const chiuso = ult && /[.!?]$/.test(ult) && ult.length >= 36;
    if (ult && !chiuso && (ult + ' ' + c).length <= max) pezzi[pezzi.length - 1] += ' ' + c;
    else pezzi.push(c);
  }
  return pezzi;
}
function aCapo(s, max = 42) {
  if (s.length <= max) return s;
  const m = s.length / 2;
  let best = -1;
  for (let i = 0; i < s.length; i++) if (s[i] === ' ' && (best < 0 || Math.abs(i - m) < Math.abs(best - m))) best = i;
  return best < 0 ? s : s.slice(0, best) + '\n' + s.slice(best + 1);
}

function sottotitoli(scene, offset) {
  const voci = [];
  for (const s of scene) {
    if (!s.voce) continue;
    const pezzi = spezza(s.voce);
    const tot = pezzi.reduce((a, p) => a + p.length, 0);
    const parlato = Math.min(s.fine - s.inizio, s.voce.trim().split(/\s+/).length / 2.4 + 0.5);
    let t = s.inizio + 0.3;
    for (const p of pezzi) {
      const d = parlato * p.length / tot;
      voci.push({ da: offset + t, a: offset + t + d, testo: p });
      t += d;
    }
  }
  return voci;
}

// ---------- intro e chiusura disegnate in HTML ----------
function paginaCartello({ sopra, titolo, sotto, piede }) {
  const logo = fs.readFileSync(path.join(RADICE, 'sito/src/img/logo-bianco.svg'), 'utf8').replace(/width="\d+" height="\d+"/, 'width="220" height="220"');
  return `<!doctype html><html><head><meta charset="utf-8"><style>
  html,body{margin:0;width:${VISTA.width}px;height:${VISTA.height}px;overflow:hidden}
  body{background:radial-gradient(ellipse at 50% 38%,#223052 0%,#12141a 62%);color:#e6e8ee;font-family:'Segoe UI',system-ui,sans-serif;
    display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center}
  .logo{display:flex;align-items:center;gap:22px;margin-bottom:34px}
  .logo svg{width:112px;height:112px}
  .nome{font-size:68px;font-weight:700;letter-spacing:.5px}
  .sopra{font-size:22px;letter-spacing:4px;text-transform:uppercase;color:#7fb0ff;margin-bottom:12px}
  .titolo{font-size:46px;font-weight:600;max-width:1000px;line-height:1.2}
  .sotto{font-size:26px;color:#9097a6;margin-top:18px}
  .piede{position:absolute;bottom:40px;font-size:22px;color:#7fb0ff;letter-spacing:1px}
  .riga{width:120px;height:4px;border-radius:2px;background:#4a8cff;margin:26px auto 26px}
  </style></head><body>
  <div class="logo">${logo}<div class="nome">ArduLearn</div></div>
  ${sopra ? `<div class="sopra">${sopra}</div>` : ''}
  <div class="titolo">${titolo}</div>
  ${sotto ? `<div class="riga"></div><div class="sotto">${sotto}</div>` : ''}
  ${piede ? `<div class="piede">${piede}</div>` : ''}
  </body></html>`;
}

async function cartelli(video, cartella) {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: VISTA, deviceScaleFactor: SCALA });
  await page.setContent(paginaCartello({ sopra: `Video tutorial · ${video.numero}`, titolo: video.titolo, sotto: video.sottotitolo, piede: 'ardulearn.org' }));
  await page.screenshot({ path: path.join(cartella, 'intro.png') });
  await page.setContent(paginaCartello({
    sopra: video.prossimo ? 'Nel prossimo video' : 'Grazie per la visione',
    titolo: video.prossimo || 'Continua su ardulearn.org',
    sotto: 'Prova online, manuale e video su ardulearn.org', piede: 'ardulearn.org',
  }));
  await page.screenshot({ path: path.join(cartella, 'chiusura.png') });
  await browser.close();
}

// ---------- montaggio completo ----------
export async function monta(video, cartella, { voce, musica } = {}) {
  const reg = JSON.parse(fs.readFileSync(path.join(cartella, 'registrazione.json'), 'utf8'));
  const durCorpo = listaFotogrammi(reg, cartella);
  fileAss(reg.scene, cartella);
  await cartelli(video, cartella);
  const totale = DURATA_INTRO + durCorpo + DURATA_CHIUSURA;

  // i tre pezzi uniti con dissolvenze dal nero, audio muto o voce/musica
  const ingressi = [
    '-loop', '1', '-framerate', String(FPS), '-t', String(DURATA_INTRO), '-i', 'intro.png',
    '-f', 'concat', '-safe', '0', '-i', 'fotogrammi.ffconcat',
    '-loop', '1', '-framerate', String(FPS), '-t', String(DURATA_CHIUSURA), '-i', 'chiusura.png',
  ];
  let filtro = `[0:v]scale=${W}:${H},fps=${FPS},format=yuv420p,fade=t=in:d=0.6,fade=t=out:st=${DURATA_INTRO - 0.5}:d=0.5,setsar=1[a];` +
    `[1:v]scale=${W}:${H},fps=${FPS},format=yuv420p,ass=scritte.ass,fade=t=in:d=0.4,setsar=1[b];` +
    `[2:v]scale=${W}:${H},fps=${FPS},format=yuv420p,fade=t=in:d=0.5,fade=t=out:st=${DURATA_CHIUSURA - 0.8}:d=0.8,setsar=1[c];` +
    `[a][b][c]concat=n=3:v=1:a=0[v]`;
  let n = 3;
  const mix = [];
  if (voce) { ingressi.push('-i', path.resolve(voce)); mix.push(`[${n++}:a]aresample=48000,volume=1.0[vo]`); }
  if (musica) { ingressi.push('-stream_loop', '-1', '-i', path.resolve(musica)); mix.push(`[${n++}:a]aresample=48000,volume=0.12,afade=t=out:st=${totale - 3}:d=3[mu]`); }
  if (mix.length) {
    filtro += ';' + mix.join(';') + ';' + (mix.length === 2 ? '[vo][mu]amix=inputs=2:duration=longest:normalize=0' : voce ? '[vo]anull' : '[mu]anull') + `,apad,atrim=0:${totale.toFixed(3)}[au]`;
  } else {
    ingressi.push('-f', 'lavfi', '-t', totale.toFixed(3), '-i', 'anullsrc=r=48000:cl=stereo');
    filtro += `;[${n}:a]anull[au]`;
  }
  const uscita = `${video.file}.mp4`;
  ffmpeg([...ingressi, '-filter_complex', filtro, '-map', '[v]', '-map', '[au]',
    '-c:v', 'libx264', '-preset', 'slow', '-crf', '18', '-profile:v', 'high', '-pix_fmt', 'yuv420p',
    '-c:a', 'aac', '-b:a', '160k', '-t', totale.toFixed(3), '-movflags', '+faststart', uscita], cartella);

  // sottotitoli e copione, con i tempi del video finale (intro compresa)
  const sub = sottotitoli(reg.scene, DURATA_INTRO);
  fs.writeFileSync(path.join(cartella, `${video.file}.srt`), sub.map((s, i) => `${i + 1}\n${tempo(s.da)} --> ${tempo(s.a)}\n${aCapo(s.testo)}\n`).join('\n'));

  let cop = `# Copione — ${video.numero}. ${video.titolo}\n\nDurata totale ${mmss(totale)} (intro ${DURATA_INTRO} s, chiusura ${DURATA_CHIUSURA} s).\n` +
    `Leggi ogni frase quando il tempo arriva all'inizio indicato: la scena dura abbastanza per dirla con calma.\n\n` +
    `| Tempo | Scritta sullo schermo | Voce |\n|---|---|---|\n| 0:00 | *(intro con logo)* | — |\n`;
  for (const s of reg.scene) cop += `| ${mmss(DURATA_INTRO + s.inizio)}–${mmss(DURATA_INTRO + s.fine)} | ${s.scritta || '—'} | ${s.voce || '—'} |\n`;
  cop += `| ${mmss(DURATA_INTRO + durCorpo)} | *(chiusura: ${video.prossimo ? 'prossimo video — ' + video.prossimo : 'ardulearn.org'})* | — |\n`;
  fs.writeFileSync(path.join(cartella, `${video.file}-copione.md`), cop);

  // capitoli per la descrizione di YouTube (il primo deve stare a 0:00)
  const capitoli = reg.scene.filter(s => s.capitolo).map((s, i) => `${i ? mmss(DURATA_INTRO + s.inizio) : '0:00'} ${s.capitolo}`).join('\n');
  return { uscita: path.join(cartella, uscita), totale, capitoli };
}

// ---------- provini: un fotogramma a metà di ogni scena, in una griglia da controllare a colpo d'occhio ----------
export function provini(video, cartella) {
  const reg = JSON.parse(fs.readFileSync(path.join(cartella, 'registrazione.json'), 'utf8'));
  const dir = path.join(cartella, 'provini');
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir);
  const tempi = reg.scene.map(s => DURATA_INTRO + s.fine - 1.2);
  tempi.forEach((t, i) => ffmpeg(['-ss', t.toFixed(2), '-i', `${video.file}.mp4`, '-frames:v', '1', '-vf', 'scale=640:360', `provini/p${String(i).padStart(2, '0')}.jpg`], cartella));
  const n = tempi.length, col = 3, righe = Math.ceil(n / col);
  const ing = [], lay = [];
  for (let i = 0; i < righe * col; i++) {
    if (i < n) ing.push('-i', `provini/p${String(i).padStart(2, '0')}.jpg`);
    else ing.push('-f', 'lavfi', '-i', 'color=c=black:s=640x360:d=1');
    lay.push(`${(i % col) * 640}_${Math.floor(i / col) * 360}`);
  }
  ffmpeg([...ing, '-filter_complex', `${Array.from({ length: righe * col }, (_, i) => `[${i}]`).join('')}xstack=inputs=${righe * col}:layout=${lay.join('|')}`, '-frames:v', '1', `${video.file}-provini.jpg`], cartella);
  return path.join(cartella, `${video.file}-provini.jpg`);
}

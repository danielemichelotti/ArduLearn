// Video fatti di pezzi (per esempio con la scheda vera): ogni pezzo diventa un filmato 1920x1080 senza
// scritte, poi i pezzi si uniscono in corpo.mp4 e le scene si mettono in fila. Le scritte, l'intro e la
// chiusura li aggiunge monta() come per gli altri video.
//   - pezzo della pagina (Playwright): fotogrammi + tagli (attese tolte) + coperture (zone sfocate)
//   - pezzo dell'app (ffmpeg gdigrab): tratti della registrazione, anche accelerati, finestra ingrandita
import fs from 'node:fs';
import path from 'node:path';
import { chromium } from 'playwright';
import { VISTA, SCALA, durataVoce } from './regia.mjs';
import { ffmpeg, ffprobeDurata } from './montaggio.mjs';

const FPS = 30, W = VISTA.width * SCALA, H = VISTA.height * SCALA;

// toglie dai tempi le attese tagliate: un istante dentro un taglio va all'inizio del taglio
function mappaTagli(tagli = []) {
  const t = [...tagli].sort((a, b) => a.da - b.da);
  return x => {
    let meno = 0;
    for (const c of t) {
      if (x >= c.a) meno += c.a - c.da;
      else if (x > c.da) meno += x - c.da;
    }
    return x - meno;
  };
}

// unisce le zone che si toccano nel tempo e nello spazio (meno zone = filtro più leggero)
function unisciZone(cop) {
  const z = cop.map(c => ({ ...c }));
  let cambiato = true;
  while (cambiato) {
    cambiato = false;
    for (let i = 0; i < z.length && !cambiato; i++) for (let j = i + 1; j < z.length && !cambiato; j++) {
      const a = z[i], b = z[j];
      // stesso elemento che si è spostato di poco (per esempio mentre la pagina scorre)
      const tempo = a.da <= b.a + 0.3 && b.da <= a.a + 0.3;
      const spazio = Math.abs(a.x - b.x) <= 12 && Math.abs(a.w - b.w) <= 12 && Math.abs(a.h - b.h) <= 12 && Math.abs(a.y - b.y) <= 60;
      if (tempo && spazio) {
        const x = Math.min(a.x, b.x), y = Math.min(a.y, b.y);
        z[i] = { x, y, w: Math.max(a.x + a.w, b.x + b.w) - x, h: Math.max(a.y + a.h, b.y + b.h) - y, da: Math.min(a.da, b.da), a: Math.max(a.a, b.a) };
        z.splice(j, 1); cambiato = true;
      }
    }
  }
  return z;
}

// filtro che copre le zone indicate nei loro intervalli di tempo con una copia molto sfocata del fotogramma
// (una sola copia sfocata, ritagliata per ogni zona: una catena di "split" per zona bloccava ffmpeg)
function catenaSfocature(ingresso, coperture, uscita) {
  const z = unisciZone(coperture);
  if (!z.length) return `[${ingresso}]null[${uscita}]`;
  let f = `[${ingresso}]split[m][q];[q]scale=${W / 16}:${H / 16},scale=${W}:${H}:flags=bilinear,split=${z.length}${z.map((_, i) => `[q${i}]`).join('')};`;
  let cur = 'm';
  z.forEach((c, i) => {
    const nuovo = i === z.length - 1 ? uscita : `s${i}`;
    f += `[q${i}]crop=${c.w}:${c.h}:${c.x}:${c.y}[b${i}];[${cur}][b${i}]overlay=${c.x}:${c.y}:enable='between(t,${c.da.toFixed(3)},${c.a.toFixed(3)})'[${nuovo}];`;
    cur = nuovo;
  });
  return f.replace(/;$/, '');
}

// ---------- pezzo della pagina ----------
function pezzoPagina(nome, dir, def = {}) {
  const reg = JSON.parse(fs.readFileSync(path.join(dir, 'registrazione.json'), 'utf8'));
  const T = mappaTagli(reg.tagli);
  const rel = reg.fotogrammi.map(f => ({ file: f.file, t: T(f.t - reg.t0) }));
  let i0 = 0;
  while (i0 + 1 < rel.length && rel[i0 + 1].t <= 0) i0++;
  const usati = rel.slice(i0);
  usati[0].t = 0;
  const fine = T(reg.tFine - reg.t0);
  let txt = 'ffconcat version 1.0\n';
  usati.forEach((f, i) => {
    const d = (i + 1 < usati.length ? usati[i + 1].t : fine) - f.t;
    txt += `file 'fotogrammi/${f.file}'\nduration ${Math.max(d, 0.001).toFixed(4)}\n`;
  });
  txt += `file 'fotogrammi/${usati.at(-1).file}'\n`;
  fs.writeFileSync(path.join(dir, 'fotogrammi.ffconcat'), txt);

  // zone annotate durante la registrazione più quelle fisse del video (pixel del video, tempi del pezzo già tagliato)
  const cop = [...(reg.coperture || []).map(c => ({ ...c, da: Math.max(0, T(c.da)), a: T(c.a) })),
    ...(def.copertureFisse || []).map(c => ({ ...c, a: Math.min(c.a ?? fine, fine) }))].filter(c => c.a > c.da);
  // coda: l'ultimo fotogramma resta fermo qualche secondo (per esempio per finire la frase della voce)
  const coda = def.coda || 0, tot = fine + coda;
  fs.writeFileSync(path.join(dir, 'filtro.txt'), `[0:v]scale=${W}:${H},fps=${FPS},format=yuv420p${coda ? `,tpad=stop_mode=clone:stop_duration=${coda}` : ''}[p];` +
    catenaSfocature('p', cop.map(c => c.a >= fine - 0.05 ? { ...c, a: tot } : c), 'v'));
  ffmpeg(['-f', 'concat', '-safe', '0', '-i', 'fotogrammi.ffconcat', '-/filter_complex', 'filtro.txt', '-map', '[v]',
    '-t', tot.toFixed(3), '-c:v', 'libx264', '-preset', 'medium', '-crf', '16', '-pix_fmt', 'yuv420p', 'pezzo.mp4'], dir);
  const scene = reg.scene.map(s => ({ ...s, inizio: T(s.inizio), fine: T(s.fine) }));
  scene.at(-1).fine = tot;
  for (const s of scene) if (s.voce && s.fine - s.inizio < durataVoce(s.voce) - 0.3)
    console.warn(`  ATTENZIONE pezzo ${nome}: la scena "${s.scritta}" dura ${(s.fine - s.inizio).toFixed(1)} s, la voce ne chiede ${durataVoce(s.voce).toFixed(1)}`);
  console.log(`  pezzo ${nome}: ${tot.toFixed(1)} s, ${scene.length} scene, ${unisciZone(cop).length} zone sfocate`);
  return { file: path.join(dir, 'pezzo.mp4'), durata: tot, scene };
}

// ---------- pezzo dell'app ----------
async function sfondo(cartella) {
  const f = path.join(cartella, 'sfondo.png');
  if (fs.existsSync(f)) return f;
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: VISTA, deviceScaleFactor: SCALA });
  await page.setContent(`<body style="margin:0;width:${VISTA.width}px;height:${VISTA.height}px;background:radial-gradient(ellipse at 50% 40%,#223052 0%,#12141a 70%)"></body>`);
  await page.screenshot({ path: f });
  await browser.close();
  return f;
}

async function pezzoApp(nome, def, cartella) {
  const dir = path.join(cartella, nome);
  fs.mkdirSync(dir, { recursive: true });
  const bg = path.resolve(await sfondo(cartella));
  const src = path.resolve(cartella, def.file);
  // tratti della registrazione: ognuno tagliato e, se serve, accelerato
  let f = '', t = 0;
  def.tratti.forEach((tr, i) => {
    const v = tr.velocita || 1;
    f += `[0:v]trim=${tr.da}:${tr.a},setpts=(PTS-STARTPTS)/${v}[t${i}];`;
    tr.inizio = t; t += (tr.a - tr.da) / v; tr.fine = t;
  });
  const durata = t;
  // finestra dell'app ingrandita al centro dello sfondo, con un'ombra leggera
  const k = def.scala || 1.5, fw = Math.round(def.larghezza * k / 2) * 2, fh = Math.round(def.altezza * k / 2) * 2;
  const x = Math.round((W - fw) / 2), y = Math.round((H - fh) / 2);
  f += `${def.tratti.map((_, i) => `[t${i}]`).join('')}concat=n=${def.tratti.length}:v=1:a=0,fps=${FPS},scale=${fw}:${fh}:flags=lanczos[app];` +
    `[1:v]scale=${W}:${H},loop=-1:1,trim=0:${durata.toFixed(3)},setpts=PTS-STARTPTS[bg];` +
    `[bg]drawbox=x=${x + 10}:y=${y + 14}:w=${fw}:h=${fh}:color=black@0.45:t=fill,boxblur=12:1[bgo];` +
    `[bgo][app]overlay=${x}:${y}:shortest=1,format=yuv420p[p];`;
  const cop = (def.coperture || []).map(c => ({ x: x + Math.round(c.x * k), y: y + Math.round(c.y * k), w: Math.round(c.w * k / 2) * 2, h: Math.round(c.h * k / 2) * 2, da: c.da, a: c.a }));
  f += catenaSfocature('p', cop, 'v');
  fs.writeFileSync(path.join(dir, 'filtro.txt'), f);
  ffmpeg(['-i', src, '-loop', '1', '-i', bg, '-/filter_complex', 'filtro.txt', '-map', '[v]', '-t', durata.toFixed(3),
    '-c:v', 'libx264', '-preset', 'medium', '-crf', '16', '-pix_fmt', 'yuv420p', 'pezzo.mp4'], dir);
  // scene: ogni scena comincia all'istante indicato (tempo del pezzo finito) e finisce alla successiva
  const scene = def.scene.map((s, i) => ({ basso: false, capitolo: '', ...s, inizio: s.da, fine: i + 1 < def.scene.length ? def.scene[i + 1].da : durata }));
  for (const s of scene) if (s.voce && s.fine - s.inizio < durataVoce(s.voce) - 0.3)
    console.warn(`  ATTENZIONE pezzo ${nome}: la scena "${s.scritta}" dura ${(s.fine - s.inizio).toFixed(1)} s, la voce ne chiede ${durataVoce(s.voce).toFixed(1)}`);
  console.log(`  pezzo ${nome}: ${durata.toFixed(1)} s, ${scene.length} scene`);
  return { file: path.join(dir, 'pezzo.mp4'), durata, scene };
}

// ---------- tutti i pezzi, nell'ordine, in corpo.mp4 ----------
export async function uniscePezzi(video, cartella) {
  const fatti = [];
  for (const nome of video.ordine) {
    const def = video.pezzi[nome];
    fatti.push(def.url ? pezzoPagina(nome, path.join(cartella, nome), def) : await pezzoApp(nome, def, cartella));
  }
  const lista = fatti.map(p => `file '${path.resolve(p.file).replace(/\\/g, '/')}'`).join('\n');
  fs.writeFileSync(path.join(cartella, 'pezzi.txt'), lista + '\n');
  // si ricodifica: una copia diretta di pezzi fatti in modi diversi lasciava tempi sballati e bloccava il montaggio
  ffmpeg(['-f', 'concat', '-safe', '0', '-i', 'pezzi.txt', '-vf', `fps=${FPS},format=yuv420p,setsar=1`,
    '-c:v', 'libx264', '-preset', 'medium', '-crf', '16', 'corpo.mp4'], cartella);
  let off = 0;
  const scene = [];
  for (const p of fatti) { for (const s of p.scene) scene.push({ ...s, inizio: s.inizio + off, fine: s.fine + off }); off += p.durata; }
  const durata = ffprobeDurata(path.join(cartella, 'corpo.mp4'));
  fs.writeFileSync(path.join(cartella, 'registrazione.json'), JSON.stringify({ corpo: 'corpo.mp4', durata, scene }, null, 1));
  return durata;
}

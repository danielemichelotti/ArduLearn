// Regia: apre il simulatore, esegue le scene con ritmo leggibile e registra i fotogrammi.
// Ogni scena porta la sua scritta in sovrimpressione e la frase del copione: la scena dura
// almeno quanto serve a leggere la frase, così il copione e i sottotitoli tornano con il video.
import fs from 'node:fs';
import path from 'node:path';
import { chromium } from 'playwright';

// Pagina 1280x720 CSS con scala 1,5: il video esce a 1920x1080 con testi ben leggibili
export const VISTA = { width: 1280, height: 720 }, SCALA = 1.5;
const PAROLE_AL_SECONDO = 2.4;               // ritmo di lettura calmo, da tutorial

export function durataVoce(testo) {
  if (!testo) return 0;
  const parole = testo.trim().split(/\s+/).length;
  return parole / PAROLE_AL_SECONDO + 0.8;
}

const attesa = ms => new Promise(ok => setTimeout(ok, ms));

// Cursore finto, anello al clic ed evidenziatore: la registrazione senza finestra non mostra il mouse
const SCRIPT_PAGINA = `(() => {
  const avvia = () => {
    if (document.getElementById('__cur')) return;
    const st = document.createElement('style');
    st.textContent = \`
      #__cur{position:fixed;left:0;top:0;width:26px;height:26px;z-index:2147483647;pointer-events:none;
        transform:translate(-100px,-100px);filter:drop-shadow(0 2px 3px rgba(0,0,0,.55))}
      .__clic{position:fixed;width:14px;height:14px;margin:-7px 0 0 -7px;border-radius:50%;z-index:2147483646;
        pointer-events:none;border:3px solid #ffd23f;animation:__clic .55s ease-out forwards}
      @keyframes __clic{to{transform:scale(3.4);opacity:0}}
      .__evid{position:fixed;z-index:2147483645;pointer-events:none;border:3px solid #ffd23f;border-radius:10px;
        box-shadow:0 0 0 4px rgba(255,210,63,.25),0 0 24px rgba(255,210,63,.45);opacity:0;transition:opacity .35s}
      .__evid.on{opacity:1}\`;
    document.head.append(st);
    const c = document.createElement('div');
    c.id = '__cur';
    c.innerHTML = '<svg viewBox="0 0 26 26" width="26" height="26"><path d="M3 2l0 19 5-4.6 3.4 7.6 3.4-1.5-3.3-7.4 6.9-.4z" fill="#fff" stroke="#111" stroke-width="1.6" stroke-linejoin="round"/></svg>';
    document.body.append(c);
    const muovi = e => { c.style.transform = 'translate(' + (e.clientX - 3) + 'px,' + (e.clientY - 2) + 'px)'; };
    addEventListener('mousemove', muovi, true);
    addEventListener('pointermove', muovi, true);
    addEventListener('mousedown', e => {
      const a = document.createElement('div'); a.className = '__clic';
      a.style.left = e.clientX + 'px'; a.style.top = e.clientY + 'px';
      document.body.append(a); setTimeout(() => a.remove(), 700);
    }, true);
  };
  if (document.readyState === 'loading') addEventListener('DOMContentLoaded', avvia); else avvia();
  window.__evidenzia = (r, ms) => {
    const d = document.createElement('div'); d.className = '__evid';
    Object.assign(d.style, { left: r.x - 6 + 'px', top: r.y - 6 + 'px', width: r.width + 12 + 'px', height: r.height + 12 + 'px' });
    document.body.append(d);
    requestAnimationFrame(() => d.classList.add('on'));
    setTimeout(() => { d.classList.remove('on'); setTimeout(() => d.remove(), 400); }, ms);
  };
})();`;

export class Regia {
  constructor(page, cartellaFotogrammi) {
    this.p = page;
    this.dir = cartellaFotogrammi;
    this.fotogrammi = [];      // { file, t }
    this.scene = [];           // { scritta, voce, inizio, fine }
    this.mouse = { x: VISTA.width / 2, y: VISTA.height / 2 };
  }

  // ---------- registrazione ----------
  async avviaRegistrazione() {
    fs.rmSync(this.dir, { recursive: true, force: true });
    fs.mkdirSync(this.dir, { recursive: true });
    this.cdp = await this.p.context().newCDPSession(this.p);
    this.cdp.on('Page.screencastFrame', async f => {
      const n = this.fotogrammi.length;
      const file = `f${String(n).padStart(6, '0')}.jpg`;
      fs.writeFileSync(path.join(this.dir, file), Buffer.from(f.data, 'base64'));
      this.fotogrammi.push({ file, t: f.metadata.timestamp });
      try { await this.cdp.send('Page.screencastFrameAck', { sessionId: f.sessionId }); } catch { }
    });
    await this.cdp.send('Page.startScreencast', { format: 'jpeg', quality: 92, maxWidth: VISTA.width * SCALA, maxHeight: VISTA.height * SCALA, everyNthFrame: 1 });
    await attesa(300);
    this.t0 = Date.now() / 1000;
  }

  async fermaRegistrazione() {
    // un ultimo movimento invisibile obbliga il browser a mandare il fotogramma finale
    await this.p.evaluate(() => document.body.style.outline = '0px solid transparent');
    await attesa(400);
    this.tFine = Date.now() / 1000;
    await this.cdp.send('Page.stopScreencast');
    await attesa(300);
  }

  ora() { return Date.now() / 1000 - this.t0; }

  // ---------- scene ----------
  async scena({ scritta = '', voce = '', capitolo = '', basso = false }, azioni) {
    const inizio = this.ora();
    const s = { scritta, voce, capitolo, basso, inizio, fine: inizio };
    this.scene.push(s);
    if (azioni) await azioni();
    const manca = durataVoce(voce) - (this.ora() - inizio);
    if (manca > 0) await attesa(manca * 1000);
    s.fine = this.ora();
    console.log(`  scena ${String(this.scene.length).padStart(2)}  ${s.inizio.toFixed(1).padStart(6)}–${s.fine.toFixed(1).padStart(6)} s  ${scritta || voce.slice(0, 40)}`);
  }

  pausa(ms) { return attesa(ms); }

  // ---------- mouse e tastiera, con movimenti visibili ----------
  async centro(sel) {
    const loc = typeof sel === 'string' ? this.p.locator(sel).first() : sel.first();
    await loc.scrollIntoViewIfNeeded().catch(() => { });
    const r = await loc.boundingBox();
    if (!r) throw new Error('elemento non trovato: ' + sel);
    return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
  }

  async punto(obj) { return typeof obj === 'string' || (obj && obj.boundingBox) ? this.centro(obj) : obj; }

  async muovi(dove, { velocita = 900 } = {}) {
    const z = await this.punto(dove);
    const a = { ...this.mouse };
    const dist = Math.hypot(z.x - a.x, z.y - a.y);
    const passi = Math.max(8, Math.round(dist / velocita * 60));
    for (let i = 1; i <= passi; i++) {
      const k = i / passi, e = k < .5 ? 2 * k * k : 1 - (-2 * k + 2) ** 2 / 2;    // accelera e rallenta
      await this.p.mouse.move(a.x + (z.x - a.x) * e, a.y + (z.y - a.y) * e);
      await attesa(1000 / 60);
    }
    this.mouse = { x: z.x, y: z.y };
  }

  async clic(dove, { dopo = 350 } = {}) {
    await this.muovi(dove);
    await attesa(180);
    await this.p.mouse.down(); await attesa(90); await this.p.mouse.up();
    await attesa(dopo);
  }

  async trascina(da, a, { velocita = 650 } = {}) {
    await this.muovi(da);
    await attesa(200);
    await this.p.mouse.down();
    await attesa(120);
    const z = await this.punto(a);
    await this.muovi(z, { velocita });
    await attesa(150);
    await this.p.mouse.up();
    await attesa(350);
  }

  async scrivi(sel, testo, { ritmo = 110 } = {}) {
    await this.clic(sel, { dopo: 200 });
    await this.p.keyboard.type(testo, { delay: ritmo });
    await attesa(300);
  }

  async cancella(sel) {
    await this.p.locator(sel).first().fill('');
    await attesa(200);
  }

  // Le tendine native non si vedono nella registrazione: clic visibile e poi scelta diretta
  async scegli(sel, valore) {
    await this.clic(sel, { dopo: 250 });
    await this.p.keyboard.press('Escape').catch(() => { });
    await this.p.locator(sel).first().selectOption(String(valore));
    await attesa(400);
  }

  async numero(sel, valore) {
    await this.clic(sel, { dopo: 150 });
    await this.p.keyboard.press('Control+A');
    await this.p.keyboard.type(String(valore), { delay: 140 });
    await this.p.keyboard.press('Tab');
    await attesa(350);
  }

  async evidenzia(sel, ms = 1800) {
    const loc = typeof sel === 'string' ? this.p.locator(sel).first() : sel.first();
    const r = await loc.boundingBox();
    if (r) await this.p.evaluate(([r, ms]) => window.__evidenzia(r, ms), [r, ms]);
  }
}

// Apre il browser sul simulatore, registra le scene e salva fotogrammi e tempi
export async function registra({ url, cartella, scene }) {
  const browser = await chromium.launch();
  const context = await browser.newContext({ viewport: VISTA, deviceScaleFactor: SCALA, colorScheme: 'dark', locale: 'it-IT' });
  await context.addInitScript(SCRIPT_PAGINA);
  const page = await context.newPage();
  page.on('dialog', d => d.dismiss());
  page.on('pageerror', e => console.warn('  errore nella pagina:', e.message));
  await page.goto(url);
  await page.waitForLoadState('networkidle');
  await attesa(800);

  const r = new Regia(page, path.join(cartella, 'fotogrammi'));
  await r.avviaRegistrazione();
  await scene(r);
  await r.fermaRegistrazione();
  await browser.close();

  const dati = { t0: r.t0, tFine: r.tFine, fotogrammi: r.fotogrammi, scene: r.scene };
  fs.writeFileSync(path.join(cartella, 'registrazione.json'), JSON.stringify(dati, null, 1));
  return dati;
}

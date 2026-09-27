// Video 2 — Ingressi, uscite e pannello nella pagina (esempio 6, Dimmer PWM, UNO R4 WiFi)
export default {
  file: '02-ingressi-uscite-pannello',
  numero: 2,
  titolo: 'Ingressi, uscite e pannello nella pagina',
  sottotitolo: 'Cursore, uscita PWM, indicatore a lampada e ingresso analogico',
  prossimo: 'Il primo programma in LADDER',
  url: '/?demo&sito&r4',

  youtube: {
    titolo: 'ArduLearn · 2. Ingressi, uscite e pannello nella pagina (Dimmer PWM)',
    descrizione: `Secondo video tutorial di ArduLearn, il PLC didattico per Arduino che si programma dal browser.

Apriamo l'esempio Dimmer PWM e vediamo la differenza fra i blocchi del pannello, che vivono nella pagina (cursore, indicatore), e gli ingressi e le uscite, che leggono e comandano i pin della scheda. L'Indicatore diventa una lampada che va da 0 (spenta) a 255 (piena luce), come il PWM; poi aggiungiamo un ingresso analogico su A0.

Nel video:
{capitoli}

Prova anche tu, gratis e senza installare nulla: https://ardulearn.org/prova/
Manuale e altri video: https://ardulearn.org
Codice sorgente: https://github.com/danielemichelotti/ArduLearn`,
    tag: ['ArduLearn', 'Arduino', 'PLC', 'PLC didattico', 'FBD', 'PWM', 'ingresso analogico', 'UNO R4 WiFi', 'simulatore', 'tutorial'],
  },
  sito: { categoria: 'Primi passi', ordine: 2, descrizione: 'Il pannello nella pagina (cursore e indicatore), l\'uscita PWM e un ingresso analogico, con l\'esempio Dimmer PWM.' },

  async scene(r) {
    const p = r.p;
    const ultimo = () => p.evaluate(() => Math.max(...[...document.querySelectorAll('.blk')].map(e => +e.dataset.id)));
    let adc, ind;
    const porta = (id, tipo, i = 0) => `.port.${tipo}[data-id="${id}"]` + (tipo === 'in' ? `[data-i="${i}"]` : '');
    // trascina la manopola di un cursore fino alla frazione f della sua corsa
    const cursore = async (sel, da, a) => {
      const t = await p.locator(sel).first().boundingBox();
      const x = f => ({ x: t.x + 6 + (t.width - 12) * f, y: t.y + t.height / 2 });
      await r.muovi(x(da)); await r.pausa(200); await p.mouse.down();
      for (const f of a) { await r.muovi(x(f), { velocita: 260 }); await r.pausa(700); }
      await p.mouse.up(); await r.pausa(300);
    };

    await r.scena({
      scritta: 'Gli esempi sono nella scheda Progetti',
      capitolo: 'Aprire un esempio',
      basso: true,
      voce: 'In questo video vediamo ingressi, uscite e i comandi del pannello nella pagina. Partiamo da un esempio pronto: nella scheda Progetti, in fondo, c\'è l\'elenco degli esempi.',
    }, async () => {
      await r.pausa(1500);
      await r.clic('#banner button');
      await r.clic(p.getByText('Progetti', { exact: true }));
      await r.pausa(800);
      await r.scorri('[data-ex="5"]');
      await r.muovi('[data-ex="5"]');
    });

    await r.scena({
      scritta: 'Apriamo «Dimmer PWM»',
      voce: 'Apriamo Dimmer PWM: un cursore regola la luminosità di un LED.',
    }, async () => {
      await r.evidenzia('[data-ex="5"]', 1800);
      await r.pausa(1200);
      await r.clic('[data-ex="5"]');
      await r.pausa(800);
    });

    await r.scena({
      scritta: 'Pannello: comandi nella pagina',
      capitolo: 'Pannello, ingressi e uscite',
      voce: 'I blocchi rosa sono il pannello: comandi e indicatori che compaiono nella pagina, e funzionano anche con la scheda vera. Qui un Cursore virtuale dà un valore da zero a duecentocinquantacinque.',
    }, async () => {
      await r.muovi(p.locator('#palette').getByText(/pannello/i));
      await r.evidenzia(p.locator('#palette').getByText(/pannello/i), 2500);
      await r.pausa(3500);
      await r.muovi('.blk[data-id="1"] .bt');
      await r.evidenzia('.blk[data-id="1"]', 3000);
    });

    await r.scena({
      scritta: 'Uscita PWM: da 0 a 255 sul pin D9',
      voce: 'I blocchi verdi sono gli ingressi e le uscite della scheda. L\'Uscita PWM manda al pin D9 un valore da zero a duecentocinquantacinque: più è alto, più il LED è luminoso.',
    }, async () => {
      await r.muovi(p.locator('#palette').getByText(/ingressi e uscite/i));
      await r.evidenzia(p.locator('#palette').getByText(/ingressi e uscite/i), 2500);
      await r.pausa(3000);
      await r.muovi('.blk[data-id="2"] .bt');
      await r.evidenzia('.blk[data-id="2"]', 3500);
    });

    await r.scena({
      scritta: 'Indicatore: numero, barra o lampada',
      voce: 'L\'Indicatore mostra un valore nella pagina. Lo trasformiamo in lampada: a zero è spenta, a duecentocinquantacinque è piena luce.',
    }, async () => {
      await r.clic('.blk[data-id="3"] .bt');
      await r.pausa(1200);
      await r.scegli('#inspector select[data-k="0"]', '1');
    });

    await r.scena({
      scritta: 'Carica e muovi il cursore',
      capitolo: 'Prova nel simulatore',
      voce: 'Carichiamo e muoviamo il cursore. Il numero sul filo, la barra dell\'uscita e la lampada seguono il valore.',
    }, async () => {
      await r.clic('#uploadBtn');
      await r.pausa(1200);
      await cursore('.blk[data-id="1"] .ctl .sl-track', 0, [0.25, 0.5, 1, 0.1]);
    });

    await r.scena({
      scritta: 'Un ingresso vero: analogico su A0',
      capitolo: 'Ingresso analogico',
      voce: 'Ora un ingresso della scheda: un Ingresso analogico, come un potenziometro sul pin A0. Legge valori da zero a milleventitré.',
    }, async () => {
      await r.trascina('.pitem[data-t="3"]', { x: 380, y: 545 });
      adc = await ultimo();
      await r.pausa(500);
      await r.scegli('#inspector select[data-pin]', { label: 'A0' });
    });

    await r.scena({
      scritta: 'Collegalo a un Indicatore e ricarica',
      voce: 'Lo colleghiamo a un nuovo Indicatore, che lascia vedere il numero, e carichiamo di nuovo il programma.',
    }, async () => {
      await r.trascina('.pitem[data-t="8"]', { x: 650, y: 545 });
      ind = await ultimo();
      await r.scegli('#inspector select[data-k="0"]', '0');
      await r.trascina(porta(adc, 'out'), porta(ind, 'in', 0), { velocita: 400 });
      await r.clic('#uploadBtn');
    });

    await r.scena({
      scritta: 'Nel simulatore: «regola» fa da potenziometro',
      voce: 'Nel simulatore l\'ingresso ha un cursore, regola, che fa le veci del potenziometro. Con la scheda vera il valore arriva dal pin.',
    }, async () => {
      await r.pausa(800);
      await cursore(`.blk[data-id="${adc}"] [data-simctl="sl"] .sl-track`, 0, [0.3, 0.75, 0.5]);
    });

    await r.scena({
      scritta: 'Rosa = nella pagina · Verde = pin della scheda',
      capitolo: 'Riassunto',
      voce: 'Riassumendo: i blocchi rosa del pannello vivono nella pagina, quelli verdi leggono e comandano i pin della scheda.',
    }, async () => {
      await r.muovi({ x: 640, y: 600 });
    });
  },
};

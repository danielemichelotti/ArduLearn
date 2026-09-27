// Video 4 — GRAFCET: un ciclo a passi (esempio 10, Carrello avanti e indietro, UNO R4 WiFi)
export default {
  file: '04-grafcet',
  numero: 4,
  titolo: 'GRAFCET: un ciclo a passi',
  sottotitolo: 'Tappe, transizioni e azioni con il carrello avanti e indietro',
  prossimo: 'Dalla prova al banco: la scheda vera',
  url: '/?demo&sito&r4&esempio=10',

  youtube: {
    titolo: 'ArduLearn · 4. GRAFCET: un ciclo a passi (carrello avanti e indietro)',
    descrizione: `Quarto video tutorial di ArduLearn, il PLC didattico per Arduino che si programma dal browser.

Con l'esempio Carrello avanti e indietro vediamo come si legge un GRAFCET (SFC): la tappa iniziale, le transizioni con le loro condizioni e le azioni N. Poi lo facciamo girare nel simulatore online, azionando Marcia e i finecorsa, e vediamo dove si scrivono le sicurezze in LADDER.

Nel video:
{capitoli}

Prova anche tu, gratis e senza installare nulla: https://ardulearn.org/prova/
Manuale e altri video: https://ardulearn.org
Codice sorgente: https://github.com/danielemichelotti/ArduLearn`,
    tag: ['ArduLearn', 'Arduino', 'PLC', 'PLC didattico', 'GRAFCET', 'SFC', 'sequenze', 'finecorsa', 'UNO R4 WiFi', 'tutorial'],
  },
  sito: { categoria: 'Linguaggi', ordine: 4, descrizione: 'Tappe, transizioni e azioni: il carrello avanti e indietro in GRAFCET, provato nel simulatore.' },

  async scene(r) {
    const p = r.p;
    const tappa = n => `#grafArea .gstep[data-gst="${n}"]`;
    const trans = n => `#grafArea .gtrans[data-gtr="${n}"]`;
    const azione = n => `#grafArea .gact[data-gst="${n}"]`;
    const tasto = nome => p.locator('#grafArea [data-simctl="din"]', { hasText: nome }).first();
    const vuoto = { x: 820, y: 470 };

    await r.scena({
      scritta: 'GRAFCET: una sequenza a passi',
      capitolo: 'L\'esempio GRAFCET',
      voce: 'Il GRAFCET descrive una sequenza a passi, una tappa alla volta. Abbiamo aperto l\'esempio Carrello avanti e indietro: un carrello va verso destra e poi torna a sinistra.',
    }, async () => {
      await r.pausa(2500);
      await r.evidenzia('#grafArea svg', 4000);
    });

    await r.scena({
      scritta: 'Le tappe: 1 iniziale, 2 avanti, 3 indietro',
      capitolo: 'Tappe, transizioni e azioni',
      voce: 'I quadrati sono le tappe. La tappa uno, con il doppio bordo, è quella iniziale: il carrello aspetta. Nella tappa due va avanti, nella tre torna indietro.',
    }, async () => {
      await r.pausa(1500);
      for (const n of [1, 2, 3]) { await r.muovi(tappa(n)); await r.evidenzia(tappa(n), 2400); await r.pausa(2800); }
    });

    await r.scena({
      scritta: 'Le transizioni e le loro condizioni',
      voce: 'Fra una tappa e l\'altra ci sono le transizioni, ognuna con la sua condizione. Per partire servono Marcia e il finecorsa sinistro. Il finecorsa destro fa tornare indietro, quello sinistro chiude il ciclo.',
    }, async () => {
      await r.pausa(2500);
      for (const n of [101, 102, 103]) { await r.muovi(trans(n)); await r.evidenzia(trans(n), 2600); await r.pausa(3200); }
    });

    await r.scena({
      scritta: 'Le azioni: N = attiva finché la tappa è attiva',
      voce: 'A destra delle tappe ci sono le azioni. N vuol dire che l\'uscita resta accesa finché la sua tappa è attiva.',
    }, async () => {
      await r.muovi(azione(2)); await r.evidenzia(azione(2), 2500); await r.pausa(2800);
      await r.muovi(azione(3)); await r.evidenzia(azione(3), 2500);
    });

    await r.scena({
      scritta: 'Carica: la tappa attiva diventa verde',
      capitolo: 'Prova nel simulatore',
      voce: 'Carichiamo il programma. La tappa attiva si colora di verde: all\'inizio è la uno.',
    }, async () => {
      await r.clic('#uploadBtn');
      await r.pausa(1200);
      await r.muovi(tappa(1)); await r.evidenzia(tappa(1), 2500);
    });

    await r.scena({
      scritta: 'Il carrello è a sinistra: FC_sx premuto',
      voce: 'I tastini verdi sono i finecorsa a riposo. Il carrello parte da sinistra: con un clic su FC_sx il finecorsa resta premuto.',
    }, async () => {
      await r.pausa(2500);
      await r.clic(tasto('FC_sx'));
      await r.muovi(vuoto);
    });

    await r.scena({
      scritta: 'Marcia: tappa 2, il carrello va avanti',
      voce: 'Premiamo Marcia: la transizione scatta e si passa alla tappa due, il carrello va avanti. Appena si muove, il finecorsa sinistro si libera.',
    }, async () => {
      await r.tieni(tasto('Marcia'), 1000);
      await r.muovi(tappa(2));
      await r.pausa(2200);
      await r.clic(tasto('FC_sx'));
      await r.muovi(vuoto);
    });

    await r.scena({
      scritta: 'FC_dx: tappa 3, il carrello torna indietro',
      voce: 'Quando arriva a destra tocca FC_dx: si passa alla tappa tre e il carrello torna indietro.',
    }, async () => {
      await r.clic(tasto('FC_dx'));
      await r.muovi(tappa(3));
      await r.pausa(2000);
      await r.clic(tasto('FC_dx'));
      await r.muovi(vuoto);
    });

    await r.scena({
      scritta: 'FC_sx: di nuovo alla tappa iniziale',
      voce: 'Tornato a sinistra, FC_sx riporta il GRAFCET alla tappa uno, pronto per un nuovo ciclo.',
    }, async () => {
      await r.clic(p.locator('#grafArea .gtrans[data-gtr="103"] [data-simctl="din"]').first());
      await r.muovi(tappa(1));
    });

    await r.scena({
      scritta: 'LADDER: i segmenti per le sicurezze',
      capitolo: 'Le sicurezze in LADDER',
      basso: true,
      voce: 'Con il pulsante LADDER, in alto, si scrivono i segmenti per le sicurezze. Qui l\'Emergenza riporta il GRAFCET alla situazione iniziale.',
    }, async () => {
      const lad = p.locator('button:visible', { hasText: /^LADDER$/ }).first();
      await r.muovi(lad); await r.evidenzia(lad, 2000);
      await r.pausa(1200);
      await r.clic(lad);
      await r.pausa(4500);
      await r.clic(p.locator('button:visible', { hasText: /^SFC$/ }).first());
    });

    await r.scena({
      scritta: 'Tappe → transizioni → azioni',
      capitolo: 'Riassunto',
      voce: 'Riassumendo: le tappe dicono cosa si fa, le transizioni quando si passa alla tappa successiva, e le azioni comandano le uscite.',
    }, async () => {
      await r.muovi(vuoto);
    });
  },
};

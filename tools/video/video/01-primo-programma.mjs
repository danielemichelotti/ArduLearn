// Video 1 — Il primo programma nel simulatore (UNO R4 WiFi, blocchi FBD)
export default {
  file: '01-primo-programma',
  numero: 1,
  titolo: 'Il primo programma',
  sottotitolo: 'Blocchi, fili e Carica sulla scheda, nel simulatore online',
  prossimo: 'Ingressi, uscite e pannello nella pagina',
  url: '/?demo&sito&r4',

  youtube: {
    titolo: 'ArduLearn · 1. Il primo programma (simulatore online, UNO R4 WiFi)',
    descrizione: `Primo video tutorial di ArduLearn, il PLC didattico per Arduino che si programma dal browser.

In meno di tre minuti scriviamo il primo programma a blocchi (FBD) nel simulatore online, senza nessuna scheda collegata: un interruttore virtuale accende un lampeggiatore che comanda il LED su D13 della UNO R4 WiFi.

Nel video:
{capitoli}

Prova anche tu, gratis e senza installare nulla: https://ardulearn.org/prova/
Manuale e altri video: https://ardulearn.org
Codice sorgente: https://github.com/danielemichelotti/ArduLearn`,
    tag: ['ArduLearn', 'Arduino', 'PLC', 'PLC didattico', 'FBD', 'UNO R4 WiFi', 'simulatore', 'automazione', 'tutorial'],
  },
  sito: { categoria: 'Primi passi', ordine: 1, descrizione: 'Il primo programma a blocchi nel simulatore online: blocchi, fili, pin e Carica sulla scheda.' },

  async scene(r) {
    const p = r.p;
    const porta = (id, tipo, i = 0) => `.port.${tipo}[data-id="${id}"]` + (tipo === 'in' ? `[data-i="${i}"]` : '');

    await r.scena({
      scritta: 'Il simulatore: ardulearn.org/prova',
      capitolo: 'Il simulatore online',
      basso: true,
      voce: 'Ciao! In questo video scriviamo il primo programma con ArduLearn, il PLC didattico che si programma dal browser. Usiamo il simulatore online, quindi non serve nessuna scheda.',
    }, async () => {
      await r.pausa(2500);
      await r.evidenzia('#banner', 3500);
      await r.pausa(5000);
      await r.clic('#banner button');
    });

    await r.scena({
      scritta: 'Com\'è fatta la pagina',
      capitolo: 'Com\'è fatta la pagina',
      voce: 'A sinistra ci sono i blocchi, divisi per famiglie. Al centro c\'è il foglio, dove si disegna il programma. A destra l\'aiuto e le proprietà del blocco selezionato.',
    }, async () => {
      await r.muovi('#palette'); await r.evidenzia('#palette', 3200); await r.pausa(3800);
      await r.muovi('#canvasWrap'); await r.evidenzia('#canvasWrap', 3200); await r.pausa(3800);
      await r.muovi('#inspector'); await r.evidenzia('#inspector', 3200);
    });

    await r.scena({
      scritta: 'La scheda simulata e Carica sulla scheda',
      voce: 'In alto si sceglie la scheda da simulare, qui una UNO R4 WiFi, e c\'è il pulsante Carica sulla scheda, che useremo alla fine.',
    }, async () => {
      await r.muovi('#sitoBoard'); await r.evidenzia('#sitoBoard', 3000); await r.pausa(3800);
      await r.muovi('#uploadBtn'); await r.evidenzia('#uploadBtn', 3000);
    });

    await r.scena({
      scritta: '1 · Trascina i blocchi sul foglio',
      capitolo: 'Trascinare e cercare i blocchi',
      voce: 'Il programma farà lampeggiare il LED della scheda quando accendiamo un interruttore. Trasciniamo sul foglio un Interruttore virtuale: è un comando che compare nella pagina.',
    }, async () => {
      await r.pausa(3500);
      await r.trascina('.pitem[data-t="6"]', { x: 330, y: 300 });
    });

    await r.scena({
      scritta: 'Un blocco si può cercare per nome',
      voce: 'Un blocco si può anche cercare per nome. Scriviamo lampeggiatore e lo trasciniamo accanto.',
    }, async () => {
      await r.scrivi('#palSearch', 'lampeg');
      await r.trascina('.pitem[data-t="23"]', { x: 560, y: 300 });
      await r.cancella('#palSearch');
    });

    await r.scena({
      scritta: 'L\'uscita digitale comanda un pin',
      voce: 'Per ultima un\'Uscita digitale, che accende e spegne un pin della scheda.',
    }, async () => {
      await r.trascina('.pitem[data-t="2"]', { x: 790, y: 300 });
    });

    await r.scena({
      scritta: '2 · Collega i fili: da uscita a ingresso',
      capitolo: 'Collegare i fili',
      voce: 'Ora colleghiamo i blocchi. Si parte dal pallino di uscita, a destra, e si trascina fino al pallino di ingresso del blocco successivo.',
    }, async () => {
      await r.pausa(1500);
      await r.trascina(porta(1, 'out'), porta(2, 'in', 0), { velocita: 400 });
      await r.pausa(600);
      await r.trascina(porta(2, 'out'), porta(3, 'in', 0), { velocita: 400 });
    });

    await r.scena({
      scritta: '3 · Scegli il pin: D13, il LED della scheda',
      capitolo: 'Scegliere il pin e regolare i tempi',
      voce: 'Il bordo rosso tratteggiato avvisa che manca qualcosa: il pin. Selezioniamo l\'uscita e a destra scegliamo D13, il pin collegato al LED della scheda.',
    }, async () => {
      await r.pausa(1500);
      await r.clic('.blk[data-id="3"] .bt');
      await r.pausa(2500);
      await r.scegli('#inspector select[data-pin]', 13);
    });

    await r.scena({
      scritta: 'Regola i tempi del lampeggio',
      voce: 'Nel Lampeggiatore regoliamo i tempi: trecento millisecondi acceso e trecento spento.',
    }, async () => {
      await r.clic('.blk[data-id="2"] .bt');
      await r.pausa(600);
      await r.numero('#inspector input[data-k="1"]', 300);
      await r.numero('#inspector input[data-k="2"]', 300);
    });

    await r.scena({
      scritta: '4 · Carica sulla scheda',
      capitolo: 'Carica sulla scheda e prova',
      voce: 'Premiamo Carica sulla scheda. Nel simulatore il programma parte subito: in alto compare RUN.',
    }, async () => {
      await r.clic('#uploadBtn');
      await r.pausa(1200);
      await r.muovi('#runPill'); await r.evidenzia('#runPill', 2500);
    });

    await r.scena({
      scritta: 'Accendi l\'interruttore',
      voce: 'Accendiamo l\'interruttore. I fili verdi valgono uno e la lampada dell\'uscita lampeggia, come farebbe il LED sulla scheda.',
    }, async () => {
      await r.clic('.ctl[data-ctl="sw"]');
      await r.pausa(2500);
      await r.muovi({ x: 700, y: 470 });
    });

    await r.scena({
      scritta: 'Spegni: il lampeggio si ferma',
      voce: 'Spegniamo l\'interruttore e il lampeggio si ferma.',
    }, async () => {
      await r.clic('.ctl[data-ctl="sw"]');
    });

    await r.scena({
      scritta: 'Blocchi → fili → pin → Carica sulla scheda',
      capitolo: 'Riassunto',
      voce: 'Riassumendo: si trascinano i blocchi, si collegano i fili, si scelgono i pin e si carica. Con una scheda vera i passi sono esattamente gli stessi.',
    }, async () => {
      await r.muovi({ x: 640, y: 560 });
    });
  },
};

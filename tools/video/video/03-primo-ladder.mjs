// Video 3 — Il primo programma in LADDER (esempio 12, Marcia/arresto, UNO R4 WiFi)
export default {
  file: '03-primo-ladder',
  numero: 3,
  titolo: 'Il primo programma in LADDER',
  sottotitolo: 'Marcia e arresto con autoritenuta, nello stile di TIA Portal',
  prossimo: 'GRAFCET: un ciclo a passi',
  url: '/?demo&sito&r4&esempio=12',

  youtube: {
    titolo: 'ArduLearn · 3. Il primo programma in LADDER (marcia/arresto con autoritenuta)',
    descrizione: `Terzo video tutorial di ArduLearn, il PLC didattico per Arduino che si programma dal browser.

Il LADDER (KOP) di ArduLearn segue lo stile di Siemens TIA Portal: segmenti, contatti NA e NC, bobine, operandi %I e %Q e la tabella delle variabili. Con l'esempio Marcia/arresto vediamo l'autoritenuta e la proviamo nel simulatore online con il controllo online.

Nel video:
{capitoli}

Prova anche tu, gratis e senza installare nulla: https://ardulearn.org/prova/
Manuale e altri video: https://ardulearn.org
Codice sorgente: https://github.com/danielemichelotti/ArduLearn`,
    tag: ['ArduLearn', 'Arduino', 'PLC', 'PLC didattico', 'LADDER', 'KOP', 'TIA Portal', 'autoritenuta', 'marcia arresto', 'UNO R4 WiFi', 'tutorial'],
  },
  sito: { categoria: 'Linguaggi', ordine: 3, descrizione: 'Contatti, bobina, operandi %I e %Q e autoritenuta: il primo programma LADDER, nello stile di TIA Portal.' },

  async scene(r) {
    const p = r.p;
    // in LADDER la barra degli strumenti occupa la parte alta: tutte le scritte stanno in basso
    const scena = (o, f) => r.scena({ basso: true, ...o }, f);
    const cella = (riga, col) => `#ladList [data-cell="0,${riga},${col}"]`;
    const tasto = pin => p.locator(`#ladList [data-simctl="din"][data-pin="${pin}"]`);

    await scena({
      scritta: 'LADDER, nello stile di TIA Portal',
      capitolo: 'L\'esempio in LADDER',
      voce: 'In questo video il primo programma in LADDER, lo schema a contatti nello stile di TIA Portal. Abbiamo aperto l\'esempio Marcia e arresto, che si trova nella scheda Progetti.',
    }, async () => {
      await r.pausa(2500);
      await r.muovi('#ladList .lnet, #ladList > *');
      await r.evidenzia(p.locator('#ladList > *').first(), 4000);
    });

    await scena({
      scritta: 'Contatto NA · contatto NC · bobina',
      capitolo: 'Contatti e bobina',
      voce: 'Il programma è un segmento e si legge da sinistra a destra. Marcia è un contatto normalmente aperto, Arresto un contatto normalmente chiuso, e in fondo c\'è la bobina Motore.',
    }, async () => {
      await r.pausa(2800);
      await r.muovi(cella(0, 0)); await r.evidenzia(cella(0, 0), 2200); await r.pausa(2600);
      await r.muovi(cella(0, 1)); await r.evidenzia(cella(0, 1), 2200); await r.pausa(2600);
      await r.muovi(cella(0, 4)); await r.evidenzia(cella(0, 4), 2200);
    });

    await scena({
      scritta: 'Operandi: %I ingressi, %Q uscite',
      voce: 'Sopra ogni elemento c\'è l\'operando: %I per gli ingressi, %Q per le uscite, con il nome simbolico. Cliccando un elemento, nel pannello a destra, sotto le istruzioni, se ne vedono le proprietà.',
    }, async () => {
      await r.pausa(3500);
      await r.clic(cella(0, 1));
      await r.pausa(600);
      // le proprietà stanno sotto l'elenco delle istruzioni
      const prop = p.locator('#inspector').getByText(/^proprietà$/i).first();
      await r.scorri(prop);
      await r.muovi(prop);
      await r.evidenzia(prop.locator('xpath=..'), 3000);
    });

    await scena({
      scritta: 'Il ramo in parallelo: l\'autoritenuta',
      capitolo: 'L\'autoritenuta',
      voce: 'Sotto Marcia c\'è un ramo in parallelo, con un contatto del Motore stesso: è l\'autoritenuta, che tiene acceso il motore quando si rilascia il pulsante.',
    }, async () => {
      await r.pausa(1500);
      await r.muovi(cella(1, 0)); await r.evidenzia(cella(1, 0), 3500);
    });

    await scena({
      scritta: 'Pin e variabili: la tabella dei tag',
      capitolo: 'La tabella delle variabili',
      voce: 'Nella scheda Pin e variabili c\'è la tabella dei nomi, come quella dei tag di TIA Portal: Marcia è sul pin D2, Arresto su D3 e Motore su D13.',
    }, async () => {
      await r.clic(p.getByText('Pin e variabili', { exact: true }));
      await r.pausa(700);
      await r.evidenzia(p.locator('table').first(), 5500);
      await r.pausa(6000);
      await r.clic(p.getByText('Programma', { exact: true }).first());
    });

    await scena({
      scritta: 'Carica: in verde i tratti in tensione',
      capitolo: 'Prova nel simulatore',
      voce: 'Carichiamo. Con il controllo online i tratti in cui passa corrente diventano verdi.',
    }, async () => {
      await r.clic('#uploadBtn');
      await r.pausa(1000);
      await r.muovi(p.locator('text="controllo online" >> visible=true'));
      await r.evidenzia(p.locator('text="controllo online" >> visible=true'), 2500);
    });

    await scena({
      scritta: 'Tieni premuto Marcia: il motore parte e resta acceso',
      voce: 'I tastini sotto i contatti simulano i pulsanti. A riposo sono HIGH, perché gli ingressi hanno il pull-up. Teniamo premuto Marcia: il motore si accende e resta acceso anche quando lo lasciamo.',
    }, async () => {
      await r.muovi(tasto(2)); await r.evidenzia(tasto(2), 2000);
      await r.pausa(5500);
      await r.tieni(tasto(2), 1600);
      await r.muovi({ x: 700, y: 480 });
    });

    await scena({
      scritta: 'Premi Arresto: il motore si ferma',
      voce: 'Premiamo Arresto: il contatto chiuso si apre e il motore si ferma.',
    }, async () => {
      await r.tieni(tasto(3), 1400);
      await r.muovi({ x: 700, y: 480 });
    });

    await scena({
      scritta: 'Contatti → bobina · autoritenuta · %I e %Q',
      capitolo: 'Riassunto',
      voce: 'Riassumendo: i contatti in serie e in parallelo comandano la bobina, e l\'autoritenuta ricorda che è stata data la marcia.',
    }, async () => {
      await r.pausa(500);
    });
  },
};

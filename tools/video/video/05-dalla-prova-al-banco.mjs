// Video 5 — Dalla prova al banco: firmware con l'app per Windows, Wi-Fi e pagina della scheda vera.
// È fatto di pezzi: A e C sono registrazioni della finestra dell'app (ffmpeg gdigrab), B e D sono
// registrazioni della pagina della scheda (Playwright). Nomi di rete e indirizzi vengono sfocati.

// zone della pagina della scheda che possono contenere nomi di reti o indirizzi
const DA_COPRIRE = ['#wifiScanRes [data-wnet]', '#wifiSsid', '#wifiNets', 'dialog[open] h3', 'dialog[open] p', 'dialog[open] ol', 'dialog[open] li', 'dialog[open] b',
  '#setWifi b', '#hostLbl', '#netInfo', '#wifiInfo'];

export default {
  file: '05-dalla-prova-al-banco',
  numero: 5,
  titolo: 'Dalla prova al banco',
  sottotitolo: 'Firmware con l\'app per Windows, Wi-Fi e pagina della scheda vera',
  prossimo: '',
  ordine: ['A', 'B', 'C', 'D'],
  pezzi: {
    B: {
      url: 'http://192.168.4.1/',
      finestra: true,
      // coperture in più, decise guardando la registrazione (pixel del video, secondi del pezzo tagliato):
      // righe con i nomi delle reti dopo "Cerca reti" e dialogo del collegamento con il nome della rete scelta
      coda: 4,                     // secondi di ultimo fotogramma fermo, per finire la frase
      copertureFisse: [
        { x: 190, y: 520, w: 840, h: 150, da: 20 },
        { x: 646, y: 298, w: 628, h: 316, da: 34 },
      ],
      async scene(r) {
        const p = r.p;
        r.copri(DA_COPRIRE);
        await r.scena({
          scritta: 'La scheda nuova crea la sua rete: ArduLearn-xxxx',
          capitolo: 'Il Wi-Fi della scheda',
          voce: 'Una scheda appena caricata non conosce ancora nessuna rete Wi-Fi, quindi crea la sua: ArduLearn seguito da quattro caratteri, con password ardulearn. Colleghiamo il PC a questa rete e apriamo nel browser l\'indirizzo 192.168.4.1.',
        }, async () => { await r.pausa(3000); });

        await r.scena({
          scritta: 'Impostazioni → Rete Wi-Fi',
          voce: 'Nella scheda Impostazioni c\'è la sezione Rete Wi-Fi.',
        }, async () => {
          await r.clic(p.locator('text="Impostazioni" >> visible=true').first());
          await r.pausa(600);
          await r.scorri('#setWifiCard');
          await r.evidenzia('#setWifiCard', 2500);
        });

        await r.scena({
          scritta: 'Cerca reti, scegli la rete e scrivi la password',
          voce: 'Premiamo Cerca reti, scegliamo la rete del laboratorio e scriviamo la sua password. Poi Salva e collega.',
        }, async () => {
          await r.clic('#wifiScan');
          await r.aspetta(() => document.querySelectorAll('#wifiScanRes [data-wnet]').length > 0, null, 30000);
          await r.pausa(3500);
          // qui Daniele sceglie la rete, scrive la password e preme "Salva e collega": l'attesa si taglia
          console.log('  >>> tocca a te: scegli la rete, scrivi la password e premi "Salva e collega"');
          await r.taglio(() => r.aspetta(() => !!document.querySelector('#dlg[open]'), null, 900000));
        });

        await r.scena({
          scritta: 'Conferma con il PIN docente',
          voce: 'Per cambiare la rete, la scheda chiede il PIN docente.',
        }, async () => {
          await r.aspetta(() => /Collegamento a/.test(document.querySelector('#dlg[open] h3')?.textContent || ''), null, 300000);
        });

        await r.scena({
          scritta: 'La scheda passa sulla rete del laboratorio',
          voce: 'La scheda lascia la sua rete e si collega a quella del laboratorio. Ora ricolleghiamo anche il PC alla stessa rete.',
        }, async () => {
          await r.pausa(800);
          await r.evidenzia('#dlg', 3500);
        });
      },
    },

    // registrazioni della finestra dell'app (980x680, pixel della finestra), ingrandite di 1,5 volte
    A: {
      file: 'grezzi/A_ok.mkv',
      larghezza: 980, altezza: 680,
      tratti: [
        { da: 3, a: 8 },                     // Trova le schede → Carica il firmware
        { da: 8, a: 17 },                    // porta USB, tipo di scheda, Aggiorna
        { da: 34, a: 39 },                   // clic su Carica il firmware (la conferma è una finestra a parte)
        { da: 40, a: 78, velocita: 5 },      // caricamento, circa un minuto
        { da: 78, a: 83 },                   // "Ultimo passo: scollega e ricollega il cavo USB"
        { da: 83, a: 107, velocita: 8 },     // attesa del cavo
        { da: 107, a: 117 },                 // "Firmware caricato: la scheda è pronta"
      ],
      scene: [
        {
          da: 0, capitolo: 'Caricare il firmware',
          scritta: 'L\'app ArduLearn per Windows',
          voce: 'Dalla prova al banco: la scheda vera ha bisogno del firmware ArduLearn. Lo carica l\'app per Windows, gratuita, da ardulearn.org.',
        },
        {
          da: 10,
          scritta: 'Porta USB e tipo di scheda',
          voce: 'Colleghiamo la UNO R4 con il cavo USB: l\'app trova la porta e il tipo di scheda.',
        },
        {
          da: 18,
          scritta: 'Carica il firmware (qui accelerato)',
          voce: 'Premiamo Carica il firmware e confermiamo. Dura circa un minuto: qui è accelerato.',
        },
        {
          da: 26.6,
          scritta: 'Ultimo passo: scollega e ricollega il cavo USB',
          voce: 'Alla fine l\'app chiede di scollegare e ricollegare il cavo USB, così la scheda riparte da capo.',
        },
        {
          da: 34.6,
          scritta: 'Fatto: la scheda è pronta',
          voce: 'Fatto: la scheda è pronta, ma non conosce ancora nessuna rete Wi-Fi.',
        },
      ],
    },

    C: {
      file: 'grezzi/C_trova.mkv',
      larghezza: 980, altezza: 680,
      tratti: [{ da: 1, a: 33 }],
      // indirizzo IP e nome della rete nella riga della scheda
      coperture: [
        { x: 318, y: 148, w: 92, h: 32, da: 0, a: 999 },
        { x: 502, y: 148, w: 156, h: 32, da: 0, a: 999 },
      ],
      scene: [
        {
          da: 0, capitolo: 'Trovare la scheda',
          scritta: 'Trova le schede: l\'app le cerca nella rete',
          voce: 'Ricollegato il PC alla rete del laboratorio, nell\'app ArduLearn la sezione Trova le schede cerca le schede ArduLearn nella rete.',
        },
        {
          da: 11,
          scritta: 'Nome, indirizzo, rete e programma',
          voce: 'Per ogni scheda si vedono il nome, l\'indirizzo, la rete e il programma in esecuzione.',
        },
        {
          da: 19,
          scritta: 'Apri nel browser, oppure con il nome',
          voce: 'Apri nel browser apre la pagina della scheda. Apri con il nome usa ardulearn seguito dal codice della scheda e punto local.',
        },
      ],
    },

    D: {
      url: 'http://ardulearn-2b28.local/',
      finestra: true,              // il PIN docente lo scrive Daniele
      async scene(r) {
        const p = r.p;
        r.copri(DA_COPRIRE);
        await r.scena({
          scritta: 'La pagina della scheda vera',
          capitolo: 'Programmare la scheda vera',
          voce: 'Questa pagina arriva dalla scheda stessa: è uguale al simulatore, ma adesso Carica sulla scheda manda il programma alla UNO R4 vera.',
        }, async () => {
          await r.pausa(2500);
          await r.muovi('#uploadBtn'); await r.evidenzia('#uploadBtn', 2500);
        });

        await r.scena({
          scritta: 'Accesso docente con il PIN',
          voce: 'Per caricare programmi sulla scheda vera serve l\'accesso docente: premiamo Docente e scriviamo il PIN.',
        }, async () => {
          await r.clic(p.locator('button:visible', { hasText: 'Docente' }).first());
          await r.pausa(800);
          console.log('  >>> tocca a te: scrivi il PIN docente e premi Entra');
          await r.taglio(() => r.aspetta(() => !document.querySelector('#dlg[open]'), null, 600000));
        });

        await r.scena({
          scritta: 'Apriamo il Primo programma dagli esempi',
          voce: 'Dalla scheda Progetti apriamo il Primo programma, quello del primo video.',
        }, async () => {
          await r.clic(p.locator('text="Progetti" >> visible=true').first());
          await r.pausa(600);
          await r.scorri('[data-ex="0"]');
          await r.clic('[data-ex="0"]');
          await r.pausa(1200);
        });

        await r.scena({
          scritta: 'Carica sulla scheda',
          voce: 'Carichiamo. Il programma resta nella memoria della scheda anche quando la si spegne.',
        }, async () => {
          await r.clic('#uploadBtn');
          await r.pausa(4000);
        });

        await r.scena({
          scritta: 'Accendi: il LED D13 della scheda lampeggia',
          voce: 'Accendiamo l\'interruttore nella pagina: i valori arrivano dalla scheda e il LED sulla UNO R4 lampeggia davvero.',
        }, async () => {
          await r.clic('.ctl[data-ctl="sw"]');
          await r.pausa(1500);
          await r.muovi({ x: 700, y: 520 });
        });

        await r.scena({
          scritta: 'Simulatore → firmware → Wi-Fi → scheda vera',
          capitolo: 'Riassunto',
          voce: 'Riassumendo: si prova nel simulatore, con l\'app si carica il firmware, si imposta il Wi-Fi e da qui in poi si programma la scheda vera dal browser.',
        }, async () => {
          await r.pausa(500);
        });
      },
    },
  },
};

// definizioni delle figure del manuale (coordinate in pixel delle schermate)
const A = (s, at, dx = 0, dy = 0) => ({ s, at, dx, dy });
const FIG = {
  // ---------------- app per Windows (900x660) ----------------
  app_firmware: { img: 'app_firmware', crop: [0, 0, 900, 640], notes: [
    { t: 'num', n: 1, at: [20, 134] }, { t: 'num', n: 2, at: [20, 159] }, { t: 'num', n: 3, at: [20, 230] }, { t: 'num', n: 4, at: [20, 283] },
    { t: 'arrow', from: [620, 215], to: [520, 162], text: 'la scheda si riconosce da sola', size: 5 },
    { t: 'box', r: [24, 272, 106, 24], pad: 5 },
    { t: 'arrow', from: [300, 350], to: [150, 290], text: 'un clic... e aspetta la fine', anchor: 'start', tdx: 2, tdy: 6 },
    { t: 'text', at: [460, 470], text: 'qui compaiono i messaggi:\nalla fine "Firmware caricato correttamente"', anchor: 'middle', size: 4.6 } ] },
  app_trova: { img: 'app_trova', crop: [0, 0, 900, 640], notes: [
    { t: 'box', r: [24, 148, 834, 22], pad: 4 },
    { t: 'arrow', from: [450, 280], to: [400, 175], text: 'la tua scheda: nome, indirizzo, stato', size: 5 },
    { t: 'num', n: 1, at: [74, 540] }, { t: 'num', n: 2, at: [345, 540] }, { t: 'num', n: 3, at: [830, 540] },
    { t: 'text', at: [450, 380], text: 'doppio clic sulla riga = apre la pagina', anchor: 'middle', size: 4.6 } ] },
  app_monitor: { img: 'app_monitor', crop: [0, 0, 900, 640], notes: [
    { t: 'num', n: 1, at: [345, 124] }, { t: 'num', n: 2, at: [823, 124] }, { t: 'box', r: [598, 160, 262, 290], pad: 4 },
    { t: 'arrow', from: [300, 350], to: [590, 300], text: 'comandi pronti:\nbasta un clic', anchor: 'middle', tdy: -8 },
    { t: 'box', r: [600, 424, 70, 22], pad: 3 }, { t: 'arrow', from: [500, 470], to: [598, 440], text: 'PIN dimenticato? → 1234', anchor: 'end', tdx: 0, tdy: 6 } ] },

  // ---------------- pagina: giro completo ----------------
  tour: { img: 'tour', w: 172, notes: [
    { t: 'num', n: 1, at: A('#hostLbl', 'b', 0, 30) }, { t: 'num', n: 2, at: A('#tabs', 'b', 0, 30) },
    { t: 'num', n: 3, at: A('#runPill', 'b', 0, 40) }, { t: 'num', n: 4, at: A('#uploadBtn', 'b', 0, 40) }, { t: 'num', n: 5, at: A('#lockBtn', 'b', 0, 40) },
    { t: 'num', n: 6, at: A('#palette', 'c', 120, -200) }, { t: 'num', n: 7, at: A('#toolbar', 'c', 150, 70) },
    { t: 'num', n: 8, at: [1200, 420] }, { t: 'num', n: 9, at: A('#inspector', 'c', 0, -420) },
    { t: 'num', n: 10, at: A('#status', 't', -300, -30) }, { t: 'num', n: 11, at: A('#mtxView', 'tl', -30, -10) },
    { t: 'arrow', from: [1150, 1450], to: [880, 800], text: 'filo verde = vale 1 (acceso)', size: 5.2, tdy: 5 },
    { t: 'box', s: '#uploadBtn', pad: 8 } ] },

  // ---------------- blocco selezionato, menu, guida ----------------
  props: { img: 'props', crop: [460, 110, 2420, 1010], w: 170, notes: [
    { t: 'box', r: [1405, 750, 350, 170], pad: 10 },
    { t: 'arrow', from: [1300, 400], to: [1500, 740], text: 'clic sul blocco = lo selezioni', size: 5.2 },
    { t: 'box', r: [2330, 598, 520, 64], pad: 8 },
    { t: 'arrow', from: [2080, 900], to: [2320, 650], text: 'tempo in millisecondi\n5000 = 5 secondi', anchor: 'middle', tdy: 2 },
    { t: 'ellipse', r: [2528, 164, 50, 44], pad: 10 },
    { t: 'arrow', from: [2200, 250], to: [2510, 190], text: 'guida del blocco', anchor: 'end', tdx: 0, tdy: 2 } ] },
  menu: { img: 'menu', crop: [980, 600, 1300, 900], w: 120, notes: [
    { t: 'box', s: '.ctxm', pad: 6 },
    { t: 'arrow', from: [1250, 700], to: [1600, 860], text: 'tasto destro\nsul blocco', anchor: 'middle', tdy: -4 } ] },
  help: { img: 'help', crop: '#dlg', pad: 20, w: 130 },
  dlg_nuovo: { img: 'dlg_nuovo', crop: '#dlg', pad: 20, w: 110 },
  dlg_pin: { img: 'dlg_pin', crop: '#dlg', pad: 20, w: 100 },
  dlg_update: { img: 'dlg_update', crop: '#dlg', pad: 20, w: 105 },

  // ---------------- LADDER ----------------
  lad: { img: 'lad', crop: [0, 100, 1580, 700], w: 172, notes: [
    { t: 'box', r: [16, 120, 1240, 66], pad: 6 }, { t: 'text', at: [640, 232 - 10], text: '', anchor: 'middle' },
    { t: 'num', n: 1, at: [640, 206] }, { t: 'num', n: 2, at: A('#ladBox', 'b', 0, 24) }, 
    { t: 'num', n: 4, at: [880, 355] },
    { t: 'box', r: [346, 504, 300, 122], pad: 4 },
    { t: 'arrow', from: [900, 640], to: [660, 580], text: 'box temporizzatore TP', anchor: 'start', tdx: 4, tdy: 5 },
    { t: 'arrow', from: [230, 648], to: [196, 540], text: 'indirizzo e nome', anchor: 'middle', tdy: 6 },
    { t: 'arrow', from: [1300, 640], to: [1110, 575], text: 'bobina', anchor: 'start', tdx: 3, tdy: 5 } ] },
  lad_marcia: { img: 'lad_marcia', crop: [14, 364, 1480, 510], w: 150 },
  lad_lampeggio: { img: 'lad_lampeggio', crop: [14, 364, 1300, 960], w: 150 },
  lad_contapezzi: { img: 'lad_contapezzi', crop: [14, 364, 1300, 790], w: 150 },
  lad_scale: { img: 'lad', crop: [14, 364, 1300, 380], w: 150 },

  // ---------------- GRAFCET ----------------
  graf_tr: { img: 'graf_tr', crop: [0, 105, 2880, 1000], w: 172, notes: [
    { t: 'num', n: 1, at: [144, 205] }, { t: 'num', n: 2, at: [809, 205] },
    { t: 'box', r: [150, 262, 110, 110], pad: 6 }, { t: 'arrow', from: [560, 330], to: [275, 320], text: 'tappa iniziale\n(doppio quadrato)', anchor: 'start', tdx: 3, tdy: -3 },
    { t: 'arrow', from: [800, 480], to: [560, 445], text: 'transizione T1\ncon la sua recettività', anchor: 'start', tdx: 3, tdy: 0 },
    { t: 'arrow', from: [900, 640], to: [690, 570], text: 'azione N: "Avanti" acceso\nfinché la tappa 2 è attiva', anchor: 'start', tdx: 3, tdy: 3 },
    { t: 'arrow', from: [560, 900], to: [110, 590], text: 'rinvio alla tappa iniziale', anchor: 'start', tdx: 3, tdy: 4, bend: -0.15 },
    { t: 'box', r: [2330, 520, 520, 70], pad: 6 }, { t: 'arrow', from: [2050, 760], to: [2320, 560], text: 'qui scrivi\nla recettività', anchor: 'middle', tdy: 3 } ] },
  graf_st: { img: 'graf_st', crop: [1600, 105, 1280, 1000], w: 90 },
  graf_carrello: { img: 'graf', crop: 'auto', pad: 30, w: 70 },
  graf_semaforo: { img: 'graf_semaforo', crop: 'auto', pad: 30, w: 70 },

  // ---------------- script ----------------
  st: { img: 'st', crop: [520, 110, 2360, 1250], w: 170, notes: [ { t: 'box', s: '#inspector', pad: -10 } ] },
  c: { img: 'c', crop: [520, 110, 2360, 1250], w: 170, notes: [ { t: 'box', s: '#inspector', pad: -10 } ] },

  // ---------------- FBD esempi ----------------
  fbd_primo: { img: 'fbd_primo', crop: 'auto', pad: 40, w: 150 },
  fbd_marcia: { img: 'tour', crop: 'auto', pad: 30, w: 150 },
  fbd_scale: { img: 'fbd_scale', crop: 'auto', pad: 30, w: 150 },
  fbd_contapezzi: { img: 'fbd_contapezzi', crop: 'auto', pad: 30, w: 155 },
  fbd_semaforo: { img: 'fbd_semaforo', crop: 'auto', pad: 30, w: 160 },
  fbd_termostato: { img: 'fbd_termostato', crop: 'auto', pad: 30, w: 160 },
  fbd_dimmer: { img: 'fbd_dimmer', crop: 'auto', pad: 30, w: 110 },
  fbd_matrice: { img: 'fbd_matrice', crop: [842, 640, 1450, 1010], w: 120, notes: [
    { t: 'box', s: '#mtxView', pad: 6 }, { t: 'arrow', from: [1700, 900], to: [1995, 1480], text: 'anteprima della matrice\n(come sulla scheda)', anchor: 'middle', tdy: -3 } ] },

  // ---------------- schede della pagina ----------------
  pins: { img: 'pins_r4', crop: [330, 120, 2220, 1620], w: 172, notes: [
    { t: 'hl', r: [370, 580, 2150, 50] },
    { t: 'arrow', from: [1650, 480], to: [1175, 590], text: 'indirizzo per LADDER e GRAFCET', anchor: 'start', tdx: 3, tdy: 2 },
    { t: 'box', r: [1091, 1015, 338, 212], pad: 6 },
    { t: 'arrow', from: [1750, 1300], to: [1300, 1115], text: 'modo del pin: pull-up per i pulsanti', anchor: 'start', tdx: 3, tdy: 4 },
    { t: 'arrow', from: [1750, 1390], to: [1300, 1185], text: 'nome (etichetta)', anchor: 'start', tdx: 3, tdy: 4 },
    { t: 'ellipse', r: [1330, 1034, 110, 34], pad: 8 }, { t: 'arrow', from: [1800, 960], to: [1450, 1050], text: 'stato dal vivo', anchor: 'start', tdx: 3, tdy: 0 },
    { t: 'box', r: [760, 1615, 160, 56], pad: 6 }, { t: 'arrow', from: [1300, 1560], to: [930, 1640], text: 'prova l\'uscita a mano', anchor: 'start', tdx: 3, tdy: 0 },
    { t: 'ellipse', r: [1505, 1036, 40, 34], pad: 6 }, { t: 'text', at: [1560, 1000], text: '~ = PWM', size: 4.6 } ] },
  images: { img: 'images', crop: [340, 120, 2230, 1580], w: 160, notes: [
    { t: 'num', n: 1, at: A('#imgIcons', 'r', 40, 0) }, { t: 'num', n: 2, at: A('#imgTools', 'l', -30, 0) },
    { t: 'num', n: 3, at: A('#imgCanvasWrap', 'l', -30, 0) }, { t: 'num', n: 4, at: A('#imgFrames', 'l', -30, 0) } ] },
  projects: { img: 'projects', crop: [330, 120, 2220, 1180], w: 172, notes: [
    { t: 'num', n: 1, at: [360, 263] }, { t: 'num', n: 2, at: [360, 576] }, { t: 'num', n: 3, at: [360, 970] },
    { t: 'box', r: [2000, 945, 105, 52], pad: 6 }, { t: 'arrow', from: [1700, 1090], to: [2010, 995], text: 'cambia il nome', anchor: 'middle', tdy: 6 },
    { t: 'box', r: [870, 1140, 230, 62], pad: 6 }, { t: 'arrow', from: [1400, 1260], to: [1110, 1180], text: 'scegli lo slot e salva', anchor: 'start', tdx: 3, tdy: 6 },
    { t: 'ellipse', r: [1140, 560, 170, 34], pad: 6 } ] },
  settings_ap: { img: 'settings_ap', crop: [340, 1090, 2200, 610], w: 172, notes: [
    { t: 'hl', r: [370, 1190, 1250, 44] },
    { t: 'num', n: 1, at: [1260, 1332] }, { t: 'num', n: 2, at: [930, 1429] }, { t: 'num', n: 3, at: [640, 1517] },
    { t: 'arrow', from: [1800, 1400], to: [1270, 1340], text: '"Cerca reti" e scegli\nil Wi-Fi della scuola', anchor: 'middle', tdy: 3 } ] },
  settings_r4: { img: 'settings_r4', crop: [330, 120, 2220, 2400], w: 150, notes: [
    { t: 'num', n: 1, at: A('#setTeacher', 'tr', -40, 40) }, { t: 'num', n: 2, at: A('#setBoard', 'tr', -40, 40) },
    { t: 'num', n: 3, at: A('#setWifiCard', 'tr', -40, 40) }, { t: 'num', n: 4, at: A('#setUpdCard', 'tr', -40, 40) },
    { t: 'num', n: 5, at: A('#setOled', 'tr', -40, 40) } ] },
  settings_mega: { img: 'settings_mega', crop: [330, 590, 2220, 480], w: 150 },
  ap_banner: { img: 'ap_banner', crop: [400, 0, 2480, 310], w: 172, notes: [ { t: 'box', s: '#banner', pad: 4 } ] },
  pins_mega: { img: 'pins_mega', crop: [330, 1070, 2220, 900], w: 105 },
};

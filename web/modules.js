// FILE GENERATO da tools/build_web.py: blocchi dei moduli (PlcBlocchi/mod_*.cpp)
const MODULE_DEFS = [
 {
  "t": 71,
  "name": "Sensore DHT11/DHT22",
  "short": "DHT",
  "desc": "Temperatura e umidità da un sensore DHT11 o DHT22 (AM2302). I valori sono in decimi: 235 = 23,5 °C, 482 = 48,2 %. Aggiornati ogni 2 secondi. Collega il pin dati con una resistenza da 10 kΩ verso 5 V (i moduli su basetta ce l'hanno già).",
  "ins": [],
  "outs": [
   "T×10",
   "U×10"
  ],
  "params": [
   {
    "k": 0,
    "t": "mpin",
    "label": "Pin dati"
   },
   {
    "k": 1,
    "t": "sel",
    "label": "Modello",
    "opts": [
     "DHT11",
     "DHT22 / AM2302"
    ],
    "def": 1
   }
  ],
  "sim": [
   235,
   482
  ],
  "file": "mod_dht.cpp"
 },
 {
  "t": 72,
  "name": "Sonda DS18B20",
  "short": "DS18",
  "desc": "Temperatura da una sonda DS18B20 (anche impermeabile), in decimi di grado: 215 = 21,5 °C. Aggiornata circa ogni secondo. Serve una resistenza da 4,7 kΩ tra il pin dati e 5 V. OK vale 1 se il sensore risponde.",
  "ins": [],
  "outs": [
   "T×10",
   "OK"
  ],
  "params": [
   {
    "k": 0,
    "t": "mpin",
    "label": "Pin dati"
   }
  ],
  "sim": [
   215,
   1
  ],
  "file": "mod_ds18b20.cpp"
 },
 {
  "t": 74,
  "name": "Encoder rotativo",
  "short": "ENC",
  "desc": "Conta gli scatti di una manopola encoder (es. KY-040): POS aumenta girando in un senso e diminuisce nell'altro, restando fra MIN e MAX (utile per scegliere una voce di menu o regolare un valore). Un fronte su CARICA porta POS al valore di VAL. DIR = +1 o −1, l'ultimo verso di rotazione. Il pin A deve essere 2, 3, 18 o 19; il pulsante della manopola si collega come ingresso digitale normale.",
  "ins": [
   {
    "n": "CARICA"
   },
   {
    "n": "VAL",
    "fb": true,
    "def": 0
   },
   {
    "n": "MIN",
    "fb": true,
    "def": -100000
   },
   {
    "n": "MAX",
    "fb": true,
    "def": 100000
   }
  ],
  "outs": [
   "POS",
   "DIR"
  ],
  "params": [
   {
    "k": 4,
    "t": "mpin",
    "label": "Pin A / CLK (2, 3, 18, 19)",
    "only": [
     2,
     3,
     18,
     19
    ],
    "pack": [
     0,
     0,
     8
    ]
   },
   {
    "k": 5,
    "t": "mpin",
    "label": "Pin B / DT",
    "pack": [
     0,
     8,
     8
    ]
   },
   {
    "k": 6,
    "t": "sel",
    "label": "Impulsi per scatto",
    "opts": [
     "1 (conta ogni fronte)",
     "2 (KY-040 e simili)"
    ],
    "def": 1,
    "pack": [
     0,
     16,
     8
    ]
   }
  ],
  "sim": [
   0,
   0
  ],
  "lad": {
   "power": 0
  },
  "file": "mod_encoder.cpp"
 },
 {
  "t": 73,
  "name": "Distanza ultrasuoni HC-SR04",
  "short": "SR04",
  "desc": "Misura la distanza di un ostacolo in millimetri, circa 16 volte al secondo, finché EN vale 1. MAX = distanza massima in cm (più è grande, più a lungo il PLC aspetta l'eco: circa 6 ms per metro). OK vale 1 se è arrivato l'eco (ostacolo entro MAX).",
  "ins": [
   {
    "n": "EN",
    "fb": true,
    "def": 1
   },
   {
    "n": "MAX",
    "fb": true,
    "def": 200,
    "unit": "cm"
   }
  ],
  "outs": [
   "MM",
   "OK"
  ],
  "params": [
   {
    "k": 4,
    "t": "mpin",
    "label": "Pin TRIG",
    "pack": [
     2,
     0,
     8
    ]
   },
   {
    "k": 5,
    "t": "mpin",
    "label": "Pin ECHO",
    "pack": [
     2,
     8,
     8
    ]
   }
  ],
  "sim": [
   350,
   1
  ],
  "lad": {
   "power": 0
  },
  "file": "mod_hcsr04.cpp"
 },
 {
  "t": 79,
  "name": "Orologio RTC DS3231",
  "short": "RTC",
  "desc": "Legge ora e data da un modulo orologio DS3231 (con batteria: tiene l'ora anche a scheda spenta). Collegalo ai pin SDA/SCL come il display. Scegli cosa mettere nelle due uscite; l'ora si imposta in Impostazioni → Orologio.",
  "ins": [],
  "outs": [
   "A",
   "B"
  ],
  "params": [
   {
    "k": 0,
    "t": "sel",
    "label": "Uscita A",
    "opts": [
     "ore",
     "minuti",
     "secondi",
     "giorno",
     "mese",
     "anno",
     "giorno della settimana (1 = lunedì)",
     "ora come HHMM (es. 1435)",
     "minuti dalla mezzanotte",
     "secondi dalla mezzanotte"
    ],
    "def": 0
   },
   {
    "k": 1,
    "t": "sel",
    "label": "Uscita B",
    "opts": [
     "ore",
     "minuti",
     "secondi",
     "giorno",
     "mese",
     "anno",
     "giorno della settimana (1 = lunedì)",
     "ora come HHMM (es. 1435)",
     "minuti dalla mezzanotte",
     "secondi dalla mezzanotte"
    ],
    "def": 1
   }
  ],
  "sim": [
   10,
   30
  ],
  "lad": {
   "power": null
  },
  "file": "mod_rtc.cpp"
 },
 {
  "t": 75,
  "name": "Servomotore",
  "short": "SERVO",
  "desc": "Porta un servomotore (es. SG90) all'angolo ANGOLO (0–180°): collega un filo all'ingresso (cursore, potenziometro scalato, script...) oppure lascia il valore fisso. VEL = velocità in gradi al secondo (0 = scatto immediato). EN = 0 spegne gli impulsi e il servo resta libero. Uscite: POS = angolo attuale, OK = 1 quando è arrivato. Al massimo 4 servo; con i servo il PWM sui pin 44, 45 e 46 non funziona.",
  "ins": [
   {
    "n": "ANGOLO",
    "fb": true,
    "def": 90,
    "unit": "°"
   },
   {
    "n": "VEL",
    "fb": true,
    "def": 0,
    "unit": "°/s"
   },
   {
    "n": "EN",
    "fb": true,
    "def": 1
   }
  ],
  "outs": [
   "POS",
   "OK"
  ],
  "params": [
   {
    "k": 3,
    "t": "mpin",
    "label": "Pin del segnale"
   }
  ],
  "sim": [
   90,
   1
  ],
  "lad": {
   "power": 2
  },
  "file": "mod_servo.cpp"
 },
 {
  "t": 76,
  "name": "Motore passo-passo",
  "short": "STEP",
  "desc": "Muove un motore passo-passo. Modo velocità: gira a VEL passi al secondo (negativo = al contrario) finché EN vale 1. Modo posizione: va alla posizione TARGET (in passi) alla velocità VEL e si ferma lì. AZZERA mette POS a 0 (lo zero, es. su un finecorsa). FERMO vale 1 quando il motore non si muove. Con il 28BYJ-48 (scheda ULN2003) servono 4 pin, con un driver STEP/DIR bastano i primi due. I passi sono generati dal ciclo del PLC: tieni VEL entro qualche centinaio.",
  "ins": [
   {
    "n": "EN",
    "fb": true,
    "def": 1
   },
   {
    "n": "VEL",
    "fb": true,
    "def": 300,
    "unit": "p/s"
   },
   {
    "n": "AZZERA"
   },
   {
    "n": "TARGET",
    "fb": true,
    "def": 0
   }
  ],
  "outs": [
   "POS",
   "FERMO"
  ],
  "params": [
   {
    "k": 8,
    "t": "sel",
    "label": "Modo",
    "opts": [
     "Velocità (gira finché EN)",
     "Posizione (va a TARGET)"
    ],
    "def": 0,
    "pack": [
     2,
     29,
     1
    ]
   },
   {
    "k": 9,
    "t": "sel",
    "label": "Collegamento",
    "opts": [
     "28BYJ-48 + ULN2003 (4 pin)",
     "Driver STEP/DIR"
    ],
    "def": 0,
    "pack": [
     2,
     28,
     1
    ]
   },
   {
    "k": 4,
    "t": "mpin",
    "label": "IN1 / STEP",
    "pack": [
     2,
     0,
     7
    ]
   },
   {
    "k": 5,
    "t": "mpin",
    "label": "IN2 / DIR",
    "pack": [
     2,
     7,
     7
    ]
   },
   {
    "k": 6,
    "t": "mpin",
    "label": "IN3 (solo ULN2003)",
    "pack": [
     2,
     14,
     7
    ],
    "opt": true
   },
   {
    "k": 7,
    "t": "mpin",
    "label": "IN4 (solo ULN2003)",
    "pack": [
     2,
     21,
     7
    ],
    "opt": true
   }
  ],
  "sim": [
   0,
   1
  ],
  "lad": {
   "power": 0
  },
  "file": "mod_stepper.cpp"
 },
 {
  "t": 70,
  "name": "Suono (tone)",
  "short": "TONE",
  "desc": "Suona una nota su un buzzer passivo o un piccolo altoparlante (con resistenza da 100 Ω). Finché EN vale 1 suona alla frequenza FREQ in Hz (es. 440 = La). Mentre suona, il PWM sui pin 9 e 10 non funziona.",
  "ins": [
   {
    "n": "EN",
    "fb": true,
    "def": 1
   },
   {
    "n": "FREQ",
    "fb": true,
    "def": 440,
    "unit": "Hz"
   }
  ],
  "outs": [],
  "params": [
   {
    "k": 2,
    "t": "mpin",
    "label": "Pin del buzzer"
   }
  ],
  "lad": {
   "power": 0
  },
  "file": "mod_tone.cpp"
 },
 {
  "t": 77,
  "name": "Striscia LED RGB (WS2812)",
  "short": "WS2812",
  "desc": "Configura una striscia o un anello di LED RGB WS2812 (NeoPixel) con fino a 60 LED; LUMIN. è la luminosità generale (0–255). I colori si impostano con i blocchi \"Colore LED\". Alimenta i LED a 5 V (per molti LED con alimentatore separato).",
  "ins": [
   {
    "n": "LUMIN.",
    "fb": true,
    "def": 60
   }
  ],
  "outs": [],
  "params": [
   {
    "k": 1,
    "t": "mpin",
    "label": "Pin dati (DIN)"
   },
   {
    "k": 2,
    "t": "num",
    "label": "Numero di LED (1–60)",
    "def": 8,
    "min": 1,
    "max": 60
   }
  ],
  "lad": {
   "power": null
  },
  "file": "mod_ws2812.cpp"
 },
 {
  "t": 78,
  "name": "Colore LED",
  "short": "RGB",
  "desc": "Dà un colore (R, G, B da 0 a 255) al LED numero N della striscia WS2812 (il primo è 0). Con N = −1 colora tutti i LED.",
  "ins": [
   {
    "n": "N",
    "fb": true,
    "def": -1
   },
   {
    "n": "R",
    "fb": true,
    "def": 255
   },
   {
    "n": "G",
    "fb": true,
    "def": 0
   },
   {
    "n": "B",
    "fb": true,
    "def": 0
   }
  ],
  "outs": [],
  "params": [],
  "lad": {
   "power": null
  },
  "file": "mod_ws2812.cpp"
 }
];

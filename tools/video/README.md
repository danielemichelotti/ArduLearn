# Video tutorial di ArduLearn

Script che registrano i video tutorial direttamente dal simulatore in `web/`, sempre uguali:
quando la pagina cambia, si rigira il video con un comando.

## Cosa serve (una volta sola)

```
winget install Gyan.FFmpeg --scope user
cd tools/video
npm install
npx playwright install chromium
```

## Girare un video

```
node gira.mjs 01                    registra e monta il video 1
node gira.mjs 01 --monta            rimonta soltanto (scritte, cartelli, audio) senza registrare di nuovo
node gira.mjs 01 --monta --voce voce.wav [--musica sottofondo.mp3]
```

I file finiti vanno in `tools/video/out/<video>/` (esclusa dal repository, i video non si mettono su Git):

| File | Contenuto |
|---|---|
| `<video>.mp4` | 1920x1080, 30 fps, H.264; audio muto se non si passa `--voce` |
| `<video>-copione.md` | cosa dire e quando, scena per scena |
| `<video>.srt` | sottotitoli (il testo del copione, con i tempi) |
| `<video>-youtube.txt` | titolo, descrizione con i capitoli, tag |
| `<video>.md` | proposta di scheda per `sito/src/video/` (bozza, senza link) |

## Come è fatto

- `video/NN-nome.mjs`: un video = titoli, testi per YouTube e sito, e le **scene**. Ogni scena ha la
  scritta in sovrimpressione, la frase del copione ed eventualmente il capitolo; dura almeno quanto serve
  a leggere la frase con calma (2,4 parole al secondo).
- `lib/regia.mjs`: apre Chromium a 1280x720 con scala 1,5 (video a 1920x1080 ben leggibile), mostra un
  cursore finto con l'anello al clic, evidenzia le zone della pagina e registra i fotogrammi via CDP.
- `lib/montaggio.mjs`: con ffmpeg unisce intro, registrazione e chiusura, aggiunge le scritte (ASS) e
  l'eventuale audio, e scrive sottotitoli, copione e capitoli.
- `lib/server.mjs`: server locale per `web/` su una porta libera (non disturba altri server aperti).

## Registrare la voce

1. Apri il copione e il video muto, uno accanto all'altro.
2. Registra la voce leggendo ogni frase all'inizio della sua scena (qualsiasi registratore, file WAV o MP3,
   che parta insieme al video, cioè dall'intro).
3. `node gira.mjs 01 --monta --voce voce.wav`

Se una frase non ci sta, allunga il testo nella scena o aggiungi `await r.pausa(...)`: la durata si adegua.

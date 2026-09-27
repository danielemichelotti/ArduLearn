# Sito di ArduLearn

Sito statico costruito con [Eleventy](https://www.11ty.dev/) e pubblicato su GitHub Pages dalla
GitHub Action `.github/workflows/sito.yml` a ogni modifica su `main`.

| Pagina | Da dove prende i contenuti |
|---|---|
| Home | `src/_data/home.json` (modificabile dal pannello) e le novità di `aggiornamenti/manifest.json` |
| Prova online | `web/index.html` del progetto, aperto con `?demo&sito` (simulatore senza scheda) |
| Training | i video in `src/video/*.md` (link YouTube, caricamento al clic, youtube-nocookie) |
| Manuale | le stesse pagine del PDF, in `docs/manuale/` |
| Scarica | i link all'ultima versione su GitHub (zip, exe) e i PDF di `docs/` |
| Chi sono, sezioni nuove | `src/chi-sono.md`, `src/pagine/*.md` |

## Pannello di amministrazione

Indirizzo: `/admin/` del sito (Sveltia CMS, configurazione in `src/admin/config.yml`). Si entra con un
**token di GitHub**: ogni salvataggio diventa una modifica nel repository e il sito si aggiorna da solo.

## Provare in locale

```bash
cd sito
npm install
npx @11ty/eleventy --serve
```

## Dominio

Il sito è su **https://ardulearn.org**: il dominio si imposta nelle impostazioni di GitHub Pages
(pubblicazione tramite Actions, workflow `.github/workflows/sito.yml`), quindi niente file CNAME e il sito
sta nella radice. Il contatore delle visite parte solo sui domini elencati in `src/_data/sito.json`
(`domini_contatore`).

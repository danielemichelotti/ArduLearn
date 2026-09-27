// Proposta di scheda per la sezione Training del sito (sito/src/video/*.md).
// Resta una bozza finché il video non è su YouTube: allora si mette il link e si toglie "bozza".
export function schedaSito(video) {
  const oggi = new Date().toISOString().slice(0, 10);
  const q = s => JSON.stringify(s);
  return `---
title: ${q(video.titolo)}
youtube: ""
descrizione: ${q(video.sito.descrizione)}
categoria: ${video.sito.categoria}
ordine: ${video.sito.ordine}
date: ${oggi}
bozza: true
---
`;
}

// Impaginazione: tiene insieme le cose che non devono separarsi fra due pagine.
(function () {
  const MM = 96 / 25.4;                       // px per mm
  const PAGE_H = 297 - 16 - 18 - 4;          // altezza utile della pagina (mm), con un margine
  document.body.style.width = '176mm';       // misure come in stampa
  const h = el => el.getBoundingClientRect().height / MM;
  const wrap = (els, cls = 'keep') => {
    const d = document.createElement('div'); d.className = cls;
    els[0].before(d); els.forEach(e => d.appendChild(e)); return d;
  };
  const next = el => { let n = el.nextElementSibling; while (n && (n.tagName === 'SCRIPT')) n = n.nextElementSibling; return n; };
  const fits = els => els.reduce((s, e) => s + h(e), 0) < PAGE_H * 0.92;

  // 1. tabelle: intestazione ripetuta su ogni pagina; le tabelle corte non si spezzano
  document.querySelectorAll('table').forEach(t => {
    const first = t.querySelector('tr');
    if (first && first.querySelector('th') && !t.tHead) { const th = t.createTHead(); th.appendChild(first); }
    if (h(t) < PAGE_H * 0.6) t.style.breakInside = 'avoid';
  });
  // 2. esercizi: intestazione, obiettivo, consegna e materiale insieme; ogni soluzione con la sua figura
  document.querySelectorAll('.ex').forEach(ex => {
    const kids = [...ex.children];
    const firstSol = kids.findIndex(k => k.classList.contains('sol'));
    if (firstSol > 0) wrap(kids.slice(0, firstSol));
    [...ex.querySelectorAll(':scope > .sol')].forEach(sol => {
      const grp = [sol]; let n = next(sol);
      if (n && (n.classList.contains('figwrap') || n.classList.contains('two'))) { grp.push(n); n = next(n); }
      if (n && n.tagName === 'P' && fits([...grp, n])) grp.push(n);
      wrap(grp);
    });
    ex.querySelectorAll(':scope > .lab').forEach(l => { const n = next(l); if (n) wrap([l, n]); });
  });
  // 3. figura + legenda numerata insieme (se entrano in una pagina)
  document.querySelectorAll('.figwrap').forEach(f => {
    const n = next(f);
    if (n && n.matches('ol.legend') && fits([f, n])) wrap([f, n]);
  });
  // 4. titoli attaccati a ciò che segue (e un paragrafo che finisce con ":" attaccato alla tabella/lista)
  document.querySelectorAll('h2, h3').forEach(t => {
    if (t.closest('.keep, .ex, .two')) return;
    const grp = [t]; let n = next(t);
    if (!n) return;
    grp.push(n);
    if (n.classList.contains('step')) { let m = next(n); while (m && (m.classList.contains('step') || m.classList.contains('box')) && fits([...grp, m])) { grp.push(m); m = next(m); } }
    else if (n.tagName === 'P' && /:\s*$/.test(n.textContent)) { const m = next(n); if (m && fits([...grp, m])) grp.push(m); }
    else if (n.tagName === 'P' || n.classList.contains('postit')) { const m = next(n); if (m && h(n) < 25 && fits([...grp, m])) grp.push(m); }
    if (fits(grp)) wrap(grp); else wrap([t, n].slice(0, h(n) < 60 ? 2 : 1));
  });
  // 5. sequenze di passi numerati tutte insieme
  document.querySelectorAll('.step').forEach(st => {
    if (st.closest('.keep')) return;
    const p = st.previousElementSibling; if (p && p.classList.contains('step')) return;
    const grp = [st]; let m = next(st);
    while (m && m.classList.contains('step')) { grp.push(m); m = next(m); }
    if (grp.length > 1 && fits(grp)) wrap(grp);
  });
  document.querySelectorAll('p').forEach(p => { if (/:\s*$/.test(p.textContent) && !p.closest('.keep')) { const n = next(p); if (n && n.matches('table, ul, ol') && fits([p, n])) wrap([p, n]); } });
  document.body.style.width = '';
})();

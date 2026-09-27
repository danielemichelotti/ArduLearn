// Figure annotate "a mano" sopra le schermate. Coordinate in pixel dell'immagine originale.
// <div class="fig" data-f="nome" data-w="170"></div>  →  FIG.nome = { img, crop, notes }
const RED = '#d6336c', INK = '#c92a2a';
function rectOf(img, s, pad = 0) {
  let r = Array.isArray(s) ? s : (s === 'auto' ? AUTO[img] : RECTS[img]?.[s]);
  if (!r) { console.warn('rect mancante', img, s); return [0, 0, 10, 10]; }
  return [r[0] - pad, r[1] - pad, r[2] + 2 * pad, r[3] + 2 * pad];
}
function pt(img, p) {                       // punto: [x,y] | {s, at:'l|r|t|b|c|tl|tr|bl|br', dx, dy}
  if (Array.isArray(p)) return p;
  const [x, y, w, h] = rectOf(img, p.s, p.pad || 0);
  const at = p.at || 'c';
  const px = at.includes('l') ? x : at.includes('r') ? x + w : x + w / 2;
  const py = at.includes('t') ? y : at.includes('b') ? y + h : y + h / 2;
  return [px + (p.dx || 0), py + (p.dy || 0)];
}
// tratto un po' irregolare, come disegnato a mano
function wobbleRect(x, y, w, h, k) {
  const j = v => v + (Math.random() - 0.5) * 3 * k;
  const r = Math.min(14 * k, h / 2, w / 2);
  return `M${j(x + r)},${j(y)} L${j(x + w - r)},${j(y)} Q${x + w},${y} ${j(x + w)},${j(y + r)} L${j(x + w)},${j(y + h - r)} Q${x + w},${y + h} ${j(x + w - r)},${j(y + h)} L${j(x + r)},${j(y + h)} Q${x},${y + h} ${j(x)},${j(y + h - r)} L${j(x)},${j(y + r)} Q${x},${y} ${x + r + 2 * k},${y - 1.5 * k}`;
}
function arrowPath(a, b, k, bend = 0.18) {
  const [x1, y1] = a, [x2, y2] = b;
  const mx = (x1 + x2) / 2, my = (y1 + y2) / 2, dx = x2 - x1, dy = y2 - y1;
  const cx = mx - dy * bend, cy = my + dx * bend;
  const ang = Math.atan2(y2 - cy, x2 - cx), L = 11 * k;
  const h1 = [x2 - L * Math.cos(ang - 0.45), y2 - L * Math.sin(ang - 0.45)], h2 = [x2 - L * Math.cos(ang + 0.45), y2 - L * Math.sin(ang + 0.45)];
  return `M${x1},${y1} Q${cx},${cy} ${x2},${y2} M${h1} L${x2},${y2} L${h2}`;
}
const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
function textEl(x, y, text, k, anchor = 'start', color = INK, size = 5) {
  const lines = String(text).split('\n'), fs = size * k;
  return `<text x="${x}" y="${y}" font-size="${fs}" fill="${color}" text-anchor="${anchor}" class="hand">${lines.map((l, i) => `<tspan x="${x}" dy="${i ? fs * 1.05 : 0}">${esc(l)}</tspan>`).join('')}</text>`;
}
function renderFig(el) {
  const f = FIG[el.dataset.f];
  if (!f) { el.textContent = 'FIGURA MANCANTE: ' + el.dataset.f; return; }
  const [W, H] = SIZES[f.img];
  let [cx, cy, cw, ch] = f.crop === undefined ? [0, 0, W, H] : rectOf(f.img, f.crop, f.pad ?? 0);
  if (f.grow) { cx -= f.grow[3] || 0; cy -= f.grow[0] || 0; cw += (f.grow[1] || 0) + (f.grow[3] || 0); ch += (f.grow[0] || 0) + (f.grow[2] || 0); }
  const wmm = +(el.dataset.w || f.w || 170);
  const k = cw / wmm;                      // pixel per millimetro
  // parte visibile della schermata (intersezione col ritaglio)
  const ix = Math.max(cx, 0), iy = Math.max(cy, 0), iw = Math.min(cx + cw, W) - ix, ih = Math.min(cy + ch, H) - iy;
  let s = `<svg viewBox="${cx} ${cy} ${cw} ${ch}" style="width:${wmm}mm;height:${wmm * ch / cw}mm" xmlns="http://www.w3.org/2000/svg">
    <defs><clipPath id="c_${el.dataset.f}"><rect x="${ix}" y="${iy}" width="${iw}" height="${ih}" rx="${2.2 * k}"/></clipPath>
    <filter id="sh_${el.dataset.f}" x="-5%" y="-5%" width="110%" height="115%"><feDropShadow dx="0" dy="${0.6 * k}" stdDeviation="${0.9 * k}" flood-opacity=".18"/></filter></defs>
    <rect x="${ix}" y="${iy}" width="${iw}" height="${ih}" rx="${2.2 * k}" fill="#fff" filter="url(#sh_${el.dataset.f})"/>
    <image href="shots/${f.img}.jpg" x="0" y="0" width="${W}" height="${H}" clip-path="url(#c_${el.dataset.f})" preserveAspectRatio="none"/>
    <rect x="${ix}" y="${iy}" width="${iw}" height="${ih}" rx="${2.2 * k}" fill="none" stroke="#cbd5e1" stroke-width="${0.25 * k}"/>`;
  for (const n of f.notes || []) {
    const sw = (n.sw || 0.7) * k, col = n.color || RED;
    if (n.t === 'hl') { const [x, y, w, h] = rectOf(f.img, n.s ?? n.r, n.pad ?? 4); s += `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${1.5 * k}" fill="#ffe066" opacity=".45"/>`; }
    if (n.t === 'box') { const [x, y, w, h] = rectOf(f.img, n.s ?? n.r, n.pad ?? 8); s += `<path d="${wobbleRect(x, y, w, h, k)}" fill="none" stroke="${col}" stroke-width="${sw}" stroke-linecap="round"/>`; }
    if (n.t === 'arrow') {
      const a = pt(f.img, n.from), b = pt(f.img, n.to);
      s += `<path d="${arrowPath(a, b, k, n.bend ?? 0.18)}" fill="none" stroke="${col}" stroke-width="${sw}" stroke-linecap="round" stroke-linejoin="round"/>`;
      if (n.text) { const tp = n.tat ? pt(f.img, n.tat) : [a[0] + (n.tdx || 0) * k, a[1] + (n.tdy ?? -1.5) * k]; s += textEl(tp[0], tp[1], n.text, k, n.anchor || 'middle', n.tcolor || INK, n.size || 5); }
    }
    if (n.t === 'text') { const p = pt(f.img, n.at); s += textEl(p[0], p[1], n.text, k, n.anchor || 'start', n.color || INK, n.size || 5); }
    if (n.t === 'num') {
      const p = pt(f.img, n.at), r = 3.1 * k;
      s += `<circle cx="${p[0]}" cy="${p[1]}" r="${r}" fill="${col}" stroke="#fff" stroke-width="${0.5 * k}"/><text x="${p[0]}" y="${p[1] + 1.35 * k}" font-size="${3.8 * k}" text-anchor="middle" fill="#fff" font-weight="700" font-family="Segoe UI, Arial">${n.n}</text>`;
    }
    if (n.t === 'ellipse') { const [x, y, w, h] = rectOf(f.img, n.s ?? n.r, n.pad ?? 10); s += `<ellipse cx="${x + w / 2}" cy="${y + h / 2}" rx="${w / 2}" ry="${h / 2}" fill="none" stroke="${col}" stroke-width="${sw}" transform="rotate(-2 ${x + w / 2} ${y + h / 2})"/>`; }
  }
  el.innerHTML = s + '</svg>';
}
document.querySelectorAll('.fig').forEach(renderFig);

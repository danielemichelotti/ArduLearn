// Preferenze privacy del sito ArduLearn (nessun cookie: la scelta sta nella memoria del browser).
//  - statistiche: contatore anonimo delle visite, attivo salvo rinuncia (nessun cookie, IP non conservato)
//  - youtube: i video si caricano solo con il consenso (Google riceve l'IP)
// La scelta si ripropone dopo 6 mesi. Chiudere il banner con la X equivale a "Continua senza".
(function () {
  var KEY = "al-privacy", DURATA = 1000 * 60 * 60 * 24 * 182;
  var cfg = window.AL_PRIVACY || {};
  function leggi() { try { var p = JSON.parse(localStorage.getItem(KEY) || "null"); if (p && Date.now() - p.t < DURATA) return p; } catch (e) {} return null; }
  function salva(p) { p.t = Date.now(); try { localStorage.setItem(KEY, JSON.stringify(p)); } catch (e) {} window.dispatchEvent(new CustomEvent("al-privacy", { detail: p })); return p; }
  var scelta = leggi();
  window.alPrivacy = {
    stat: function () { var p = leggi(); return !p || p.stat !== false; },
    youtube: function () { var p = leggi(); return !!(p && p.yt); },
    consentiYoutube: function () { var p = leggi() || { stat: true }; p.yt = true; salva(p); },
    apri: apriPreferenze,
  };

  // contatore delle visite: solo sui domini pubblici, una volta per sessione, se non disattivato
  function conta() {
    if (!cfg.contatore || (cfg.domini || []).indexOf(location.hostname) < 0 || !window.alPrivacy.stat()) return;
    try { if (sessionStorage.getItem("al-visita")) return; sessionStorage.setItem("al-visita", "1"); } catch (e) {}
    try { fetch(cfg.contatore, { mode: "no-cors", credentials: "omit", keepalive: true }).catch(function () {}); } catch (e) {}
  }

  function el(html) { var d = document.createElement("div"); d.innerHTML = html.trim(); return d.firstChild; }
  var base = cfg.base || "/";

  function banner() {
    var b = el('<div class="priv-banner" role="dialog" aria-live="polite" aria-label="Privacy">' +
      '<button class="priv-x" aria-label="Chiudi e continua senza" data-p="no">×</button>' +
      '<p><b>La tua privacy.</b> Questo sito non usa cookie di profilazione né pubblicità. Conta le visite in forma anonima (senza cookie, l\'indirizzo IP non viene conservato) e mostra i video di YouTube solo se lo accetti. ' +
      '<a href="' + base + 'privacy/">Informativa privacy</a></p>' +
      '<div class="priv-azioni"><button class="priv-btn" data-p="no">Continua senza video esterni</button>' +
      '<button class="priv-btn" data-p="pref">Preferenze</button>' +
      '<button class="priv-btn" data-p="si">Accetta i video di YouTube</button></div></div>');
    b.addEventListener("click", function (e) {
      var a = e.target.closest("[data-p]"); if (!a) return;
      if (a.dataset.p === "si") salva({ stat: true, yt: true });
      if (a.dataset.p === "no") salva({ stat: true, yt: false });
      if (a.dataset.p === "pref") { b.remove(); apriPreferenze(); return; }
      b.remove();
    });
    document.body.appendChild(b);
  }

  function apriPreferenze() {
    var p = leggi() || { stat: true, yt: false };
    var old = document.querySelector(".priv-pref"); if (old) old.remove();
    var d = el('<div class="priv-pref" role="dialog" aria-modal="true" aria-label="Preferenze privacy"><div class="priv-box">' +
      '<h2>Preferenze privacy</h2>' +
      '<label class="priv-riga"><input type="checkbox" checked disabled><span><b>Necessari</b> — memoria del browser per ricordare queste scelte e, nel simulatore, il tuo lavoro. Sempre attivi.</span></label>' +
      '<label class="priv-riga"><input type="checkbox" id="priv-stat"' + (p.stat !== false ? " checked" : "") + '><span><b>Statistiche anonime</b> — conteggio delle visite e del Paese di provenienza, senza cookie e senza conservare l\'indirizzo IP.</span></label>' +
      '<label class="priv-riga"><input type="checkbox" id="priv-yt"' + (p.yt ? " checked" : "") + '><span><b>Video di YouTube</b> — i video della sezione Training si caricano da YouTube (Google), che riceve il tuo indirizzo IP e può usare i propri cookie.</span></label>' +
      '<p class="priv-nota">Dettagli nell\'<a href="' + base + 'privacy/">informativa privacy</a>. Puoi cambiare idea quando vuoi dal link "Preferenze privacy" in fondo alle pagine.</p>' +
      '<div class="priv-azioni"><button class="priv-btn" data-p="annulla">Annulla</button><button class="priv-btn pri" data-p="salva">Salva le scelte</button></div></div></div>');
    d.addEventListener("click", function (e) {
      var a = e.target.closest("[data-p]");
      if (e.target === d || (a && a.dataset.p === "annulla")) { d.remove(); if (!leggi()) banner(); return; }
      if (a && a.dataset.p === "salva") { salva({ stat: d.querySelector("#priv-stat").checked, yt: d.querySelector("#priv-yt").checked }); d.remove(); }
    });
    document.body.appendChild(d);
    d.querySelector("#priv-stat").focus();
  }

  document.addEventListener("click", function (e) { if (e.target.closest("[data-privacy-pref]")) { e.preventDefault(); apriPreferenze(); } });
  if (!scelta) banner();
  conta();
})();

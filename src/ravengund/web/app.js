'use strict';
// RavenGun-app: tabbladen, schuifjes, schakelaars. Geen externe bronnen, geen inline code (CSP).
// Alle tekst gaat via textContent. De logica zonder DOM staat in app-core.js (RGApp).
(function () {
  var A = window.RGApp;
  var csrf = (document.querySelector('meta[name="csrf-token"]') || {}).content || '';
  var $ = function (id) { return document.getElementById(id); };
  var current = '';
  var pollTimer = null;
  var es = null;
  var lastStatus = null;   // laatste /api/status
  var defaults = null;     // fabrieksinstellingen uit /api/settings
  // Gekozen speler (spelerbalk): de instellingen op Overzicht, Richten, Trillen, Knoppen en LED gelden voor de controller van
  // deze speler. Onthouden in localStorage (met try/catch; zonder opslag gewoon speler 1).
  var player = A.pickPlayer((function () { try { return window.localStorage.getItem('rg.player'); } catch (e) { return null; } })());
  function pq() { return '?player=' + player; }
  function P(body) { return A.withPlayer(body, player); }

  // ---- Hulpfuncties ----
  function api(path, body) {
    var opt = { cache: 'no-store' };
    if (body !== undefined) {
      opt.method = 'POST';
      opt.headers = { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf };
      opt.body = JSON.stringify(body);
    }
    return fetch(path, opt).then(function (r) {
      return r.json().then(function (j) { j.httpStatus = r.status; setConn(true); return j; });
    }, function (e) { setConn(false); throw e; });
  }
  // Als api(), maar een geweigerd verzoek is een fout met de melding van de daemon.
  function call(path, body) {
    return api(path, body).then(function (j) {
      if (!j.ok) throw new Error(errText(j));
      return j;
    });
  }
  function errText(j) { return j && j.error ? j.error.message : 'onbekende fout'; }
  function errMsg(e) { return e && e.message === 'Failed to fetch' ? 'het programma op de achtergrond is niet bereikbaar' : (e && e.message) || 'onbekende fout'; }
  function setConn(ok) {
    var c = $('conn');
    c.textContent = ok ? 'verbonden' : 'niet bereikbaar';
    c.className = 'pill ' + (ok ? 'ok' : 'bad');
  }
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function fmt(v, d) { return A.fmtNl(v, d); }
  function yn(b) { return b ? 'ja' : 'nee'; }
  function fill(dl, rows) {
    dl.textContent = '';
    rows.forEach(function (r) {
      var dd = el('dd', r[2] || '', r[1]);
      dl.appendChild(el('dt', '', r[0]));
      dl.appendChild(dd);
    });
  }
  var toastTimer = null;
  function toast(msg) {
    var t = $('toast');
    t.textContent = msg;
    t.className = 'toast show';
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.className = 'toast'; }, 2600);
  }
  function dur(s) {
    if (typeof s !== 'number' || !isFinite(s)) return '-';
    s = Math.floor(s);
    var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
    return (d ? d + ' d ' : '') + (h || d ? h + ' u ' : '') + m + ' min ' + (s % 60) + ' s';
  }
  function setHidden(e, hidden, text) {
    e.hidden = hidden;
    if (!hidden) e.textContent = text;
  }
  function rgbCss(r, g, b) { return 'rgb(' + r + ',' + g + ',' + b + ')'; }
  function hex2(n) { var s = n.toString(16); return s.length < 2 ? '0' + s : s; }
  function num(id, lo, hi, name, errs, integer) {
    var e = $(id), raw = e.value.trim().replace(',', '.');
    e.classList.remove('invalid');
    var v = raw === '' ? NaN : Number(raw);
    if (!isFinite(v) || v < lo || v > hi || (integer && Math.floor(v) !== v)) {
      errs.push(name + ' moet ' + (integer ? 'een geheel getal' : 'een getal') + ' tussen ' + lo + ' en ' + hi + ' zijn');
      e.classList.add('invalid');
      return null;
    }
    return v;
  }
  // POST met foutmelding bij het formulier; onOk krijgt het resultaat.
  function submit(path, body, errEl, okMsg, btn, onOk) {
    errEl.textContent = '';
    if (btn) btn.disabled = true;
    return api(path, body).then(function (j) {
      if (btn) btn.disabled = false;
      if (j.ok) { if (okMsg) toast(okMsg); if (onOk) onOk(j.result); }
      else errEl.textContent = errText(j);
      return j;
    }).catch(function () {
      if (btn) btn.disabled = false;
      errEl.textContent = 'het programma op de achtergrond is niet bereikbaar';
    });
  }
  function getStatus() {
    return call('/api/status' + pq()).then(function (j) { lastStatus = j.result; renderPlayerBar(j.result); return j.result; });
  }

  // ---- Spelerbalk ----
  var pbSig = '';
  function renderPlayerBar(st) {
    var players = st && st.assignment && st.assignment.players;
    if (!players) return;
    var opts = A.playerOptions(players, player);
    var sig = JSON.stringify(opts) + '|' + player;
    if (sig === pbSig) return;
    pbSig = sig;
    var box = $('pb-choices');
    box.textContent = '';
    opts.forEach(function (o) {
      var lab = el('label');
      var r = el('input');
      r.type = 'radio'; r.name = 'player'; r.value = String(o.player);
      r.checked = o.player === player;
      var seg = el('span', 'seg', o.label);
      seg.appendChild(el('small', '', o.sub));
      lab.appendChild(r); lab.appendChild(seg);
      box.appendChild(lab);
      r.addEventListener('change', function () { choosePlayer(o.player); });
    });
    var mine = null;
    players.forEach(function (x) { if (x.player === player) mine = x; });
    $('pb-note').textContent = mine && !mine.controller ? 'Deze speler heeft nog geen Move: wijzigingen gelden voor nieuwe controllers.' : '';
  }
  function choosePlayer(n) {
    player = A.pickPlayer(n);
    try { window.localStorage.setItem('rg.player', String(player)); } catch (e) { /* geen opslag */ }
    btnBase = {}; btnEdit = {};   // de knoppenindeling hoort bij de vorige speler
    activeLed = null;
    show(true);
  }

  // ---- Bouwstenen: schakelaar, schuifje ----
  // apply(checked) geeft een belofte met de werkelijke stand uit de daemon.
  function makeSwitch(box, o) {
    var lab = el('label', 'switch');
    var inp = el('input');
    inp.type = 'checkbox';
    var track = el('span', 'track');
    var txt = el('span', 'label', o.label);
    if (o.help) { txt.appendChild(el('span', 'help', o.help)); }
    lab.appendChild(inp); lab.appendChild(track); lab.appendChild(txt);
    box.appendChild(lab);
    var busy = false;
    inp.addEventListener('change', function () {
      var want = inp.checked;
      busy = true; inp.disabled = true;
      if (o.errEl) o.errEl.textContent = '';
      Promise.resolve(o.apply(want)).then(function (actual) {
        inp.checked = !!actual;
      }, function (e) {
        inp.checked = !want;
        if (o.errEl) o.errEl.textContent = errMsg(e);
      }).then(function () { busy = false; inp.disabled = false; });
    });
    return {
      set: function (v) { if (!busy) inp.checked = !!v; },
      setDisabled: function (d) { inp.disabled = !!d; }
    };
  }

  // o: { name, help, min, max, step, scale, fmt(v), send(v) -> belofte met antwoord, actual(result, v) -> getal }
  function makeSlider(box, o) {
    var wrap = el('div', 'slider');
    var head = el('div', 'head');
    var name = el('span', 'name', o.name), val = el('span', 'val', '-');
    head.appendChild(name); head.appendChild(val);
    var inp = el('input');
    inp.type = 'range';
    if (o.scale) { inp.min = 0; inp.max = o.scale.steps; inp.step = 1; }
    else { inp.min = o.min; inp.max = o.max; inp.step = o.step || 1; }
    inp.setAttribute('aria-label', o.name);
    var st = el('div', 'st');
    wrap.appendChild(head); wrap.appendChild(inp);
    if (o.help) wrap.appendChild(el('div', 'help', o.help));
    wrap.appendChild(st);
    box.appendChild(wrap);
    var known = null, dragging = false, stTimer = null;
    function valueOf(pos) { return o.scale ? o.scale.toValue(Number(pos)) : Number(pos); }
    function show(v) {
      val.textContent = o.fmt(v);
      inp.value = o.scale ? o.scale.toPos(v) : Math.min(Math.max(v, o.min), o.max);
    }
    function message(text, cls) {
      st.textContent = text; st.className = 'st ' + (cls || '');
      clearTimeout(stTimer);
      if (cls === 'ok') stTimer = setTimeout(function () { st.textContent = ''; st.className = 'st'; }, 2200);
    }
    var sender = A.createLiveSender({
      delay: 150,
      send: function (v) { message('Bezig...', ''); return o.send(v); },
      onDone: function (j, v) {
        // Bevestiging: de werkelijke waarde uit de daemon (die kan afwijken van wat gevraagd is).
        var a = o.actual(j.result, v);
        if (typeof a === 'number' && isFinite(a)) { known = a; if (!dragging) show(a); }
        message('Toegepast', 'ok');
      },
      onError: function (e) {
        message(errMsg(e), 'bad');
        if (known !== null && !dragging) show(known);
      }
    });
    inp.addEventListener('input', function () {
      var v = valueOf(inp.value);
      val.textContent = o.fmt(v);
      sender.input(v);
    });
    inp.addEventListener('change', function () { sender.commit(valueOf(inp.value)); });
    inp.addEventListener('pointerdown', function () { dragging = true; });
    inp.addEventListener('pointerup', function () { dragging = false; });
    inp.addEventListener('pointercancel', function () { dragging = false; });
    inp.addEventListener('blur', function () { dragging = false; });
    return {
      // Waarde uit de daemon (poll); wordt genegeerd zolang de speler het schuifje vasthoudt of er iets onderweg is.
      set: function (v) {
        if (typeof v !== 'number' || !isFinite(v)) return;
        known = v;
        if (dragging || sender.busy()) return;
        show(v);
      },
      setDisabled: function (d) { inp.disabled = !!d; },
      value: function () { return known; }
    };
  }

  // ---- Batterij-icoon ----
  function drawBattery(box, view) {
    box.textContent = '';
    var b = el('div', 'battery ' + (view.level === 'full' ? '' : view.level));
    b.setAttribute('role', 'img');
    b.setAttribute('aria-label', 'Batterij: ' + view.label + (view.detail ? ', ' + view.detail : ''));
    var body = el('div', 'body');
    for (var i = 0; i < 5; i++) body.appendChild(el('span', 'cell' + (i < view.segments ? ' on' : '')));
    b.appendChild(body); b.appendChild(el('div', 'tip'));
    box.appendChild(b);
  }
  function batteryText(view, connected) {
    if (!connected) return '';
    return 'Batterij: ' + view.label + (view.detail ? ' (' + view.detail + ')' : '');
  }
  function glyph(box, kind, text) {
    box.textContent = '';
    box.appendChild(el('span', kind, text));
  }

  // ---- Overzicht ----
  var gamesCache = null;
  var lastSample = null;
  function gameName(profile) {
    var list = (gamesCache && gamesCache.games) || [];
    for (var i = 0; i < list.length; i++) if (list[i].profile === profile) return list[i].name;
    return profile;
  }
  function refreshOverview() {
    Promise.all([api('/api/status' + pq()), api('/api/sample' + pq()), api('/api/games'), api('/api/controllers')]).then(function (res) {
      var st = res[0], sm = res[1], gm = res[2], ct = res[3];
      if (!st.ok || !sm.ok) { setConn(false); return; }
      lastStatus = st.result;
      renderPlayerBar(st.result);
      var s = st.result, p = sm.result, c = s.controller, cam = s.camera || {};
      if (gm.ok) gamesCache = gm.result;
      // Alle controllers met batterij (Moves en Navigation Controllers).
      renderOverviewControllers(ct.ok ? ct.result.controllers : null, s);
      // Camera
      lastSample = p;
      var pos = p.position || {};
      var streaming = cam.enabled && cam.state === 'streaming';
      if (!cam.enabled) { glyph($('ov-cam-icon'), 'no', '✖'); $('ov-cam-state').textContent = 'Camera staat uit'; $('ov-cam-state').className = 'big warn'; $('ov-cam-sub').textContent = ''; }
      else if (!streaming) { glyph($('ov-cam-icon'), 'bad', '✖'); $('ov-cam-state').textContent = 'Camera werkt niet'; $('ov-cam-state').className = 'big bad'; $('ov-cam-sub').textContent = cam.error || 'Is de camera aangesloten?'; }
      else if (pos.valid) { glyph($('ov-cam-icon'), 'yes', '✔'); $('ov-cam-state').textContent = 'Ziet de bol'; $('ov-cam-state').className = 'big ok'; $('ov-cam-sub').textContent = ''; }
      else { glyph($('ov-cam-icon'), 'warn', '✖'); $('ov-cam-state').textContent = 'Ziet de bol niet'; $('ov-cam-state').className = 'big warn'; $('ov-cam-sub').textContent = c.connected ? 'Houd de Move in beeld van de camera.' : 'De bol licht alleen op als de Move aan staat.'; }
      // Lightgun
      var armed = !!(s.output && s.output.armed);
      glyph($('ov-gun-icon'), armed ? 'yes' : 'no', armed ? '✔' : '✖');
      $('ov-gun-state').textContent = armed ? 'Klaar' : 'Uit';
      $('ov-gun-state').className = 'big ' + (armed ? 'ok' : 'warn');
      $('ov-gun-btn').textContent = armed ? 'Uitzetten' : 'Aanzetten';
      $('ov-gun-btn').className = armed ? 'secondary' : '';
      // Game
      var g = gm.ok ? gm.result.status : null;
      var gv = A.gameStateView(g);
      var running = g && g.state === 'running';
      glyph($('ov-game-icon'), running ? 'yes' : (g && g.state === 'failed' ? 'bad' : 'no'), running ? '▶' : (g && g.state === 'failed' ? '✖' : '—'));
      var gt = running ? (g.profile ? gameName(g.profile) + ' draait' : 'Er draait een game') : (g && g.state === 'idle' ? 'Geen game actief' : (g ? g.text : '-'));
      $('ov-game-state').textContent = gt;
      $('ov-game-state').className = 'big ' + (running ? 'ok' : (g && g.state === 'failed' ? 'bad' : ''));
      $('ov-game-sub').textContent = g && g.state === 'failed' && g.message ? g.message : (gv.busy ? gv.text : '');
      fill($('ov-settings'), A.plainSettings(s));
    }).catch(function () { setConn(false); });
  }
  function ledChip(rgb, cls) {
    var d = el('span', cls + (rgb ? '' : ' none'));
    if (rgb) d.style.background = rgbCss(rgb[0], rgb[1], rgb[2]);
    d.setAttribute('aria-hidden', 'true');
    return d;
  }
  function renderOverviewControllers(list, s) {
    var box = $('ov-ctrls');
    box.textContent = '';
    var shown = (list || []).filter(function (c) { return !c.removed; });
    if (!list) {
      // Geen controllerlijst (oude daemon): de Move van deze speler uit de status.
      shown = [{ name: 'Move', type: 'move', player: player, connected: s.controller.connected,
        battery: { known: s.controller.connected, code: s.controller.battery && s.controller.battery.code } }];
    }
    if (!shown.length) {
      var e = el('article', 'card status ovctrl');
      e.appendChild(el('h2', '', 'Controllers'));
      e.appendChild(el('p', 'big warn', 'Nog geen controller'));
      var a = el('a', 'button secondary', 'Controller koppelen');
      a.href = '#/controllers';
      var pa = el('p', 'actions'); pa.appendChild(a); e.appendChild(pa);
      box.appendChild(e);
      return;
    }
    shown.forEach(function (c) {
      var card = el('article', 'card status ovctrl');
      var h = el('h2');
      if (c.type === 'move') h.appendChild(ledChip(c.led && c.led.rgb, 'led-chip'));
      h.appendChild(document.createTextNode(c.name + (c.player ? ' · speler ' + c.player : '')));
      card.appendChild(h);
      var bw = el('div', 'batwrap');
      var bv = A.controllerBattery(c);
      drawBattery(bw, bv);
      card.appendChild(bw);
      card.appendChild(el('p', 'big ' + (c.connected ? 'ok' : 'warn'), c.connected ? 'Verbonden' : 'Niet verbonden'));
      var sub = c.connected ? batteryText(bv, true) : (c.type === 'move' ? 'Zet de Move aan met de PS-knop.' : 'Niet in gebruik.');
      if (c.type === 'move' && c.connected && c.player === player && lastSample && lastSample.ext && lastSample.ext.connected) sub += '. Sharp Shooter aangesloten.';
      card.appendChild(el('p', 'sub', A.controllerTypeLabel(c.type) + '. ' + sub));
      box.appendChild(card);
    });
  }
  $('ov-gun-btn').addEventListener('click', function (e) {
    var armed = lastStatus && lastStatus.output && lastStatus.output.armed;
    if (!armed && !window.confirm('De lightgun aanzetten? Games krijgen dan muisbeweging en knoppen van de Move.')) return;
    submit(armed ? '/api/disarm' : '/api/arm', {}, $('ov-gun-err'), armed ? 'Lightgun staat uit' : 'Lightgun staat aan', e.target, refreshOverview);
  });
  $('btn-center').addEventListener('click', function (e) {
    var m = $('center-msg');
    m.textContent = ''; m.className = 'msg';
    e.target.disabled = true;
    api('/api/recenter', P({})).then(function (j) {
      e.target.disabled = false;
      if (j.ok) { m.textContent = 'Gecentreerd.'; setTimeout(function () { m.textContent = ''; }, 2500); }
      else { m.className = 'msg bad'; m.textContent = j.httpStatus === 503 ? 'Centreren lukt nog niet: is de Move van speler ' + player + ' aan en verbonden?' : errText(j); }
    }).catch(function () { e.target.disabled = false; m.className = 'msg bad'; m.textContent = 'Het programma op de achtergrond is niet bereikbaar.'; });
  });

  // ---- Richten ----
  var sliders = {};
  var switches = {};
  function defaultsOf(section, key, fallback) {
    return defaults && defaults[section] && typeof defaults[section][key] === 'number' ? defaults[section][key] : fallback;
  }
  function buildRichten() {
    var err = $('rich-err');
    switches.filter = makeSwitch($('sw-filter'), {
      label: 'Richtpunt gladstrijken', help: 'Haalt het trillen van je hand uit de cursor.', errEl: err,
      apply: function (on) { return call('/api/filter', P({ enabled: on })).then(function (j) { return j.result.enabled; }); }
    });
    var sl = $('sl-richten');
    sliders.predict = makeSlider(sl, {
      name: 'Vooruitkijken', help: 'De cursor kijkt zoveel milliseconden vooruit, zodat hij niet achterloopt op je hand. 0 is uit.',
      min: 0, max: 100, step: 1, fmt: function (v) { return fmt(v, 0) + ' ms'; },
      send: function (v) { return call('/api/aim', P({ predict_ms: v })); },
      actual: function (r) { return r.predict_ms; }
    });
    sliders.mincut = makeSlider(sl, {
      name: 'Rust bij stilhouden', help: 'Lager = een rustiger richtpunt, maar het volgt je hand wat trager.',
      scale: A.logScale(0.05, 5, 1000), fmt: function (v) { return fmt(v, 2); },
      send: function (v) { return call('/api/filter', P({ min_cutoff_hz: v })); },
      actual: function (r) { return r.min_cutoff_hz; }
    });
    sliders.beta = makeSlider(sl, {
      name: 'Reactiesnelheid bij snel richten', help: 'Hoger = minder vertraging als je snel zwaait, maar iets onrustiger.',
      min: 0, max: 100, step: 1, fmt: function (v) { return fmt(v, 0); },
      send: function (v) { return call('/api/filter', P({ beta: v })); },
      actual: function (r) { return r.beta; }
    });
    var adv = $('sl-richten-adv');
    sliders.hedge = makeSlider(adv, {
      name: 'Draaihoek tot de rand, links en rechts', help: 'Hoeveel graden je de Move draait van het midden tot de zijkant van het scherm. Kleiner = gevoeliger.',
      min: 1, max: 89, step: 1, fmt: function (v) { return fmt(v, 0) + ' graden'; },
      send: function (v) { return call('/api/aim', P({ h_edge_deg: v })); },
      actual: function (r) { return r.h_edge_deg; }
    });
    sliders.vedge = makeSlider(adv, {
      name: 'Draaihoek tot de rand, boven en onder', help: 'Zelfde, voor omhoog en omlaag.',
      min: 1, max: 89, step: 1, fmt: function (v) { return fmt(v, 0) + ' graden'; },
      send: function (v) { return call('/api/aim', P({ v_edge_deg: v })); },
      actual: function (r) { return r.v_edge_deg; }
    });
    sliders.dcut = makeSlider(adv, {
      name: 'Snelheidsfilter', help: 'Hoe vloeiend de snelheid van je hand wordt gemeten (afsnijfrequentie in Hz).',
      min: 1, max: 100, step: 1, fmt: function (v) { return fmt(v, 0) + ' Hz'; },
      send: function (v) { return call('/api/filter', P({ d_cutoff_hz: v })); },
      actual: function (r) { return r.d_cutoff_hz; }
    });
    sliders.extrap = makeSlider(adv, {
      name: 'Doorlopen bij een haperende verbinding', help: 'Hoe lang de cursor doorloopt als de Move even geen gegevens stuurt (daarna blijft hij staan).',
      min: 0, max: 100, step: 1, fmt: function (v) { return fmt(v, 0) + ' ms'; },
      send: function (v) { return call('/api/clock', { extrapolate_ms: v }); },
      actual: function (r) { return r.extrapolate_ms; }
    });
    // Driftcorrectie: keuzerondjes met uitleg.
    var box = $('fusion-choices');
    A.FUSION_CHOICES.forEach(function (c) {
      var lab = el('label', 'choice');
      var r = el('input');
      r.type = 'radio'; r.name = 'fusion'; r.value = c.id;
      var t = el('span');
      t.appendChild(el('span', 't', c.label));
      t.appendChild(el('span', 'd', c.help));
      lab.appendChild(r); lab.appendChild(t);
      box.appendChild(lab);
      r.addEventListener('change', function () {
        $('fusion-err').textContent = '';
        call('/api/fusion', P({ mode: c.id })).then(function (j) { setFusionChoice(j.result.mode); toast('Driftcorrectie: ' + A.fusionLabel(j.result.mode)); },
          function (e) { $('fusion-err').textContent = errMsg(e); getStatus().then(function (s) { setFusionChoice(s.config.fusion.mode); }); });
      });
    });
    switches.learn = makeSwitch($('sw-learn'), {
      label: 'Leren van centreren', help: 'Elke keer dat je centreert leert de Move bij hoe hij wegdraait en corrigeert dat zelf.', errEl: $('fusion-err'),
      apply: function (on) { return call('/api/fusion', P({ recenter_learning: on })).then(function (j) { return j.result.recenter_learning; }); }
    });
    $('rich-reset').addEventListener('click', function (e) {
      var d = defaults || {};
      var f = d.filter || {}, a = d.aim || {};
      if (!d.filter) { $('rich-err').textContent = 'De standaardwaarden zijn niet beschikbaar.'; return; }
      $('rich-err').textContent = '';
      e.target.disabled = true;
      call('/api/filter', P({ enabled: f.enabled, min_cutoff_hz: f.min_cutoff_hz, beta: f.beta, d_cutoff_hz: f.d_cutoff_hz }))
        .then(function () { return call('/api/aim', P({ predict_ms: a.predict_ms })); })
        .then(function () { toast('Standaardwaarden hersteld'); return refreshRichten(true); })
        .catch(function (er) { $('rich-err').textContent = errMsg(er); })
        .then(function () { e.target.disabled = false; });
    });
  }
  function setFusionChoice(mode) {
    Array.prototype.forEach.call(document.querySelectorAll('#fusion-choices input'), function (r) {
      r.checked = r.value === mode;
      r.parentNode.className = 'choice' + (r.checked ? ' sel' : '');
    });
  }
  function renderClock(hz) {
    var box = $('clock-choices');
    var wantSig = A.clockChoices(hz).map(function (c) { return c.hz; }).join();
    if (box.getAttribute('data-sig') !== wantSig) {
      box.setAttribute('data-sig', wantSig);
      box.textContent = '';
      A.clockChoices(hz).forEach(function (c) {
        var lab = el('label');
        var r = el('input');
        r.type = 'radio'; r.name = 'clock'; r.value = String(c.hz);
        lab.appendChild(r); lab.appendChild(el('span', 'seg', c.label));
        box.appendChild(lab);
        r.addEventListener('change', function () {
          $('clock-err').textContent = '';
          call('/api/clock', { clock_hz: c.hz }).then(function (j) { renderClock(j.result.clock_hz); toast('Uitvoerklok: ' + (c.hz ? c.hz + ' Hz' : 'uit')); },
            function (e) { $('clock-err').textContent = errMsg(e); refreshRichten(true); });
        });
      });
    }
    Array.prototype.forEach.call(box.querySelectorAll('input'), function (r) { r.checked = Number(r.value) === hz; });
  }
  function refreshRichten() {
    return getStatus().then(function (s) {
      var f = s.config.filter, a = s.config.aim, ck = s.output && s.output.live && s.output.live.clock;
      switches.filter.set(f.enabled);
      sliders.predict.set(a.predict_ms);
      sliders.mincut.set(f.min_cutoff_hz);
      sliders.beta.set(f.beta);
      sliders.dcut.set(f.d_cutoff_hz);
      sliders.hedge.set(a.h_edge_deg);
      sliders.vedge.set(a.v_edge_deg);
      if (ck) { sliders.extrap.set(ck.extrapolate_ms); renderClock(ck.enabled ? ck.hz : 0); }
      setFusionChoice(s.config.fusion.mode);
      switches.learn.set(s.yaw_trim ? s.yaw_trim.learning : true);
    }).catch(function () {});
  }
  // Kalibratie op de tv
  function refreshCal() {
    api('/api/calibrate/ui').then(function (j) {
      $('cal-state').textContent = j.ok ? j.result.message : 'Kalibratiescherm: ' + errText(j);
    }).catch(function () {});
  }
  $('cal-open').addEventListener('click', function (e) {
    submit('/api/calibrate/ui', { action: 'open' }, $('cal-err'), 'Kalibratiescherm geopend op de tv', e.target, refreshCal);
  });
  $('cal-practice').addEventListener('click', function (e) {
    submit('/api/calibrate/ui', { action: 'open', step: 'practice' }, $('cal-err'), 'Oefenscherm geopend op de tv', e.target, refreshCal);
  });
  $('cal-close').addEventListener('click', function (e) {
    submit('/api/calibrate/ui', { action: 'close' }, $('cal-err'), 'Scherm gesloten', e.target, refreshCal);
  });

  // ---- Live richtpunt en bol (stroom) ----
  var last = null;
  function drawAim() {
    var cv = $('aim'), g = cv.getContext('2d'), w = cv.width, h = cv.height;
    g.clearRect(0, 0, w, h);
    g.strokeStyle = '#2c3547';
    g.lineWidth = 1;
    g.strokeRect(0.5, 0.5, w - 1, h - 1);
    g.beginPath();
    g.moveTo(w / 2, 0); g.lineTo(w / 2, h);
    g.moveTo(0, h / 2); g.lineTo(w, h / 2);
    g.stroke();
    var a = last && last.aim;
    if (!a || !a.valid) { $('aim-note').textContent = 'Zet de Move aan om te zien waar hij wijst.'; return; }
    $('aim-note').textContent = a.off_screen ? 'buiten het scherm' : '';
    function dot(x, y, color, r) {
      var px = Math.max(0, Math.min(1, x)) * w, py = Math.max(0, Math.min(1, y)) * h;
      g.fillStyle = color;
      g.beginPath(); g.arc(px, py, r, 0, 6.2832); g.fill();
    }
    dot(a.x_raw, a.y_raw, '#7d8799', 6);
    dot(a.x, a.y, '#4fb3ff', 10);
  }
  function drawMini() {
    var cv = $('mini'), g = cv.getContext('2d'), w = cv.width, h = cv.height;
    g.clearRect(0, 0, w, h);
    g.strokeStyle = '#2c3547';
    g.strokeRect(0.5, 0.5, w - 1, h - 1);
    var p = last && last.position;
    if (!p || !p.valid) {
      $('cam-see').textContent = 'Nee, de bol is niet in beeld';
      $('cam-see').className = 'big warn';
      $('mini-note').textContent = 'Alleen een stip, nooit een camerabeeld.' + (p && p.reason && p.reason !== 'none' ? ' (' + p.reason + ')' : '');
      return;
    }
    $('cam-see').textContent = 'Ja, de bol is in beeld';
    $('cam-see').className = 'big ok';
    $('mini-note').textContent = 'Alleen een stip, nooit een camerabeeld.';
    var r = Math.max(5, (p.radius_px / p.image_w) * w);
    g.strokeStyle = '#5ad19a';
    g.lineWidth = 2;
    g.beginPath(); g.arc(p.x_norm * w, p.y_norm * h, r, 0, 6.2832); g.stroke();
    g.fillStyle = '#5ad19a';
    g.beginPath(); g.arc(p.x_norm * w, p.y_norm * h, 3, 0, 6.2832); g.fill();
  }
  function onSample(ev) {
    var d;
    try { d = JSON.parse(ev.data); } catch (e) { return; }
    last = d.sample;
    if (current === 'richten') drawAim();
    else if (current === 'led') drawMini();
    else if (current === 'knoppen') lightButtons();
  }
  function startStream() {
    stopStream();
    if (!window.EventSource || /[?&]nostream\b/.test(location.search)) return;  // ?nostream: voor schermafdrukken en tests
    es = new EventSource('/api/stream' + pq());
    es.addEventListener('sample', onSample);
    es.onopen = function () { setConn(true); };
  }
  function stopStream() { if (es) { es.close(); es = null; } }

  // ---- Trillen ----
  function buildTrillen() {
    var err = $('rum-err');
    switches.rumble = makeSwitch($('sw-rumble'), {
      label: 'Trillen aan', help: 'Zet het trillen van de Move bij schieten aan of uit.', errEl: err,
      apply: function (on) { return call('/api/rumble', P({ enabled: on })).then(function (j) { return j.result.enabled; }); }
    });
    var sl = $('sl-trillen');
    function pct(v) { return A.percent255(v) + ' %'; }
    sliders.rstrength = makeSlider(sl, {
      name: 'Sterkte', help: 'Hoe hard de Move trilt bij een schot.', min: 0, max: 255, step: 1, fmt: pct,
      send: function (v) { return call('/api/rumble', P({ strength: Math.round(v) })); }, actual: function (r) { return r.strength; }
    });
    sliders.rpulse = makeSlider(sl, {
      name: 'Pulsduur', help: 'Hoe lang elke trilling duurt.', min: 10, max: 500, step: 5, fmt: function (v) { return fmt(v, 0) + ' ms'; },
      send: function (v) { return call('/api/rumble', P({ pulse_ms: Math.round(v) })); }, actual: function (r) { return r.pulse_ms; }
    });
    sliders.rrepeat = makeSlider(sl, {
      name: 'Ritme bij vasthouden', help: 'Hoe vaak de Move trilt als je de trekker ingedrukt houdt. 0 is een enkele trilling per schot.',
      min: 0, max: 30, step: 1, fmt: function (v) { return v === 0 ? 'enkel' : fmt(v, 0) + ' per seconde'; },
      send: function (v) { return call('/api/rumble', P({ repeat_hz: v })); }, actual: function (r) { return r.repeat_hz; }
    });
    sliders.rpump = makeSlider(sl, {
      name: 'Sterkte bij de pomp', help: 'Hoe hard de Move trilt als je de Sharp Shooter pompt. 0 is uit.', min: 0, max: 255, step: 1, fmt: pct,
      send: function (v) { return call('/api/rumble', P({ pump_strength: Math.round(v) })); }, actual: function (r) { return r.pump_strength; }
    });
    $('rum-test').addEventListener('click', function (e) {
      var m = $('rum-msg');
      err.textContent = ''; m.textContent = '';
      e.target.disabled = true;
      api('/api/rumble-test', P({})).then(function (j) {
        e.target.disabled = false;
        if (j.ok) { m.textContent = 'Testpuls gestuurd.'; setTimeout(function () { m.textContent = ''; }, 2500); }
        else err.textContent = j.httpStatus === 503 ? 'Geen trilling: ' + errText(j) : errText(j);
      }).catch(function () { e.target.disabled = false; err.textContent = 'Het programma op de achtergrond is niet bereikbaar.'; });
    });
  }
  function refreshTrillen() {
    return getStatus().then(function (s) {
      var r = s.controller && s.controller.rumble;
      if (!r) return;
      switches.rumble.set(r.enabled);
      sliders.rstrength.set(r.strength);
      sliders.rpulse.set(r.pulse_ms);
      sliders.rrepeat.set(r.repeat_hz);
      sliders.rpump.set(r.pump_strength);
    }).catch(function () {});
  }

  // ---- Knoppen ----
  var btnBase = {};      // de indeling zoals de daemon hem heeft
  var btnEdit = {};      // wat er nu in de keuzelijsten staat
  var btnBuilt = false;
  var btnSelects = {};
  var btnRows = {};
  function buildKnoppen() {
    var box = $('btn-rows');
    box.textContent = '';
    btnSelects = {}; btnRows = {};
    function row(id, label, fixedText) {
      var r = el('div', 'btnrow' + (fixedText ? ' fixed' : ''));
      var l = el('div', 'lbl');
      l.appendChild(el('span', 'led'));
      l.appendChild(el('span', '', label));
      r.appendChild(l);
      var sel = el('select');
      sel.setAttribute('aria-label', label);
      if (fixedText) {
        sel.disabled = true;
        sel.appendChild(new Option(fixedText, fixedText));
      } else {
        A.actionGroups().forEach(function (g) {
          var og = document.createElement('optgroup');
          og.label = g.label;
          g.options.forEach(function (o) { og.appendChild(new Option(o.label, o.value)); });
          sel.appendChild(og);
        });
        sel.addEventListener('change', function () { btnEdit[id] = sel.value; updateBtnPending(); });
        btnSelects[id] = sel;
      }
      r.appendChild(sel);
      box.appendChild(r);
      btnRows[id] = r;
    }
    box.appendChild(el('p', 'btnsep', 'Vaste knoppen'));
    row('t', 'Trekker', 'Schieten (vast)');
    row('ps', 'PS-knop', 'Centreren (vast)');
    box.appendChild(el('p', 'btnsep', 'Knoppen van de Move'));
    A.BUTTONS.forEach(function (b) {
      if (b.group === 'move') row(b.id, b.label);
    });
    box.appendChild(el('p', 'btnsep', 'Knoppen van de Sharp Shooter (alleen als hij aangesloten is)'));
    A.BUTTONS.forEach(function (b) {
      if (b.group === 'sharp') row(b.id, b.label);
    });
    box.appendChild(el('p', 'btnsep', 'Knoppen van de Navigation Controller (als die bij deze speler hoort)'));
    A.BUTTONS.forEach(function (b) {
      if (b.group === 'nav') row(b.id, b.label);
    });
    btnBuilt = true;
  }
  function applyEditToSelects() {
    A.BUTTONS.forEach(function (b) {
      var sel = btnSelects[b.id];
      if (!sel) return;
      var v = btnEdit[b.id] || 'none';
      // Een actie die niet in de lijst staat (handmatig in config.json gezet) blijft zichtbaar.
      var found = false;
      for (var i = 0; i < sel.options.length; i++) if (sel.options[i].value === v) found = true;
      if (!found) sel.insertBefore(new Option(v, v), sel.firstChild);
      sel.value = v;
    });
    updateBtnPending();
  }
  function updateBtnPending() {
    var ch = A.mapChanges(btnBase, btnEdit);
    A.BUTTONS.forEach(function (b) {
      if (btnRows[b.id]) btnRows[b.id].classList.toggle('changed', ch.changes[b.id] !== undefined);
    });
    $('btn-savebar').hidden = ch.count === 0;
    $('btn-pending').textContent = ch.count === 1 ? '1 wijziging nog niet opgeslagen' : ch.count + ' wijzigingen nog niet opgeslagen';
  }
  function copyMap(m) { var o = {}; A.BUTTONS.forEach(function (b) { o[b.id] = m[b.id] || 'none'; }); return o; }
  function loadKnoppen(force) {
    if (!btnBuilt) buildKnoppen();
    if (!force && A.mapChanges(btnBase, btnEdit).count > 0) return Promise.resolve();
    return getStatus().then(function (st) {
      btnBase = copyMap((st.gun_buttons && st.gun_buttons.map) || {});
      btnEdit = copyMap(btnBase);
      applyEditToSelects();
    }).catch(function (e) { $('btn-err').textContent = errMsg(e); });
  }
  function lightButtons() {
    var down = A.pressedSet(last && last.buttons && last.buttons.down, last ? last.trigger : 0);
    Object.keys(btnRows).forEach(function (id) { btnRows[id].classList.toggle('down', !!down[id]); });
    var linked = last && last.link && last.link !== 'disconnected' && last.link !== 'lost';
    $('btn-live').textContent = linked ? 'De Move van speler ' + player + ' is verbonden: druk op een knop om te zien welke regel oplicht.' : 'Zet de Move van speler ' + player + ' aan (PS-knop) om de knoppen uit te proberen.';
  }
  $('btn-save').addEventListener('click', function (e) {
    var ch = A.mapChanges(btnBase, btnEdit);
    if (!ch.count) return;
    $('btn-err').textContent = ''; $('btn-msg').textContent = '';
    submit('/api/gun-buttons', P({ map: ch.changes }), $('btn-err'), null, e.target, function (r) {
      btnBase = copyMap(r.map || {});
      btnEdit = copyMap(btnBase);
      applyEditToSelects();
      $('btn-msg').textContent = r.saved ? 'Opgeslagen. De nieuwe indeling werkt meteen.' : 'De nieuwe indeling werkt, maar is niet bewaard' + (r.save_error ? ': ' + r.save_error : '') + '.';
      $('btn-msg').className = r.saved ? 'msg' : 'msg bad';
      toast('Knoppen opgeslagen');
    });
  });
  $('btn-undo').addEventListener('click', function () { btnEdit = copyMap(btnBase); applyEditToSelects(); $('btn-err').textContent = ''; });
  $('btn-defaults').addEventListener('click', function () {
    var d = defaults && defaults.gun_buttons && defaults.gun_buttons.map;
    if (!d) { $('btn-err').textContent = 'De standaardindeling is niet beschikbaar.'; return; }
    btnEdit = copyMap(d);
    applyEditToSelects();
    $('btn-err').textContent = '';
    $('btn-msg').textContent = 'Standaardindeling klaargezet: kies Opslaan om hem te gebruiken.';
    $('btn-msg').className = 'msg';
  });

  // ---- LED en camera ----
  var presetList = [
    { name: 'cyaan', rgb: [0, 255, 255], hue_deg: -75, label: 'Cyaan' },
    { name: 'magenta', rgb: [255, 0, 255], hue_deg: 86, label: 'Magenta' },
    { name: 'rood', rgb: [255, 0, 0], hue_deg: 119, label: 'Rood' },
    { name: 'groen', rgb: [0, 255, 0], hue_deg: -145, label: 'Groen' },
    { name: 'blauw', rgb: [0, 0, 255], hue_deg: -31, label: 'Blauw' },
    { name: 'wit', rgb: [255, 255, 255], hue_deg: null, label: 'Wit' }
  ];
  var activeLed = null;
  function renderPresets() {
    var box = $('led-presets');
    box.textContent = '';
    presetList.forEach(function (p) {
      var b = el('button', 'swatch' + (activeLed && activeLed.join() === p.rgb.join() ? ' active' : ''));
      b.type = 'button';
      var chip = el('span', 'chip');
      chip.style.background = rgbCss(p.rgb[0], p.rgb[1], p.rgb[2]);
      var t = el('span');
      t.appendChild(document.createTextNode(p.label));
      t.appendChild(el('small', '', p.hue_deg === null ? 'camera ziet de bol niet' : ''));
      b.appendChild(chip); b.appendChild(t);
      b.addEventListener('click', function () { applyLed({ preset: p.name }, $('led-err'), b); });
      box.appendChild(b);
    });
  }
  function showLedResult(r) {
    var box = $('led-result');
    box.textContent = '';
    box.hidden = false;
    var tr = r.tracker;
    box.appendChild(el('p', '', 'De LED staat op ' + (r.led.custom ? 'een eigen kleur' : r.led.preset) + '.'));
    box.appendChild(el('p', '', tr.changed
      ? 'De camera zoekt nu naar die kleur' + (tr.applied_live ? '.' : ' (geldt bij de volgende start: de camera is niet actief).')
      : 'De zoekkleur van de camera is niet veranderd.'));
    r.warnings.forEach(function (w) { box.appendChild(el('p', 'warnbox', w)); });
    box.appendChild(el('p', 'muted', r.saved ? 'Bewaard.' : 'Niet bewaard' + (r.save_error ? ': ' + r.save_error : '') + '.'));
  }
  function applyLed(body, errEl, btn) {
    submit('/api/led', P(body), errEl, 'LED ingesteld voor speler ' + player, btn, function (r) {
      activeLed = r.led.rgb;
      renderPresets();
      showLedResult(r);
    });
  }
  function syncColor() {
    var r = Number($('led-r').value) || 0, g = Number($('led-g').value) || 0, b = Number($('led-b').value) || 0;
    function c(x) { return hex2(Math.max(0, Math.min(255, x))); }
    $('led-color').value = '#' + c(r) + c(g) + c(b);
  }
  $('led-color').addEventListener('input', function () {
    var v = $('led-color').value;
    $('led-r').value = parseInt(v.substr(1, 2), 16);
    $('led-g').value = parseInt(v.substr(3, 2), 16);
    $('led-b').value = parseInt(v.substr(5, 2), 16);
  });
  ['led-r', 'led-g', 'led-b'].forEach(function (id) { $(id).addEventListener('input', syncColor); });
  $('f-led').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var r = num('led-r', 0, 255, 'Rood', errs, true), g = num('led-g', 0, 255, 'Groen', errs, true), b = num('led-b', 0, 255, 'Blauw', errs, true);
    $('led-cerr').textContent = errs.join('. ');
    if (errs.length) return;
    applyLed({ r: r, g: g, b: b }, $('led-cerr'), e.target.querySelector('button'));
  });

  function findCtrl(cam, test) {
    var list = cam.controls || [];
    for (var i = 0; i < list.length; i++) if (test(list[i].name.toLowerCase())) return list[i];
    return null;
  }
  var camState = { ae: false, ag: false };
  function camApply(body) {
    return call('/api/camera', body).then(function (j) {
      var r = j.result;
      setHidden($('cam-warn'), !r.warnings.length, r.warnings.join('. '));
      return r;
    });
  }
  function buildCamera() {
    switches.cam_ae = makeSwitch($('sw-cam'), {
      label: 'Belichting automatisch', errEl: $('cam-err'),
      apply: function (on) { return camApply({ auto_exposure: on }).then(function (r) { camState.ae = r.controls.auto_exposure; camAuto(); return r.controls.auto_exposure; }); }
    });
    switches.cam_ag = makeSwitch($('sw-cam'), {
      label: 'Gain automatisch', errEl: $('cam-err'),
      apply: function (on) { return camApply({ auto_gain: on }).then(function (r) { camState.ag = r.controls.auto_gain; camAuto(); return r.controls.auto_gain; }); }
    });
    switches.cam_awb = makeSwitch($('sw-cam'), {
      label: 'Witbalans automatisch', errEl: $('cam-err'),
      apply: function (on) { return camApply({ auto_awb: on }).then(function (r) { return r.controls.auto_awb; }); }
    });
    var sl = $('sl-camera');
    sliders.cexp = makeSlider(sl, {
      name: 'Belichting', help: 'Hoe lang de camera licht opvangt. Te hoog: de bol wordt wit. Alleen als de belichting niet automatisch is.',
      min: 0, max: 255, step: 1, fmt: function (v) { return fmt(v, 0); },
      send: function (v) { return camApply({ exposure: Math.round(v) }); }, actual: function (r) { return r.controls.exposure; }
    });
    sliders.cgain = makeSlider(sl, {
      name: 'Gain', help: 'Versterking van het camerabeeld. Houd dit laag. Alleen als de gain niet automatisch is.',
      min: 0, max: 63, step: 1, fmt: function (v) { return fmt(v, 0); },
      send: function (v) { return camApply({ gain: Math.round(v) }); }, actual: function (r) { return r.controls.gain; }
    });
    $('cam-mode').addEventListener('change', function () {
      var m = $('cam-mode');
      $('cam-err').textContent = '';
      m.disabled = true;
      camApply({ mode: m.value }).then(function (r) { m.value = r.mode.width + 'x' + r.mode.height + '@' + r.mode.fps; toast('Beeldformaat aangepast'); },
        function (e) { $('cam-err').textContent = errMsg(e); initCamera(); }).then(function () { m.disabled = false; });
    });
  }
  function camAuto() {
    sliders.cexp.setDisabled(camState.ae);
    sliders.cgain.setDisabled(camState.ag);
  }
  function refreshCamera() {
    return api('/api/camera').then(function (j) {
      if (!j.ok) return;
      var cam = j.result, fr = cam.frames || {}, m = cam.mode || {};
      var live = cam.enabled && cam.state === 'streaming';
      $('cam-state').textContent = !cam.enabled ? 'De camera staat uit (de daemon draait zonder camera).' :
        (live ? 'Camera werkt: ' + fmt(fr.fps_5s, 0) + ' beelden per seconde.' : 'De camera werkt nu niet' + (cam.error ? ': ' + cam.error : '') + '.');
      if (!live) return;
      var ex = findCtrl(cam, function (n) { return n.indexOf('exposure') >= 0 && n.indexOf('auto') < 0; });
      var ga = findCtrl(cam, function (n) { return n.indexOf('gain') >= 0 && n.indexOf('auto') < 0; });
      var ag = findCtrl(cam, function (n) { return n.indexOf('gain') >= 0 && n.indexOf('auto') >= 0; });
      var ae = findCtrl(cam, function (n) { return n.indexOf('exposure') >= 0 && n.indexOf('auto') >= 0; });
      var wb = findCtrl(cam, function (n) { return n.indexOf('white') >= 0 && n.indexOf('auto') >= 0; });
      if (m.width && !$('cam-mode').disabled) $('cam-mode').value = m.width + 'x' + m.height + '@' + m.requested_fps;
      if (ex && ex.value !== null) sliders.cexp.set(ex.value);
      if (ga && ga.value !== null) sliders.cgain.set(ga.value);
      // auto_exposure is een menu: 0 = automatisch, 1 = handmatig.
      if (ae && ae.value !== null) { camState.ae = ae.value !== 1; switches.cam_ae.set(camState.ae); }
      if (ag && ag.value !== null) { camState.ag = ag.value !== 0; switches.cam_ag.set(camState.ag); }
      if (wb && wb.value !== null) switches.cam_awb.set(wb.value !== 0);
      camAuto();
    }).catch(function () {});
  }
  function initCamera() { return refreshCamera(); }
  function refreshLed() {
    getStatus().then(function (s) {
      var led = s.controller.led;
      if (led.enabled && (!activeLed || activeLed.join() !== led.rgb.join())) { activeLed = led.rgb; renderPresets(); }
    }).catch(function () {});
    refreshCamera();
  }

  // ---- Games ----
  var gamesSig = '';
  var gameCards = {};
  var gameBusy = false;
  function buildGames(list) {
    var box = $('game-list');
    box.textContent = '';
    gameCards = {};
    list.forEach(function (g) {
      var card = el('article', 'card game');
      var icon = el('div', 'icon');
      if (g.icon) {
        var img = el('img');
        img.alt = '';
        img.src = g.icon;
        img.addEventListener('error', function () { icon.textContent = g.name.charAt(0).toUpperCase(); });
        icon.appendChild(img);
      } else icon.textContent = g.name.charAt(0).toUpperCase();
      var body = el('div');
      body.appendChild(el('p', 'gname', g.name));
      if (g.note) body.appendChild(el('p', 'gnote', g.note));
      var state = el('p', 'gstate', '');
      var actions = el('p', 'actions');
      var play = el('button', '', 'Spelen');
      play.type = 'button';
      var stop = el('button', 'danger', 'Stoppen');
      stop.type = 'button';
      play.addEventListener('click', function () { gameStart(g.profile, play); });
      stop.addEventListener('click', function () { gameStop(stop); });
      actions.appendChild(play); actions.appendChild(stop);
      body.appendChild(state);
      card.appendChild(icon); card.appendChild(body); card.appendChild(actions);
      box.appendChild(card);
      gameCards[g.profile] = { card: card, state: state, play: play, stop: stop };
    });
    if (!list.length) box.appendChild(el('p', 'muted', 'Er zijn geen games gevonden.'));
  }
  function updateGames(status) {
    var v = A.gameStateView(status);
    $('game-banner').textContent = (status && status.text) || v.text;
    $('game-banner').className = 'big ' + (v.tone === 'ok' ? 'ok' : v.tone === 'bad' ? 'bad' : v.tone === 'warn' ? 'warn' : '');
    $('game-banner-sub').textContent = status && status.state === 'failed' ? (status.message || '') : (status && status.profile ? gameName(status.profile) : '');
    $('game-stop-all').hidden = !v.canStop;
    $('game-stop-all').disabled = false;
    Object.keys(gameCards).forEach(function (p) {
      var c = gameCards[p], cs = A.gameCardState(status, p);
      c.card.className = 'card game' + (cs.active ? ' active' : cs.failed ? ' failed' : '');
      c.state.textContent = cs.active ? v.text : (cs.failed ? v.text : '');
      c.state.className = 'gstate ' + (cs.active ? (v.tone === 'ok' ? 'ok' : 'warn') : cs.failed ? 'bad' : '');
      c.play.disabled = !v.canStart || gameBusy;
      var mine = cs.active || (status && !status.profile);
      c.stop.hidden = !(v.canStop && mine);
    });
  }
  function refreshGames() {
    return api('/api/games').then(function (j) {
      if (!j.ok) { $('game-err').textContent = errText(j); return; }
      var r = j.result;
      gamesCache = r;
      var sig = JSON.stringify(r.games);
      if (sig !== gamesSig) { gamesSig = sig; buildGames(r.games); }
      $('game-hint').textContent = r.available ? 'Spelen zet de lightgun aan en start de game. Heb je de Move nog niet aangezet, druk dan op de PS-knop.' : (r.message || '');
      updateGames(r.status);
    }).catch(function () {});
  }
  function gameStart(profile, btn) {
    $('game-err').textContent = '';
    gameBusy = true; btn.disabled = true;
    api('/api/games/start', { profile: profile }).then(function (j) {
      gameBusy = false;
      if (!j.ok) $('game-err').textContent = errText(j);
      else updateGames(j.result);
      refreshGames();
    }).catch(function () { gameBusy = false; $('game-err').textContent = 'Het programma op de achtergrond is niet bereikbaar.'; });
  }
  function gameStop(btn) {
    $('game-err').textContent = '';
    btn.disabled = true;
    api('/api/games/stop', {}).then(function (j) {
      btn.disabled = false;
      if (!j.ok) $('game-err').textContent = errText(j);
      else updateGames(j.result);
      refreshGames();
    }).catch(function () { btn.disabled = false; $('game-err').textContent = 'Het programma op de achtergrond is niet bereikbaar.'; });
  }
  $('game-stop-all').addEventListener('click', function (e) { gameStop(e.target); });

  // ---- Controllers: lijst, koppelwizard, naam/speler, knipperen, verwijderen ----
  var ctrlSig = '';
  var ctrlCards = {};
  var ctrlEditing = {};   // sleutel -> naamveld heeft de focus of is gewijzigd (niet overschrijven)
  var pairActive = false;
  function renderPair(p, helper) {
    var v = A.pairView(p);
    pairActive = v.active;
    var ol = $('pair-steps');
    ol.textContent = '';
    A.PAIR_STEPS.forEach(function (name, i) {
      ol.appendChild(el('li', i < v.step ? 'done' : (i === v.step && v.state !== 'idle' && v.state !== 'cancelled' && v.state !== 'failed' ? 'now' : ''), name));
    });
    $('pair-text').textContent = v.text;
    $('pair-text').className = 'big ' + (v.tone === 'ok' ? 'ok' : v.tone === 'bad' ? 'bad' : v.tone === 'warn' ? 'warn' : '');
    var sub = v.message;
    if (v.state === 'done' && p && p.player) sub = 'De controller is speler ' + p.player + '. Je kunt de naam en de speler hieronder aanpassen.';
    if (v.busy) sub = sub || 'Even geduld...';
    $('pair-msg').textContent = sub;
    $('pair-install').hidden = !v.showInstall;
    $('pair-cmd').textContent = (p && p.helper && p.helper.install_command) || (helper && helper.install_command) || '';
    $('pair-start').hidden = !v.canStart;
    $('pair-start').textContent = v.startLabel;
    $('pair-confirm').hidden = !v.canConfirm;
    $('pair-confirm').textContent = v.confirmLabel || 'Doorgaan';
    $('pair-cancel').hidden = !v.canCancel;
  }
  function pairAction(action, btn) {
    $('pair-err').textContent = '';
    if (btn) btn.disabled = true;
    api('/api/controllers/pair', { action: action }).then(function (j) {
      if (btn) btn.disabled = false;
      if (!j.ok) { $('pair-err').textContent = errText(j); return; }
      renderPair(j.result, null);
      restartCtrlPoll();
    }).catch(function () { if (btn) btn.disabled = false; $('pair-err').textContent = 'Het programma op de achtergrond is niet bereikbaar.'; });
  }
  $('pair-start').addEventListener('click', function (e) { pairAction('start', e.target); });
  $('pair-confirm').addEventListener('click', function (e) { pairAction('confirm', e.target); });
  $('pair-cancel').addEventListener('click', function (e) { pairAction('cancel', e.target); });

  function ctrlUpdate(c, body, okMsg, errEl, btn) {
    var b = { controller: c.key };
    Object.keys(body).forEach(function (k) { b[k] = body[k]; });
    return submit('/api/controllers/update', b, errEl, okMsg, btn, function () { ctrlSig = ''; refreshControllers(); });
  }
  function buildCtrlCard(c, nowS) {
    var card = el('article', 'card ctrl' + (c.removed ? ' removed' : ''));
    var head = el('div', 'chead');
    head.appendChild(ledChip(c.type === 'move' && c.led ? c.led.rgb : null, 'ledbig'));
    var name = el('p', 'cname', c.name);
    head.appendChild(name);
    head.appendChild(el('span', 'ctype', A.controllerTypeLabel(c.type) + (c.model ? ' (' + c.model + ')' : '')));
    card.appendChild(head);
    var bv = A.controllerBattery(c);
    var dl = el('dl');
    var rows = [
      ['Speler', c.removed ? 'geen (verwijderd)' : (c.player ? (c.type === 'move' ? 'speler ' + c.player : 'hoort bij speler ' + c.player) : 'geen'), ''],
      ['Verbonden', c.connected ? 'ja' + (c.connection === 'usb' ? ' (USB)' : '') : 'nee', c.connected ? 'ok' : 'warn'],
      ['Batterij', c.connected ? bv.label + (bv.detail ? ' (' + bv.detail + ')' : '') : '-', bv.level === 'low' ? 'bad' : ''],
      ['Laatst gezien', c.connected ? 'nu' : A.lastSeenText(c.last_seen_unix, nowS), ''],
      ['Gekoppeld aan deze pc', c.paired ? 'ja' : 'nee', c.paired ? '' : 'warn']
    ];
    if (c.overrides) rows.push(['Eigen instellingen', c.overrides + ' afwijkend van de standaard', '']);
    fill(dl, rows);
    card.appendChild(dl);
    var err = el('p', 'err');
    err.setAttribute('role', 'alert');
    if (!c.known) {
      card.appendChild(el('p', 'hint', 'Bluetooth kent deze controller, maar RavenGun nog niet. Zet hem aan met de PS-knop.'));
    }
    if (c.known && !c.removed) {
      // Naam en speler.
      var form = el('div', 'rowform');
      var lab = el('label', '', 'Naam');
      var inp = el('input');
      inp.type = 'text'; inp.maxLength = 32; inp.value = c.name;
      inp.addEventListener('input', function () { ctrlEditing[c.key] = true; });
      lab.appendChild(inp);
      form.appendChild(lab);
      var save = el('button', 'secondary', 'Naam bewaren');
      save.type = 'button';
      save.addEventListener('click', function () {
        var v = inp.value.trim();
        if (!v) { err.textContent = 'Geef een naam van 1 tot 32 tekens.'; return; }
        ctrlEditing[c.key] = false;
        ctrlUpdate(c, { name: v }, 'Naam bewaard', err, save);
      });
      form.appendChild(save);
      card.appendChild(form);
      var pl = el('label', '', c.type === 'move' ? 'Speler (een bezette speler wordt geruild)' : 'Hoort bij speler');
      var sel = el('select');
      for (var i = 1; i <= A.MAX_PLAYERS; i++) sel.appendChild(new Option('Speler ' + i, String(i)));
      sel.value = String(c.player || 1);
      sel.addEventListener('change', function () {
        ctrlUpdate(c, { new_player: Number(sel.value) }, 'Spelernummer aangepast', err, sel);
      });
      pl.appendChild(sel);
      card.appendChild(pl);
    }
    var acts = el('p', 'actions');
    if (c.type === 'move' && c.known && !c.removed) {
      var blink = el('button', 'secondary', 'Knipperen');
      blink.type = 'button';
      blink.title = 'De LED van deze Move knippert 3 seconden wit, zodat je ziet welke het is.';
      blink.addEventListener('click', function () {
        err.textContent = '';
        submit('/api/controllers/blink', { controller: c.key }, err, 'De LED knippert', blink);
      });
      acts.appendChild(blink);
    }
    if (c.overrides && c.known && !c.removed) {
      var reset = el('button', 'secondary', 'Standaardinstellingen');
      reset.type = 'button';
      reset.addEventListener('click', function () {
        if (!window.confirm('De eigen instellingen van ' + c.name + ' wissen? Dan gelden weer de standaardinstellingen.')) return;
        ctrlUpdate(c, { reset_settings: true }, 'Standaardinstellingen hersteld', err, reset);
      });
      acts.appendChild(reset);
    }
    var del = el('button', 'danger', 'Verwijderen');
    del.type = 'button';
    acts.appendChild(del);
    card.appendChild(acts);
    // Verwijderen met bevestiging: instellingen bewaren of wissen.
    var conf = el('div', 'confirm');
    conf.hidden = true;
    conf.appendChild(el('p', '', c.name + ' verwijderen? De controller wordt bij Bluetooth ontkoppeld. Bewaar je de instellingen, dan krijgt hij ze terug als je hem opnieuw koppelt.'));
    var ca = el('p', 'actions');
    var keep = el('button', 'danger', 'Verwijderen, instellingen bewaren');
    var forget = el('button', 'danger', 'Verwijderen en instellingen wissen');
    var no = el('button', 'secondary', 'Annuleren');
    [keep, forget, no].forEach(function (b) { b.type = 'button'; ca.appendChild(b); });
    conf.appendChild(ca);
    card.appendChild(conf);
    del.addEventListener('click', function () { conf.hidden = false; del.hidden = true; });
    no.addEventListener('click', function () { conf.hidden = true; del.hidden = false; });
    function remove(keepSettings, btn) {
      submit('/api/controllers/unpair', { controller: c.key, keep_settings: keepSettings }, err,
        keepSettings ? 'Verwijderd; instellingen bewaard' : 'Verwijderd', btn, function () { ctrlSig = ''; refreshControllers(); });
    }
    keep.addEventListener('click', function () { remove(true, keep); });
    forget.addEventListener('click', function () { remove(false, forget); });
    card.appendChild(err);
    return card;
  }
  function refreshControllers() {
    return api('/api/controllers').then(function (j) {
      if (!j.ok) { $('ctrl-err').textContent = errText(j); return; }
      $('ctrl-err').textContent = '';
      var r = j.result;
      renderPair(r.pairing, r.helper);
      // Opnieuw opbouwen als er iets veranderd is, maar niet terwijl iemand een naam aan het typen is.
      var typing = Object.keys(ctrlEditing).some(function (k) { return ctrlEditing[k]; });
      var sig = JSON.stringify(r.controllers.map(function (c) {
        return [c.key, c.name, c.player, c.connected, c.removed, c.paired, c.known, c.battery && c.battery.code, c.overrides, c.led && c.led.rgb, Math.floor((c.last_seen_unix || 0) / 60)];
      }));
      if (sig !== ctrlSig && !typing) {
        ctrlSig = sig;
        var box = $('ctrl-list');
        box.textContent = '';
        var nowS = Date.now() / 1000;
        r.controllers.forEach(function (c) { box.appendChild(buildCtrlCard(c, nowS)); });
        if (!r.controllers.length) box.appendChild(el('p', 'muted', 'Nog geen controllers. Koppel er een met de knop hierboven.'));
      }
      var notes = [];
      if (!r.adapter.found) notes.push('Er is geen Bluetooth-adapter gevonden' + (r.adapter.error ? ' (' + r.adapter.error + ')' : '') + '.');
      if (!r.helper.installed) notes.push('Koppelen vraagt eenmalig een hulpprogramma met beheerrechten; de wizard laat zien hoe.');
      $('ctrl-hint').textContent = (notes.length ? notes.join(' ') + ' ' : '') +
        'Elke Move is een speler met een eigen pistool in de game en eigen instellingen. Een Navigation Controller hoort bij een speler: zijn knoppen kies je bij Knoppen van die speler.';
    }).catch(function () { setConn(false); });
  }
  function restartCtrlPoll() {
    if (current !== 'controllers') return;
    clearInterval(pollTimer);
    pollTimer = setInterval(refreshControllers, pairActive ? 700 : 3000);
  }

  // ---- Info (technisch) ----
  function moveRows(s) {
    var c = s.controller;
    return [
      ['Verbonden', c.connected ? 'ja' : 'nee, geen Move gevonden', c.connected ? 'ok' : 'warn'],
      ['Model', c.model || '-'],
      ['Verbinding', c.connection || '-'],
      ['Controller-ID (afgeschermd)', c.id || '-'],
      ['Kalibratie in de controller', c.connected ? yn(c.has_calibration) : '-'],
      ['Magnetometer', c.magnetometer],
      ['Batterij', c.connected ? c.battery.text : '-'],
      ['Verbonden sinds', c.connected ? dur(c.connected_s) : '-'],
      ['Verbindingen / verbroken', c.connects + ' / ' + c.disconnects]
    ];
  }
  function camRows(s) {
    var cam = s.camera || {}, d = cam.device || {}, m = cam.mode || {};
    if (!cam.enabled) return [['Camera', 'uit (--no-camera)', 'muted']];
    return [
      ['Camera: toestand', cam.state, cam.state === 'streaming' ? 'ok' : 'warn'],
      ['Camera: model', d.card || '-'],
      ['Camera: driver', d.driver || '-'],
      ['Camera: vast pad', d.by_id || d.by_path || d.node || '-'],
      ['Camera: modus', m.width ? m.width + 'x' + m.height + ' ' + (m.format || '') + ' @ ' + fmt(m.driver_fps, 0) + ' fps' : '-']
    ];
  }
  function outRows(o) {
    var d = o.device || {};
    return [
      ['Lightgun aan', o.armed ? 'ja (stuurt invoer)' : 'nee (stuurt niets)', o.armed ? 'warn' : ''],
      ['Apparaat', d.name || '-'],
      ['Vaste link', d.stable_link || '-'],
      ['Event-node', d.created ? (d.event_node || '-') : 'nog niet aangemaakt (eerste keer aanzetten maakt het)'],
      ['USB-id', d.vendor ? d.vendor + ':' + d.product : '-']
    ];
  }
  function fusionRows(f) {
    if (!f) return [];
    return [
      ['Schatter (in de sample)', f.mode],
      ['Yaw-bron', f.yaw_source === 'magnetometer' ? 'kompas-gecorrigeerd' : 'gyro', f.yaw_source === 'magnetometer' ? 'ok' : ''],
      ['Magnetometer', f.magnetometer + (f.mag_locked ? ', vergrendeld' : ''), f.magnetometer === 'disturbed' ? 'warn' : ''],
      ['Openstaande yaw-fout', fmt(f.yaw_pending_deg, 2) + ' graden'],
      ['Correctie', fmt(f.correction_rate_deg_s, 3) + ' graden/s (grens ' + fmt(f.correction_limit_deg_s, 3) + '), totaal ' + fmt(f.correction_total_deg, 2) + ' graden'],
      ['Verticale bias (magnetometer)', fmt(f.vertical_bias_deg_s, 3) + ' graden/s']
    ];
  }
  var outFormFilled = false;
  function refreshInfo() {
    Promise.all([api('/api/status'), api('/api/info'), api('/api/sample'), api('/api/settings'), api('/api/output')]).then(function (res) {
      if (!res[0].ok || !res[1].ok) return;
      var s = res[0].result, inf = res[1].result, v = inf.version, cam = s.camera || {}, fr = cam.frames || {}, r = s.reports;
      var p = res[2].ok ? res[2].result : null;
      fill($('inf-ver'), [
        ['Daemon', v.daemon], ['Git-commit', v.git_commit], ['PSMoveAPI-pin', v.psmoveapi_pin],
        ['PSMoveAPI gelinkt', yn(v.psmoveapi_linked), v.psmoveapi_linked ? 'ok' : 'warn'],
        ['IPC-protocol', String(v.ipc_protocol)], ['Invoerschema', String(v.input_schema)]
      ]);
      fill($('inf-dev'), moveRows(s).concat(camRows(s)));
      fill($('inf-daemon'), [
        ['Looptijd', dur(inf.daemon.uptime_s)], ['PID', String(inf.daemon.pid)],
        ['Webpoort', inf.daemon.web_port ? String(inf.daemon.web_port) : 'uit'],
        ['LAN-toegang', inf.daemon.web_lan ? 'aan (token vereist)' : 'uit (alleen deze computer)']
      ]);
      var iv = r.interval_ms || {}, hp = s.processing_us || {};
      fill($('inf-imu'), [
        ['Rapporten per seconde (5 s)', fmt(r.rate_hz_5s)],
        ['Interval p50 / p95 / max (ms)', fmt(iv.p50, 2) + ' / ' + fmt(iv.p95, 2) + ' / ' + fmt(iv.max, 1)],
        ['Gaten (volgnummer overgeslagen)', String(r.dropped_by_seq)],
        ['Lange tussenpozen (50 tot 250 ms)', String(r.long_dt_50_250ms)],
        ['Gaten niet geintegreerd', String(r.gaps_not_integrated)],
        ['Verwerking p50 / p95 / max (µs)', fmt(hp.p50, 0) + ' / ' + fmt(hp.p95, 0) + ' / ' + fmt(hp.max, 0)]
      ]);
      if (!cam.enabled) fill($('inf-cam'), [['Camera', 'uit (--no-camera)', 'muted']]);
      else {
        var pu = cam.processing_us || {}, age = cam.age_at_processing_ms || {}, q = cam.queue || {};
        fill($('inf-cam'), [
          ['Beelden per seconde (5 s)', fmt(fr.fps_5s)], ['Beelden ontvangen', String(fr.received)],
          ['Gemiste beelden', String(fr.missed_by_sequence)], ['Foutbeelden / streamfouten', fr.error_frames + ' / ' + fr.stream_errors],
          ['Weggegooid door de wachtrij', String((q.dropped_full || 0) + (q.dropped_stale || 0))],
          ['Verwerking p50 / p95 / max (µs)', fmt(pu.p50, 0) + ' / ' + fmt(pu.p95, 0) + ' / ' + fmt(pu.max, 0)],
          ['Leeftijd bij verwerking p95 (ms)', fmt(age.p95, 1)]
        ]);
      }
      var tr = s.yaw_trim || {};
      var trackRows = [
        ['Driftcorrectie (ingesteld)', A.fusionLabel(s.config.fusion.mode)],
        ['Filter (ingesteld)', s.config.filter.enabled ? 'aan' : 'uit'],
        ['Leren van centreren', tr.learning ? 'aan' : 'uit'],
        ['Geleerde correctie', typeof tr.trim_deg_s === 'number' ? fmt(tr.trim_deg_s, 3) + ' graden/s' : '-'],
        ['Centreerdrukken gezien / geleerd', (tr.presses !== undefined ? tr.presses : '-') + ' / ' + (tr.learned !== undefined ? tr.learned : '-')]
      ];
      if (p) {
        trackRows.push(['Midden bepaald door', p.aim.center_source]);
        trackRows.push(['Oriëntatie geldig', yn(p.orientation.valid)]);
        trackRows.push(['Aim geldig', yn(p.aim.valid)]);
        trackRows = trackRows.concat(fusionRows(p.orientation.fusion));
      }
      fill($('inf-track'), trackRows);
      if (res[3].ok) {
        var i = res[3].result.info;
        if (res[3].result.values) $('web-port').value = $('web-port').matches(':focus') ? $('web-port').value : res[3].result.values.web.port;
        fill($('cfg-info'), [
          ['Bestand', i.available ? i.path : 'geen (--no-config)'], ['Aanwezig', yn(i.file_present)],
          ['Toestand', i.state, i.state === 'loaded' ? 'ok' : (i.state === 'missing' ? '' : 'warn')], ['Schemaversie', String(i.schema)]
        ]);
        var msgs = [];
        if (i.message) msgs.push(i.message);
        (i.warnings || []).forEach(function (m) { msgs.push(m); });
        if (i.last_save_error) msgs.push('Laatste opslagfout: ' + i.last_save_error);
        setHidden($('cfg-msg'), !msgs.length, msgs.join('. '));
      }
      fill($('web-now'), [
        ['Actieve poort', inf.daemon.web_port ? String(inf.daemon.web_port) : 'uit'],
        ['LAN-toegang', inf.daemon.web_lan ? 'aan (token vereist)' : 'uit (alleen deze computer)']
      ]);
      if (res[4].ok) showOutput(res[4].result);
    }).catch(function () { setConn(false); });
  }
  function showOutput(o) {
    var p = o.pattern || {}, pos = o.position || {};
    var rows = outRows(o);
    rows.push(['Laatste positie', typeof pos.nx === 'number' ? fmt(pos.nx, 3) + ', ' + fmt(pos.ny, 3) : '-']);
    rows.push(['Knoppen ingedrukt', o.buttons_down && o.buttons_down.length ? o.buttons_down.join(', ') : 'geen']);
    rows.push(['Toetsen ingedrukt', o.keys_down && o.keys_down.length ? o.keys_down.join(', ') : 'geen']);
    fill($('out-state'), rows);
    var prow = [['Toestand', p.state || '-']];
    if (p.kind) prow.push(['Patroon', p.kind + (p.steps ? ' (stap ' + p.step + ' van ' + p.steps + ')' : '')]);
    fill($('out-pattern'), prow);
    $('out-arm').disabled = !!o.armed;
    $('out-disarm').disabled = !o.armed;
    Array.prototype.forEach.call(document.querySelectorAll('.pat'), function (b) {
      b.disabled = !o.armed && b.getAttribute('data-pattern') !== 'stop';
    });
    if (!outFormFilled && o.transform) {
      var t = o.transform, v = t.viewport || {};
      $('o-ix').checked = !!t.invert_x; $('o-iy').checked = !!t.invert_y;
      $('o-sx').value = t.scale_x; $('o-sy').value = t.scale_y;
      $('o-ox').value = t.offset_x; $('o-oy').value = t.offset_y;
      $('o-x0').value = v.x0; $('o-y0').value = v.y0; $('o-x1').value = v.x1; $('o-y1').value = v.y1;
      outFormFilled = true;
    }
  }
  $('out-arm').addEventListener('click', function (e) {
    if (!window.confirm('De lightgun inschakelen? Games krijgen dan muisbeweging en knoppen van de Move.')) return;
    submit('/api/arm', {}, $('out-err'), 'Lightgun staat aan', e.target, refreshInfo);
  });
  $('out-disarm').addEventListener('click', function (e) {
    submit('/api/disarm', {}, $('out-err'), 'Lightgun staat uit', e.target, refreshInfo);
  });
  Array.prototype.forEach.call(document.querySelectorAll('.pat'), function (b) {
    b.addEventListener('click', function () {
      var name = b.getAttribute('data-pattern');
      var body = { pattern: name };
      if (name === 'grid' || name === 'corners') body.dwell_ms = 600;
      submit('/api/test-pattern', body, $('pat-err'), name === 'stop' ? 'Patroon gestopt' : 'Patroon gestart', b, refreshInfo);
    });
  });
  $('f-output').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var body = { invert_x: $('o-ix').checked, invert_y: $('o-iy').checked };
    body.scale_x = num('o-sx', 0.05, 20, 'Schaal X', errs);
    body.scale_y = num('o-sy', 0.05, 20, 'Schaal Y', errs);
    body.offset_x = num('o-ox', -1, 1, 'Verschuiving X', errs);
    body.offset_y = num('o-oy', -1, 1, 'Verschuiving Y', errs);
    body.viewport_x0 = num('o-x0', 0, 1, 'Beeld links', errs);
    body.viewport_y0 = num('o-y0', 0, 1, 'Beeld boven', errs);
    body.viewport_x1 = num('o-x1', 0, 1, 'Beeld rechts', errs);
    body.viewport_y1 = num('o-y1', 0, 1, 'Beeld onder', errs);
    $('o-err').textContent = errs.join('. ');
    if (errs.length) return;
    submit('/api/output', body, $('o-err'), 'Toegepast', e.target.querySelector('button'), refreshInfo);
  });
  $('f-web').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var port = num('web-port', 1, 65535, 'Poort', errs, true);
    $('web-err').textContent = errs.join('. ');
    if (errs.length) return;
    submit('/api/web', { port: port }, $('web-err'), 'Poort bewaard (geldt na een herstart)', e.target.querySelector('button'), refreshInfo);
  });

  // ---- Navigatie ----
  var built = {};
  var handlers = {
    overzicht: { show: refreshOverview, poll: refreshOverview, ms: 1500 },
    richten: { build: buildRichten, show: function () { refreshRichten(); refreshCal(); drawAim(); startStream(); }, poll: function () { refreshRichten(); refreshCal(); }, ms: 3000 },
    trillen: { build: buildTrillen, show: refreshTrillen, poll: refreshTrillen, ms: 3000 },
    knoppen: { show: function () { loadKnoppen(false).then(lightButtons); startStream(); } },
    led: { build: buildCamera, show: function () { renderPresets(); refreshLed(); drawMini(); startStream(); }, poll: refreshLed, ms: 3000 },
    games: { show: refreshGames, poll: refreshGames, ms: 2000 },
    controllers: { show: function () { ctrlSig = ''; refreshControllers().then(restartCtrlPoll); } },
    info: { show: function () { outFormFilled = false; refreshInfo(); }, poll: refreshInfo, ms: 2500 }
  };
  function remember(name) { try { window.localStorage.setItem('rg.tab', name); } catch (e) { /* geen opslag: dan onthouden we niets */ } }
  function recall() { try { return window.localStorage.getItem('rg.tab'); } catch (e) { return null; } }
  function renderTabs() {
    var nav = $('tabs');
    A.TABS.forEach(function (t) {
      var a = el('a', '', t.label);
      a.href = '#/' + t.id;
      a.setAttribute('data-page', t.id);
      nav.appendChild(a);
    });
  }
  function show(force) {
    var name = A.pickTab(location.hash, recall());
    if (name === current && !force) return;
    var changed = name !== current;
    current = name;
    remember(name);
    A.TABS.forEach(function (t) { $('p-' + t.id).className = 'page' + (t.id === name ? ' active' : ''); });
    Array.prototype.forEach.call(document.querySelectorAll('#tabs a'), function (a) {
      var on = a.getAttribute('data-page') === name;
      a.className = on ? 'active' : '';
      if (on) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
    });
    clearInterval(pollTimer);
    pollTimer = null;
    stopStream();
    $('playerbar').hidden = !A.hasPlayerBar(name);
    var h = handlers[name];
    if (h) {
      if (h.build && !built[name]) { built[name] = true; h.build(); }
      h.show();
      if (h.poll) pollTimer = setInterval(h.poll, h.ms);
    }
    if (changed) window.scrollTo(0, 0);
  }
  window.addEventListener('hashchange', function () { show(false); });
  document.addEventListener('visibilitychange', function () {
    if (document.hidden) { stopStream(); clearInterval(pollTimer); pollTimer = null; }
    else show(true);
  });
  renderTabs();
  api('/api/settings').then(function (j) {
    if (j.ok) {
      defaults = j.result.defaults || null;
      if (j.result.presets) {
        presetList = j.result.presets.map(function (p) {
          return { name: p.name, rgb: p.rgb, hue_deg: p.hue_deg, label: p.name.charAt(0).toUpperCase() + p.name.slice(1) };
        });
      }
    }
  }).catch(function () {}).then(function () { show(true); });
})();

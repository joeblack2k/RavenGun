'use strict';
// RavenGun kalibratie-UI voor de tv (/calibrate): stappenwizard, alleen bediend met de controller.
// Geen externe bronnen, geen inline code (CSP), alle tekst via textContent. De pagina zet nooit invoer om
// in game-acties: knopdrukken worden hier alleen als navigatie of meting gebruikt.
(function () {
  var C = window.RGCal;
  var $ = function (id) { return document.getElementById(id); };
  var csrf = (document.querySelector('meta[name="csrf-token"]') || {}).content || '';
  var now = function () { return performance.now(); };

  // ---- Hulpfuncties ----
  function api(path, body) {
    var opt = { cache: 'no-store' };
    if (body !== undefined) {
      opt.method = 'POST';
      opt.headers = { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf };
      opt.body = JSON.stringify(body);
    }
    return fetch(path, opt).then(function (r) { return r.json(); }).catch(function () {
      return { ok: false, error: { message: 'daemon niet bereikbaar' } };
    });
  }
  function errText(j) { return j && j.error ? j.error.message : 'onbekende fout'; }
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function setText(id, t) { var e = $(id); if (e.textContent !== t) e.textContent = t; }
  function show(id, on) { $(id).hidden = !on; }
  function fmt1(v) { return (Math.round(v * 10) / 10).toFixed(1).replace('.', ','); }

  // ---- Podium: vast 1920x1080, geschaald naar het venster ----
  function fit() {
    var f = C.stageFit(window.innerWidth, window.innerHeight, 1920, 1080);
    $('stage').style.transform = 'translate(' + f.left + 'px,' + f.top + 'px) scale(' + f.scale + ')';
  }
  window.addEventListener('resize', fit);
  fit();

  // ---- Toestand ----
  var sample = null, progress = null, link = 'disconnected', streamUp = false;
  var nav = new C.ButtonNav({ lockoutMs: 350 });
  var hold = new C.HoldTracker(1500);
  var mapc = new C.MappingCollector(200);
  var lastPress = null, lastMask = 0;
  var gyroEma = new C.Ema(250);
  var dps = null;
  var state = '', busy = false, timers = [];
  var results = { mag: null, still: null, buttons: null };
  var BTN_NAMES = ['t', 'move', 'ps', 'start', 'select', 'triangle', 'circle', 'cross', 'square'];
  var settingsButtons = {};

  var ROLES = [
    { k: 'trekker', title: 'de trekker', sub: 'Trek de trekker van de Sharp Shooter over.' },
    { k: 'pomp', title: 'de pomp', sub: 'Trek de pomp naar achteren (of doe wat de pomp bedient).' },
    { k: 'driehoek', title: 'Driehoek', sub: 'Druk op de knop met de driehoek.' },
    { k: 'cirkel', title: 'Cirkel', sub: 'Druk op de knop met de cirkel.' },
    { k: 'kruis', title: 'Kruis', sub: 'Druk op de knop met het kruis.' },
    { k: 'vierkant', title: 'Vierkant', sub: 'Druk op de knop met het vierkant.' },
    { k: 'start', title: 'Start', sub: 'Druk op de Start-knop.' },
    { k: 'select', title: 'Select', sub: 'Druk op de Select-knop.' },
    { k: 'move', title: 'Move-knop', sub: 'Druk op de grote Move-knop.' },
    { k: 'ps', title: 'PS-knop', sub: 'Druk op de PS-knop (kan ontoegankelijk zijn: dan overslaan door te wachten).' }
  ];
  var ROLE_LABEL = {};
  ROLES.forEach(function (r) { ROLE_LABEL[r.k] = r.title.charAt(0).toUpperCase() + r.title.slice(1); });
  var MAP_TIMEOUT_MS = 15000;

  function later(fn, ms) { var t = setTimeout(fn, ms); timers.push(t); return t; }
  function clearTimers() { timers.forEach(clearTimeout); timers = []; }

  // ---- Stap-definities ----
  var ST = {};
  var SECTIONS = ['s-welcome', 's-mag-intro', 's-mag-run', 's-mag-result', 's-still', 's-still-done', 's-still-intro', 's-btn-live', 's-btn-map', 's-btn-done', 's-screen', 's-done', 's-practice'];
  var closing = false;

  function def(name, d) { ST[name] = d; }

  def('welcome', {
    sec: 's-welcome', crumb: 'Kalibreren', move: false,
    allow: { next: 1, abort: 1 }, keys: { next: 'Beginnen', abort: 'Sluiten' },
    next: function () { go('mag_intro'); },
    abort: function () { closeUi(); }
  });

  def('mag_intro', {
    sec: 's-mag-intro', crumb: 'Stap 1 van 5: Magnetometer', move: true,
    allow: { next: 1, back: 1, skip: 1, abort: 1 }, keys: { next: 'Starten', back: 'Terug', skip: 'Overslaan', abort: 'Afbreken' },
    enter: function () { show('mag-intro-err', false); },
    next: function () {
      if (busy) return;
      busy = true;
      api('/api/calibrate/mag/start', { holder: 'sharp_shooter' }).then(function (j) {
        busy = false;
        if (state !== 'mag_intro') return;
        if (j.ok) go('mag_run');
        else { setText('mag-intro-err', errText(j)); show('mag-intro-err', true); }
      });
    },
    back: function () { go('welcome'); },
    skip: function () { go('still_intro'); },
    abort: function () { go('welcome'); }
  });

  var magWasActive = false, trail = [], lastDirKey = '';
  def('mag_run', {
    sec: 's-mag-run', crumb: 'Stap 1 van 5: Magnetometer draaien', move: true,
    allow: { next: 1, abort: 1 }, keys: { next: 'Afronden (als de balk vol is)', abort: 'Afbreken' },
    enter: function () { magWasActive = false; trail = []; lastDirKey = ''; gyroEma.reset(); },
    next: function () {
      var m = progress && progress.mag_calibration;
      if (!m || !m.active) return;
      if (!m.ready) { flashHint('Nog niet genoeg: wacht tot de balk vol is en draai verder.'); return; }
      if (busy) return;
      busy = true;
      api('/api/calibrate/mag/stop', {}).then(function (j) {
        busy = false;
        if (j.ok) { results.mag = C.fitVerdict(j.result); results.magRaw = j.result; go('mag_result'); }
        else flashHint(errText(j));
      });
    },
    abort: function () { api('/api/calibrate/mag/abort', {}); go('welcome'); }
  });

  def('mag_result', {
    sec: 's-mag-result', crumb: 'Stap 1 van 5: Resultaat', move: false,
    allow: { next: 1, back: 1, skip: 1, abort: 1 }, keys: { next: 'Verder', back: 'Opnieuw', skip: 'Verder', abort: 'Afbreken' },
    enter: function () {
      var v = results.mag, r = results.magRaw || {};
      var h = $('mag-verdict');
      h.textContent = v.headline;
      h.className = 'verdict ' + v.level;
      var ul = $('mag-tips');
      ul.textContent = '';
      v.tips.forEach(function (t, i) {
        ul.appendChild(el('li', '', t));
        var a = C.actionTip(t);
        if (a) ul.appendChild(el('li', 'sub', a));
      });
      var parts = [];
      if (typeof r.coverage === 'number') parts.push('dekking ' + Math.round(r.coverage * 100) + ' %');
      if (typeof r.rms_norm_err === 'number') parts.push('restfout ' + fmt1(r.rms_norm_err * 100) + ' % (goed: 2 %, marginaal: 4 %)');
      if (typeof r.dip_std_deg === 'number') parts.push('dip-spreiding ' + fmt1(r.dip_std_deg) + ' graden (goed: 1,5, marginaal: 8)');
      if (typeof r.duration_s === 'number') parts.push(Math.round(r.duration_s) + ' s');
      setText('mag-metrics', parts.join(' · '));
      setText('mag-result-next', v.level === 'fail' ? 'Trekker: toch verder' : 'Trekker: verder');
    },
    next: function () { go('still_intro'); },
    skip: function () { go('still_intro'); },
    back: function () { go('mag_intro'); },
    abort: function () { go('welcome'); }
  });

  // ---- Stil richten ----
  var plan = C.stillPlan(), si = 0, attempts = {}, stillDone = [], recOn = false;
  var stillTrack = new C.StillTracker({});
  var countStart = 0, STILL_MS = 5000, stillEma = new C.Ema(250);

  function stillReset() { si = 0; attempts = {}; stillDone = []; }
  function stopRec() {
    if (!recOn) return Promise.resolve(null);
    recOn = false;
    return api('/api/calibrate/rec/stop', {});
  }
  function mark(label) { return api('/api/calibrate/rec/mark', { label: label }); }
  function currentLabel(end) { var k = plan[si].key; return C.stillLabel(k, attempts[k] || 1, end); }

  def('still_intro', {
    sec: 's-still-intro', crumb: 'Stap 2 van 5: Stil richten', move: true,
    allow: { next: 1, back: 1, skip: 1, abort: 1 }, keys: { next: 'Starten', back: 'Terug', skip: 'Overslaan', abort: 'Afbreken' },
    enter: function () { show('still-intro-err', false); stillReset(); },
    next: function () {
      if (busy) return;
      busy = true;
      api('/api/calibrate/rec/start', { tag: 'stil-herhaalbaarheid', duration_s: 900 }).then(function (j) {
        busy = false;
        if (state !== 'still_intro') return;
        if (j.ok) { recOn = true; go('still_aim'); }
        else { setText('still-intro-err', 'Opname starten mislukt: ' + errText(j)); show('still-intro-err', true); }
      });
    },
    back: function () { go('mag_result'); },
    skip: function () { go('btn_live'); },
    abort: function () { go('welcome'); }
  });

  function renderDots(curFail) {
    var box = $('still-dots');
    box.textContent = '';
    plan.forEach(function (p, i) {
      var cls = i < stillDone.length ? 'ok' : (i === si ? 'cur' : '');
      var t = p.where === 'midden' ? 'M' + p.n : (p.where === 'links' ? 'L' : 'R');
      box.appendChild(el('i', cls, t));
    });
  }
  function crossClass(c) { $('cross').className = 'cross' + (c ? ' ' + c : ''); }

  def('still_aim', {
    sec: 's-still', crumb: 'Stap 2 van 5: Stil richten', move: true,
    allow: { next: 1, back: 1, abort: 1 }, keys: { next: 'Start de meting', back: 'Vorige opnieuw', abort: 'Afbreken' },
    enter: function () {
      var p = plan[si];
      crossClass('');
      show('still-meter', false);
      $('still-count').className = 'count small';
      var pos;
      if (p.where === 'midden') {
        setText('still-title', 'Richt op het kruis');
        pos = 'Meting ' + p.n + ' van 5 in het midden. ';
      } else if (p.where === 'links') {
        setText('still-title', 'Stap een halve meter naar links');
        pos = 'Meting links. ';
      } else {
        setText('still-title', 'Stap een halve meter naar rechts van het midden');
        pos = 'Meting rechts. ';
      }
      var k = plan[si].key;
      var tryNo = attempts[k] || 1;
      setText('still-sub', pos + 'Richt op het kruis, trek de trekker en houd vijf seconden stil.' + (tryNo > 1 ? ' (nieuwe poging)' : ''));
      setText('still-count', 'Trekker = start');
      setText('still-live', stillMsg || '');
      stillMsg = '';
      renderDots();
    },
    next: function () {
      mark(currentLabel(null));
      stillTrack.start(now());
      stillEma.reset();
      countStart = now();
      go('still_count');
    },
    back: function () {
      if (si > 0) { si--; stillDone.pop(); var k = plan[si].key; attempts[k] = (attempts[k] || 1) + 1; go('still_aim'); }
      else { stopRec(); go('still_intro'); }
    },
    abort: function () { stopRec(); go('welcome'); }
  });
  var stillMsg = '';

  def('still_count', {
    sec: 's-still', crumb: 'Stap 2 van 5: Houd stil', move: true,
    allow: { abort: 1 }, keys: { abort: 'Afbreken' },
    enter: function () {
      setText('still-title', 'Houd stil');
      setText('still-sub', 'Blijf op het kruis gericht en beweeg niet.');
      $('still-count').className = 'count';
      show('still-meter', true);
      renderDots();
    },
    abort: function () { mark(currentLabel('fail')); stopRec(); go('welcome'); }
  });

  function finishCount() {
    var r = stillTrack.result();
    var ok = r.ok;
    mark(currentLabel(ok ? 'ok' : 'fail'));
    var k = plan[si].key;
    show('still-meter', false);
    $('still-count').className = 'count small';
    if (ok) {
      crossClass('still');
      setText('still-count', 'Goed');
      stillDone.push({ key: k, attempts: attempts[k] || 1, max_dps: r.max_dps, fraction: r.fraction });
      si++;
      renderDots();
      go('still_flash', true);
      later(function () { if (si >= plan.length) go('still_done'); else go('still_aim'); }, 1300);
    } else {
      crossClass('fail');
      setText('still-count', 'Opnieuw');
      setText('still-live', 'Te onrustig: ' + r.why);
      stillMsg = 'Vorige poging mislukt: ' + r.why + '.';
      attempts[k] = (attempts[k] || 1) + 1;
      go('still_flash', true);
      later(function () { go('still_aim'); }, 2200);
    }
  }
  def('still_flash', {
    sec: 's-still', crumb: 'Stap 2 van 5: Stil richten', move: true, allow: { abort: 1 }, keys: { abort: 'Afbreken' },
    abort: function () { stopRec(); go('welcome'); }
  });

  def('still_done', {
    sec: 's-still-done', crumb: 'Stap 2 van 5: Klaar', move: false,
    allow: { next: 1, back: 1, abort: 1 }, keys: { next: 'Verder', back: 'Opnieuw', abort: 'Afbreken' },
    enter: function () {
      show('still-done-err', false);
      var extra = 0;
      stillDone.forEach(function (d) { extra += (d.attempts - 1); });
      setText('still-done-text', stillDone.length + ' metingen vastgelegd' + (extra ? ', ' + extra + ' keer opnieuw gedaan' : '') + '. De opname wordt nu afgesloten.');
      results.still = { n: stillDone.length, retries: extra, file: null };
      stopRec().then(function (j) {
        if (!j) return;
        if (j.ok) {
          var f = j.result && (j.result.file || (j.result.last && j.result.last.file));
          if (f) { results.still.file = f; setText('still-done-text', $('still-done-text').textContent + ' Bestand: ' + f); }
        } else { setText('still-done-err', 'Opname stoppen mislukt: ' + errText(j)); show('still-done-err', true); }
      });
    },
    next: function () { go('btn_live'); },
    back: function () { go('still_intro'); },
    abort: function () { go('welcome'); }
  });

  // ---- Knoppen ----
  var tiles = {};
  function buildTiles() {
    var box = $('btn-tiles');
    if (box.childNodes.length) return;
    BTN_NAMES.forEach(function (n) {
      var t = el('div', 'tile');
      t.appendChild(el('b', '', C.LABEL[n]));
      var s = el('small', '', '');
      t.appendChild(s);
      box.appendChild(t);
      tiles[n] = { box: t, small: s, seen: false };
    });
  }
  function aliasFor(moveName) {
    var out = [];
    Object.keys(settingsButtons).forEach(function (role) { if (settingsButtons[role] === moveName) out.push(ROLE_LABEL[role] || role); });
    return out.join(', ');
  }
  function loadSettingsButtons() {
    return api('/api/settings').then(function (j) {
      var b = j.ok && j.result && j.result.values && j.result.values.sharp_shooter && j.result.values.sharp_shooter.buttons;
      if (j.ok) settingsButtons = b || {};
    });
  }
  function armHold() {
    // Een knop die nu al ingedrukt is (van de vorige stap) telt pas na loslaten.
    hold.reset();
    [C.BIT.t, C.BIT.move, C.BIT.circle, C.BIT.select, C.BIT.start].forEach(function (b) {
      if ((lastMask >>> b) & 1) { hold.since[b] = now(); hold.fired[b] = true; }
    });
  }
  def('btn_live', {
    sec: 's-btn-live', crumb: 'Stap 3 van 5: Knoppen', move: true,
    allow: {}, keys: { next: 'Lang: vastleggen', back: 'Lang: terug', skip: 'Lang: overslaan', abort: 'Lang: stoppen' },
    enter: function () {
      buildTiles();
      BTN_NAMES.forEach(function (n) { tiles[n].seen = false; tiles[n].box.className = 'tile'; });
      if (!isDemo) loadSettingsButtons().then(function () { BTN_NAMES.forEach(function (n) { setText2(tiles[n].small, aliasFor(n)); }); });
      armHold();
    },
    hold: function (bit) {
      if (bit === C.BIT.t || bit === C.BIT.move) go('btn_map');
      else if (bit === C.BIT.circle) go('still_done');
      else if (bit === C.BIT.select) go('screen');
      else if (bit === C.BIT.start) go('welcome');
    }
  });
  function setText2(e, t) { if (e.textContent !== t) e.textContent = t; }

  var mi = 0, mapped = {}, mapStart = 0;
  def('btn_map', {
    sec: 's-btn-map', crumb: 'Stap 3 van 5: Knoppen vastleggen', move: true,
    allow: {}, keys: { abort: 'Lang: afbreken' },
    enter: function () { mi = 0; mapped = {}; armHold(); mapc.reset(); promptMap(); },
    hold: function (bit) { if (bit === C.BIT.start && ROLES[mi] && ROLES[mi].k !== 'start') go('welcome'); }
  });
  function promptMap() {
    var r = ROLES[mi];
    setText('map-count', 'Knop ' + (mi + 1) + ' van ' + ROLES.length);
    setText('map-title', 'Druk nu op ' + r.title);
    setText('map-sub', r.sub + ' (wachten = overslaan)');
    setText('map-msg', '');
    mapStart = now();
    mapc.reset();
    renderMapList($('map-list'));
  }
  function renderMapList(box) {
    box.textContent = '';
    ROLES.forEach(function (r, i) {
      var row = el('div');
      row.appendChild(el('span', '', ROLE_LABEL[r.k]));
      var v = mapped[r.k];
      var txt = v ? C.LABEL[v] : (state === 'btn_done' ? 'niet gevonden' : (i < mi ? 'overgeslagen' : (i === mi ? '...' : '')));
      var cell = el('span', v ? 'v' : 'none', txt);
      row.appendChild(cell);
      box.appendChild(row);
    });
  }
  function mapAdvance() {
    mi++;
    if (mi >= ROLES.length) { saveMapping(); return; }
    promptMap();
  }
  function saveMapping() {
    var body = {};
    ROLES.forEach(function (r) { body[r.k] = mapped[r.k] || ''; });
    var found = Object.keys(mapped).length;
    results.buttons = { found: found, total: ROLES.length, saved: false };
    go('btn_done');
    setText('btn-save-msg', 'Opslaan...');
    api('/api/calibrate/buttons', body).then(function (j) {
      if (j.ok) {
        results.buttons.saved = true;
        settingsButtons = j.result.buttons || {};
        setText('btn-save-msg', found + ' van ' + ROLES.length + ' knoppen gevonden en opgeslagen in de configuratie.');
      } else setText('btn-save-msg', 'Opslaan mislukt: ' + errText(j));
    });
  }
  def('btn_done', {
    sec: 's-btn-done', crumb: 'Stap 3 van 5: Knoppen', move: false,
    allow: { next: 1, back: 1, skip: 1, abort: 1 }, keys: { next: 'Verder', back: 'Opnieuw meten', skip: 'Verder', abort: 'Afbreken' },
    enter: function () { renderMapList($('done-list')); },
    next: function () { go('screen'); },
    skip: function () { go('screen'); },
    back: function () { go('btn_live'); },
    abort: function () { go('welcome'); }
  });

  // ---- Scherm (fase 5, plaatshouder) ----
  var targetsBuilt = false;
  def('screen', {
    sec: 's-screen', crumb: 'Stap 4 van 5: Scherm (fase 5)', move: false, fs: true,
    allow: { next: 1, back: 1, skip: 1, abort: 1 }, keys: { next: 'Verder', back: 'Terug', abort: 'Afbreken' },
    enter: function () {
      if (targetsBuilt) return;
      targetsBuilt = true;
      var box = $('targets');
      C.screenTargets(1920, 1080).forEach(function (t) {
        var d = el('div', 't');
        d.style.left = t.x + 'px';
        d.style.top = t.y + 'px';
        d.appendChild(el('i', 'h'));
        d.appendChild(el('i', 'v'));
        d.appendChild(el('i', 'ring'));
        d.appendChild(el('em', '', String(t.n)));
        box.appendChild(d);
      });
    },
    next: function () { go('done'); },
    skip: function () { go('done'); },
    back: function () { go('btn_done'); },
    abort: function () { go('welcome'); }
  });

  def('done', {
    sec: 's-done', crumb: 'Klaar', move: false,
    allow: { next: 1, back: 1, skip: 1, abort: 1 }, keys: { next: 'Oefenen', back: 'Terug', skip: 'Sluiten', abort: 'Naar begin' },
    enter: function () {
      var ul = $('summary');
      ul.textContent = '';
      var m = results.mag, s = results.still, b = results.buttons;
      ul.appendChild(el('li', '', 'Magnetometer: ' + (m ? m.headline : 'overgeslagen')));
      ul.appendChild(el('li', '', 'Stil richten: ' + (s ? s.n + ' metingen' + (s.retries ? ' (' + s.retries + ' herhaald)' : '') : 'overgeslagen')));
      ul.appendChild(el('li', '', 'Knoppen: ' + (b ? b.found + ' van ' + b.total + ' vastgelegd' + (b.saved ? '' : ' (niet opgeslagen)') : 'overgeslagen')));
      ul.appendChild(el('li', '', 'Scherm: volgt in fase 5'));
    },
    next: function () { go('practice'); },
    skip: function () { closeUi(); },
    back: function () { go('screen'); },
    abort: function () { go('welcome'); }
  });

  // ---- Oefenen: vizier, doelen, treffers ----
  // Alleen weergave en meting op deze pagina: er gaat niets naar de game en er wordt niets (de)armed.
  // Trekker = schieten, PS = midden zetten (door de daemon zelf; hier alleen een melding), Cirkel = terug, Start = sluiten, Select = ruwe stip aan/uit.
  var PW = 1920, PH = 1080, STREAM_HZ_PRACTICE = 60;
  var pbtn = new C.PracticeButtons();
  var pStats = new C.PracticeStats();
  var pFilt = new C.Interpolator(), pRaw = new C.Interpolator();
  var pRate = new C.RateMeter(1000);
  var pAim = null, pTarget = null, pShots = [], pFlash = 0, pHitFx = null, pMsgUntil = 0, pRawOn = true, pRaf = 0;
  var pCtx = null, pLast = { score: '', status: '', msg: '' };

  function pNewTarget() { pTarget = C.nextTarget(Math.random, pTarget, PW, PH, { r: 60 }); }
  function pMsg(text, ms) { setText('pr-msg', text); show('pr-msg', true); pMsgUntil = now() + (ms || 1200); }

  def('practice', {
    sec: 's-practice', crumb: 'Stap 5 van 5: Oefenen', move: true, fs: true,
    allow: {}, keys: {},
    enter: function () {
      pbtn.reset(); pStats.reset(); pShots = []; pFlash = 0; pHitFx = null; pAim = null;
      pFilt.push(0, null); pRaw.push(0, null); pRate.reset();
      show('pr-msg', false); show('pr-none', false);
      if (!pTarget || !isDemo) pNewTarget();
      pCtx = $('pr-canvas').getContext('2d');
      if (!pRaf) pRaf = requestAnimationFrame(pFrame);
    },
    back: function () { nav.reset(); go('done'); },
    close: function () { closeUi(); },
    shoot: function () {
      if (!pAim) return;
      var sx = pAim.x * PW, sy = pAim.y * PH;
      var res = C.shotResult(sx, sy, pTarget, PW);
      pStats.add(res);
      pShots.push({ x: pAim.cx * PW, y: pAim.cy * PH, hit: res.hit, t: now() });
      if (pShots.length > 14) pShots.shift();
      pFlash = now();
      if (res.hit) { pHitFx = { x: pTarget.x, y: pTarget.y, t: now() }; pNewTarget(); }
    },
    // De daemon centreert bij elke PS-druk zelf; hier géén /api/recenter erbij (dubbel centreren).
    centered: function () { pMsg('Midden gezet', 1200); },
    toggle_raw: function () { pRawOn = !pRawOn; pMsg(pRawOn ? 'Ruwe stip aan' : 'Ruwe stip uit', 900); }
  });

  // Elke SSE-frame in de oefenstap: aim voor de weergave, knoppen voor de acties.
  function practiceFrame(t, press) {
    pRate.add(t);
    pAim = C.aimView(sample);
    pFilt.push(t, pAim ? [pAim.x, pAim.y] : null);
    pRaw.push(t, pAim ? [pAim.rx, pAim.ry] : null);
    if (press) {
      var acts = pbtn.feed(press, t), d = ST.practice;
      for (var i = 0; i < acts.length; i++) {
        if (state !== 'practice') break;
        d[acts[i]]();
      }
    }
  }

  function pArrow(ctx, a) {
    ctx.save();
    ctx.translate(a.px, a.py); ctx.rotate(a.angle);
    ctx.fillStyle = '#ffb454'; ctx.strokeStyle = '#000'; ctx.lineWidth = 4;
    ctx.beginPath(); ctx.moveTo(40, 0); ctx.lineTo(-24, -34); ctx.lineTo(-8, 0); ctx.lineTo(-24, 34); ctx.closePath();
    ctx.fill(); ctx.stroke();
    ctx.restore();
  }
  function pReticle(ctx, x, y, color) {
    ctx.strokeStyle = '#000'; ctx.lineWidth = 9;
    ctx.beginPath(); ctx.arc(x, y, 30, 0, 6.2832); ctx.moveTo(x - 52, y); ctx.lineTo(x - 14, y); ctx.moveTo(x + 14, y); ctx.lineTo(x + 52, y);
    ctx.moveTo(x, y - 52); ctx.lineTo(x, y - 14); ctx.moveTo(x, y + 14); ctx.lineTo(x, y + 52); ctx.stroke();
    ctx.strokeStyle = color; ctx.lineWidth = 4; ctx.stroke();
  }

  function pDraw(t) {
    var ctx = pCtx;
    ctx.clearRect(0, 0, PW, PH);
    ctx.fillStyle = '#05080d'; ctx.fillRect(0, 0, PW, PH);
    ctx.strokeStyle = '#1b2434'; ctx.lineWidth = 2; ctx.strokeRect(1, 1, PW - 2, PH - 2);
    // Doel
    if (pTarget) {
      var r = pTarget.r;
      ctx.lineWidth = 6;
      ctx.fillStyle = '#3a1a1a'; ctx.strokeStyle = '#ff6b6b';
      ctx.beginPath(); ctx.arc(pTarget.x, pTarget.y, r, 0, 6.2832); ctx.fill(); ctx.stroke();
      ctx.fillStyle = '#ffd0d0'; ctx.strokeStyle = '#ff6b6b';
      ctx.beginPath(); ctx.arc(pTarget.x, pTarget.y, r * 0.55, 0, 6.2832); ctx.stroke();
      ctx.beginPath(); ctx.arc(pTarget.x, pTarget.y, 7, 0, 6.2832); ctx.fill();
    }
    if (pHitFx && t - pHitFx.t < 450) {
      var k = (t - pHitFx.t) / 450;
      ctx.strokeStyle = 'rgba(61,220,151,' + (1 - k) + ')'; ctx.lineWidth = 8;
      ctx.beginPath(); ctx.arc(pHitFx.x, pHitFx.y, 60 + k * 90, 0, 6.2832); ctx.stroke();
    }
    // Inslagmarkeringen (vervagen na enkele seconden)
    for (var i = 0; i < pShots.length; i++) {
      var s = pShots[i], age = t - s.t;
      if (age > 6000) continue;
      var al = Math.min(1, Math.max(0.25, 1 - age / 6000));
      ctx.strokeStyle = (s.hit ? 'rgba(61,220,151,' : 'rgba(255,107,107,') + al + ')'; ctx.lineWidth = 5;
      ctx.beginPath(); ctx.arc(s.x, s.y, 14, 0, 6.2832); ctx.moveTo(s.x - 22, s.y); ctx.lineTo(s.x + 22, s.y); ctx.moveTo(s.x, s.y - 22); ctx.lineTo(s.x, s.y + 22); ctx.stroke();
    }
    // Vizier
    var f = pFilt.at(t), rw = pRaw.at(t);
    var none = !f;
    if (f) {
      if (pRawOn && rw) {
        var rxp = rw[0] * PW, ryp = rw[1] * PH;
        if (rxp > -40 && rxp < PW + 40 && ryp > -40 && ryp < PH + 40) {
          ctx.fillStyle = '#9aa3b5'; ctx.strokeStyle = '#000'; ctx.lineWidth = 3;
          ctx.beginPath(); ctx.arc(rxp, ryp, 8, 0, 6.2832); ctx.fill(); ctx.stroke();
        }
      }
      var arrow = C.offscreenArrow(f[0], f[1], PW, PH, 60);
      if (arrow) pArrow(ctx, arrow);
      else pReticle(ctx, f[0] * PW, f[1] * PH, '#5ab8ff');
    }
    // Schotflits
    if (pFlash && t - pFlash < 160 && f) {
      var q = (t - pFlash) / 160, fx = Math.min(PW, Math.max(0, f[0] * PW)), fy = Math.min(PH, Math.max(0, f[1] * PH));
      ctx.strokeStyle = 'rgba(255,255,255,' + (1 - q) + ')'; ctx.lineWidth = 10;
      ctx.beginPath(); ctx.arc(fx, fy, 30 + q * 70, 0, 6.2832); ctx.stroke();
      ctx.fillStyle = 'rgba(255,255,255,' + (0.18 * (1 - q)) + ')'; ctx.fillRect(0, 0, PW, PH);
    }
    if ($('pr-none').hidden !== !none) show('pr-none', none);
  }

  function pFrame() {
    pRaf = 0;
    if (state !== 'practice') return;
    var t = now();
    pDraw(t);
    var st = pStats.summary();
    var sc = st.shots ? 'Treffers ' + st.hits + '   Missers ' + st.misses + '   Gemiddelde afstand ' + Math.round(st.avgPx) + ' px (' + fmt1(st.avgPct) + ' % van de breedte)'
                      : 'Raak de doelen met de trekker';
    if (sc !== pLast.score) { setText('pr-score', sc); pLast.score = sc; }
    var hz = isDemo ? 60 : pRate.hz(t);
    var stt = 'Aim: ' + C.aimModeText(sample) + '   |   Filter: ' + (sample && sample.aim && sample.aim.filter_enabled ? 'aan' : 'uit') +
              '   |   ' + (streamHz > 30 ? 'stream ' + streamHz + ' Hz' : 'stream 30 Hz') + ', ontvangen ' + Math.round(hz) + ' frames/s';
    if (stt !== pLast.status) { setText('pr-status', stt); pLast.status = stt; }
    if (!$('pr-msg').hidden && t > pMsgUntil) show('pr-msg', false);
    pRaf = requestAnimationFrame(pFrame);
  }

  // ---- Navigatie ----
  function closeUi() {
    closing = true;
    $('overlay-text').textContent = 'Kalibratiescherm wordt gesloten...';
    show('overlay', true);
    api('/api/calibrate/ui', { action: 'close' });
  }

  function setKey(id, text) {
    var k = $(id);
    k.classList.toggle('off', !text);
    if (text) k.lastChild.textContent = ' ' + text;
  }

  function go(name, keepTimers) {
    if (!keepTimers) clearTimers();
    state = name;
    var d = ST[name];
    SECTIONS.forEach(function (s) { $(s).hidden = s !== d.sec; });
    document.body.classList.toggle('fs', !!d.fs);
    setText('crumb', d.crumb);
    var k = d.keys || {};
    setKey('k-next', k.next);
    setKey('k-back', k.back);
    setKey('k-skip', k.skip);
    setKey('k-abort', k.abort);
    wantHz = name === 'practice' ? STREAM_HZ_PRACTICE : 30;
    if (es && wantHz !== streamHz && !isDemo) startStream();
    if (d.enter) d.enter();
    updateOverlay();
  }

  function dispatch(act) {
    var d = ST[state];
    if (!d || !d.allow || !d.allow[act] || !d[act]) return;
    d[act]();
  }

  function updateOverlay() {
    var d = ST[state];
    var msg = '';
    if (!streamUp) msg = 'Geen verbinding met de daemon.\nWe proberen het opnieuw...';
    else if (d && d.move && link !== 'fresh') msg = 'Geen Move verbonden.\nDruk op de PS-knop van de Move.';
    var o = $('overlay');
    if (closing) return;
    if (msg) { setText('overlay-text', msg); o.hidden = false; } else o.hidden = true;
  }

  var hintUntil = 0;
  function flashHint(t) { setText('mag-hint', t); hintUntil = now() + 3500; }

  // ---- Stroom ----
  var es = null, wantHz = 30;
  // De oefenstap vraagt 60 Hz voor alleen deze verbinding (/api/stream?hz=60); de rest blijft op 30 Hz.
  var streamHz = 30;
  function startStream() {
    if (es) es.close();
    streamHz = wantHz;
    es = new EventSource(streamHz > 30 ? '/api/stream?hz=' + streamHz : '/api/stream');
    es.onopen = function () { streamUp = true; nav.reset(); pbtn.reset(); pRate.reset(); lastPress = null; updateOverlay(); };
    es.onerror = function () { streamUp = false; updateOverlay(); };
    es.addEventListener('sample', onSample);
  }

  function onSample(ev) {
    var d;
    try { d = JSON.parse(ev.data); } catch (e) { return; }
    var t = now();
    sample = d.sample;
    if (d.progress) progress = d.progress;
    var nl = sample && sample.link ? sample.link : 'disconnected';
    if (nl !== link) { link = nl; updateOverlay(); }
    var pill = $('pill-move');
    var txt = link === 'fresh' ? 'Move: verbonden' : 'Move: niet verbonden';
    if (pill.textContent !== txt) { pill.textContent = txt; pill.className = 'pill ' + (link === 'fresh' ? 'ok' : 'bad'); }
    var pl = $('pill-link');
    if (pl.textContent !== 'Daemon: ok') { pl.textContent = 'Daemon: ok'; pl.className = 'pill ok'; }

    var g = C.gyroDps(sample);
    dps = g === null ? null : gyroEma.add(g, t);
    lastMask = sample && sample.buttons ? sample.buttons.mask >>> 0 : 0;

    var press = d.edges && d.edges.press;
    var bits = [];
    if (press) {
      bits = lastPress ? C.pressedBits(lastPress, press) : [];
      lastPress = press.slice();
      if (state !== 'practice') {
        var act = nav.feed(press, t, (ST[state] || {}).allow);
        if (act) dispatch(act);
      }
    }
    if (state === 'practice') practiceFrame(t, press);
    else onFrame(t, bits, g);
  }

  // Per frame: live weergave van de huidige stap.
  function onFrame(t, bits, rawDps) {
    var d = ST[state] || {};
    if (state === 'still_count') {
      var sm = rawDps === null ? null : stillEma.add(rawDps, t);
      stillTrack.add(rawDps, t);
      var still = sm !== null && stillTrack.isStill(sm);
      crossClass(sm === null ? '' : (still ? 'still' : 'rest'));
      var f = $('still-fill');
      var level = sm === null ? 0 : Math.max(0, Math.min(1, 1 - sm / (stillTrack.thr * 2)));
      f.style.width = Math.round(level * 100) + '%';
      f.style.background = still ? 'var(--ok)' : 'var(--warn)';
      setText('still-live', sm === null ? '' : (still ? 'Stil genoeg' : 'Te onrustig, houd stiller'));
    } else if (state === 'btn_live') {
      BTN_NAMES.forEach(function (n) {
        var on = (lastMask >>> C.BIT[n]) & 1;
        var tl = tiles[n];
        if (on) tl.seen = true;
        var cls = 'tile' + (on ? ' on' : (tl.seen ? ' seen' : ''));
        if (tl.box.className !== cls) tl.box.className = cls;
      });
      var tv = sample && typeof sample.trigger === 'number' ? sample.trigger : 0;
      $('trig-fill').style.width = Math.round(tv / 255 * 100) + '%';
      setText('trig-val', String(tv));
    }
    if (d.hold) {
      var watch = state === 'btn_map' ? [C.BIT.start] : [C.BIT.t, C.BIT.move, C.BIT.circle, C.BIT.select, C.BIT.start];
      var h = hold.update(lastMask, t, watch);
      if (state === 'btn_live') {
        var pn = Math.max(h.progress[C.BIT.t] || 0, h.progress[C.BIT.move] || 0);
        showHold('hold-next', pn); showHold('hold-back', h.progress[C.BIT.circle] || 0); showHold('hold-skip', h.progress[C.BIT.select] || 0);
      } else if (state === 'btn_map') {
        showHold('map-abort', h.progress[C.BIT.start] || 0);
      }
      if (h.fired !== null) d.hold(h.fired);
    }
    if (state === 'btn_map') {
      mapc.add(bits, t);
      var res = mapc.poll(t);
      if (res) {
        var role = ROLES[mi].k;
        if (res.status === 'single') {
          mapped[role] = res.name;
          setText('map-msg', C.LABEL[res.name] + ' herkend');
          renderMapList($('map-list'));
          later(mapAdvance, 500);
          mapStart = now() + 10 * MAP_TIMEOUT_MS;  // timer pauzeren tot de volgende
        } else {
          setText('map-msg', 'Meerdere knoppen tegelijk (' + res.names.map(function (n) { return C.LABEL[n]; }).join(', ') + '). Probeer opnieuw.');
          mapStart = now();
        }
      }
    }
  }
  function showHold(id, p) {
    var e = $(id);
    var on = p > 0;
    e.classList.toggle('on', on);
    var base = e.getAttribute('data-t') || e.textContent;
    e.setAttribute('data-t', base);
    var s = on ? base + ' (' + Math.round(p * 100) + ' %)' : base;
    if (e.textContent !== s) e.textContent = s;
  }

  // ---- Tikklok (timers, magnetometerweergave) ----
  var sphF = $('sph-front'), sphB = $('sph-back');
  var SZ = 260;
  sphF.width = sphF.height = sphB.width = sphB.height = SZ;
  var imgF = sphF.getContext('2d').createImageData(SZ, SZ), imgB = sphB.getContext('2d').createImageData(SZ, SZ);
  var TILT = 0.45;

  function drawSphere(cv, img, yaw, cells, minCount, dir) {
    var ctx = cv.getContext('2d');
    C.renderSphere(img.data, SZ, cells, minCount, yaw, TILT);
    ctx.putImageData(img, 0, 0);
    var half = SZ / 2, rad = half - 2;
    // spoor van recente richtingen en de huidige richting
    trail.forEach(function (p, i) {
      var q = C.projectDir(p, yaw, TILT);
      if (q.z <= 0) return;
      ctx.fillStyle = 'rgba(255,255,255,' + (0.15 + 0.5 * i / trail.length) + ')';
      ctx.beginPath(); ctx.arc(half + q.x * rad, half - q.y * rad, 3, 0, 6.3); ctx.fill();
    });
    if (dir) {
      var c = C.projectDir(dir, yaw, TILT);
      if (c.z > 0) {
        ctx.strokeStyle = '#fff'; ctx.lineWidth = 4;
        ctx.beginPath(); ctx.arc(half + c.x * rad, half - c.y * rad, 11, 0, 6.3); ctx.stroke();
        ctx.fillStyle = '#5ab8ff';
        ctx.beginPath(); ctx.arc(half + c.x * rad, half - c.y * rad, 6, 0, 6.3); ctx.fill();
      }
    }
  }

  var lastDraw = 0;
  function renderMag(t) {
    var m = progress && progress.mag_calibration;
    if (!m) return;
    if (m.active) magWasActive = true;
    if (!m.active && magWasActive && !busy) {
      // De sessie is buiten ons om geëindigd (verbinding verbroken, afgebroken).
      var lr = m.last_result;
      magWasActive = false;
      if (lr && lr.aborted) { go('mag_intro'); setText('mag-intro-err', 'Kalibratie afgebroken: ' + lr.aborted + '. Start opnieuw.'); show('mag-intro-err', true); return; }
    }
    var pr = C.magProgress(m);
    setText('mag-pct', String(pr.pct));
    $('mag-bar').style.width = pr.pct + '%';
    setText('mag-cov', String(Math.round(m.coverage_pct || 0)));
    setText('mag-time', String(Math.floor(m.elapsed_s || 0)));
    setText('mag-smp', String(m.samples || 0));
    var tp = C.classifyTempo(dps);
    var tt = $('tempo-text');
    setText('tempo-text', tp.text + (dps === null ? '' : ' (' + Math.round(dps) + ' graden/s)'));
    tt.className = tp.ok ? 'ok' : (tp.level === 'snel' || tp.level === 'stil' ? 'warn' : 'bad');
    $('tempo-mark').style.left = 'calc(' + Math.round(C.tempoPosition(dps) * 100) + '% - 6px)';
    if (m.dir) {
      var key = m.dir.map(function (x) { return x.toFixed(2); }).join(',');
      if (key !== lastDirKey) { lastDirKey = key; trail.push(m.dir); if (trail.length > 60) trail.shift(); }
    }
    if (t > hintUntil) {
      var h;
      if (tp.level === 'te_snel') h = 'Veel te snel: draai rustig, zonder te schudden.';
      else if (tp.level === 'snel') h = 'Een tikje rustiger draaien.';
      else if (m.ready) h = 'Genoeg! Druk op de trekker om af te ronden, of draai nog even door.';
      else if ((m.elapsed_s || 0) >= (m.min_seconds || 40) && pr.cov < 1) h = 'Draai ook ondersteboven, loop naar het plafond en de vloer, en rol om de loop.';
      else if (tp.level === 'stil' && (m.elapsed_s || 0) > 3) h = 'Blijf draaien: houd het geweer in beweging.';
      else if (pr.time < 1 && pr.cov >= 1) h = 'Dekking is goed; draai nog door tot de tijd (40 s) om is.';
      else h = 'Goed bezig. Draai langzaam alle kanten op.';
      setText('mag-hint', h);
    }
    if (t - lastDraw > 50) {
      lastDraw = t;
      var yaw = t / 1000 * 0.35;
      var cells = m.cells || [];
      var minc = m.cell_min_count || 3;
      drawSphere(sphF, imgF, yaw, cells, minc, m.dir);
      drawSphere(sphB, imgB, yaw + Math.PI, cells, minc, m.dir);
    }
  }

  function tick() {
    var t = now();
    if (isDemo && state !== 'mag_run') return;
    if (state === 'still_count') {
      var left = Math.max(0, STILL_MS - (t - countStart));
      setText('still-count', String(Math.ceil(left / 1000)));
      if (left <= 0) finishCount();
    } else if (state === 'btn_map') {
      var el1 = t - mapStart;
      $('map-timer').style.width = Math.max(0, Math.min(100, 100 - el1 / MAP_TIMEOUT_MS * 100)) + '%';
      if (el1 >= MAP_TIMEOUT_MS) { setText('map-msg', 'Geen druk gezien: overgeslagen.'); mapStart = now() + 10 * MAP_TIMEOUT_MS; later(mapAdvance, 600); }
    } else if (state === 'mag_run') {
      renderMag(t);
    }
  }
  setInterval(tick, 50);

  // ---- Voorbeeldmodus (?demo=<stap>): vaste voorbeeldgegevens, zonder daemon. Voor schermcontrole en screenshots. ----
  var isDemo = false;
  function demo(name) {
    isDemo = true;
    streamUp = true; link = 'fresh'; dps = 42;
    $('pill-move').textContent = 'Move: verbonden'; $('pill-move').className = 'pill ok';
    $('pill-link').textContent = 'Daemon: demo'; $('pill-link').className = 'pill warn';
    var cells = [5, 0, 9, 4, 0, 7, 1, 3, 0, 12, 6, 0, 2, 8, 0, 4, 3, 0, 5, 1];
    progress = { mag_calibration: { active: true, coverage_pct: 55, elapsed_s: 27, samples: 210, target_coverage_pct: 80, target_samples: 300,
      min_seconds: 40, ready: false, cell_min_count: 3, cells: cells, dir: [0.35, 0.55, 0.76] } };
    trail = [[0.1, 0.9, 0.4], [0.2, 0.8, 0.5], [0.3, 0.6, 0.7], [0.35, 0.55, 0.76]].map(function (v) { var n = Math.hypot(v[0], v[1], v[2]); return [v[0] / n, v[1] / n, v[2] / n]; });
    var raw = { ok: false, marginal: true, saved: true, coverage: 0.85, rms_norm_err: 0.031, dip_std_deg: 5.2, duration_s: 63,
      problems: ['veld niet homogeen: de restfout is 3,1 % (goed is 2 %). Ga verder van metaal en speakers af.'] };
    results.magRaw = raw; results.mag = C.fitVerdict(raw);
    results.still = { n: 7, retries: 1 }; results.buttons = { found: 8, total: 10, saved: true };
    settingsButtons = { trekker: 't', pomp: 'move', driehoek: 'triangle', cirkel: 'circle' };
    if (name === 'still_aim' || name === 'still_count' || name === 'still_flash') { si = 2; stillDone = [{}, {}]; }
    if (name === 'btn_map') { go('btn_map'); mi = 3; mapped = { trekker: 't', pomp: 'move', driehoek: 'triangle' }; promptMap(); return; }
    go(name);
    if (name === 'still_count') { setText('still-count', '3'); show('still-meter', true); $('still-fill').style.width = '80%'; crossClass('still'); setText('still-live', 'Stil genoeg'); }
    if (name === 'btn_live') {
      lastMask = (1 << C.BIT.t) | (1 << C.BIT.triangle);
      BTN_NAMES.forEach(function (n) { var on = (lastMask >>> C.BIT[n]) & 1; tiles[n].box.className = 'tile' + (on ? ' on' : (n === 'circle' ? ' seen' : '')); setText2(tiles[n].small, aliasFor(n)); });
      $('trig-fill').style.width = '72%'; setText('trig-val', '184');
    }
    if (name === 'btn_done') renderMapList($('done-list'));
    if (name === 'practice') {
      // Vaste voorbeeldgegevens: doel, vizier iets naast het doel, ruwe stip, een paar eerdere schoten.
      sample = { tracking_mode: 'orientation', aim: { valid: true, center_source: 'auto_initial', filter_enabled: true } };
      pTarget = { x: 1290, y: 400, r: 60 };
      pAim = { x: 0.62, y: 0.40, rx: 0.655, ry: 0.372, cx: 0.62, cy: 0.40, off: false };
      pFilt.push(now(), [0.62, 0.40]); pRaw.push(now(), [0.655, 0.372]);
      [[1000, 700, 0], [1500, 300, 1], [420, 760, 0], [980, 560, 1]].forEach(function (s) {
        var tg = { x: s[0], y: s[1], r: 60 };
        var rs = C.shotResult(s[0] + (s[2] ? 18 : 130), s[1] - (s[2] ? 12 : 70), tg, PW);
        pStats.add(rs);
        pShots.push({ x: s[0] + (s[2] ? 18 : 130), y: s[1] - (s[2] ? 12 : 70), hit: rs.hit, t: now() + 1e7 });
      });
      pRate.reset();
    }
  }

  // ---- Start ----
  function init() {
    var qs = new URLSearchParams(window.location.search);
    var dm = qs.get('demo');
    if (dm && ST[dm]) { demo(dm); return; }
    // ?step=practice: direct naar de oefenstap (knop in de web-UI), met de hogere streamsnelheid vanaf het begin.
    if (qs.get('step') === 'practice') { wantHz = STREAM_HZ_PRACTICE; startStream(); go('practice'); return; }
    startStream();
    // Liep er al een kalibratiesessie (pagina herladen)? Dan daar verder.
    api('/api/magnetometer').then(function (j) {
      var act = j.ok && j.result && j.result.session && j.result.session.mag_calibration && j.result.session.mag_calibration.active;
      if (act && state === '') go('mag_run');
      else if (state === '') go(window.location.hash === '#screen' ? 'screen' : 'welcome');
    });
    // Fallback als de aanvraag hangt.
    setTimeout(function () { if (state === '') go('welcome'); }, 2500);
    // Voor de proef met screenshots: #stap in de URL springt naar die stap.
    window.addEventListener('hashchange', function () { var n = window.location.hash.slice(1); if (ST[n]) go(n); });
  }
  window.__rgcal = { go: function (n) { go(n); }, state: function () { return state; } };
  init();
})();

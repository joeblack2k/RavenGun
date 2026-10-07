'use strict';
// RavenGun-webinterface: geen externe bronnen, geen inline code (CSP). Alle tekst via textContent.
(function () {
  var csrf = (document.querySelector('meta[name="csrf-token"]') || {}).content || '';
  var $ = function (id) { return document.getElementById(id); };
  var pages = ['overzicht', 'live', 'apparaten', 'camera', 'led', 'uitvoer', 'instellingen', 'info', 'kalibratie', 'games'];
  var current = '';
  var pollTimer = null;
  var es = null;

  // ---- Hulpfuncties ----
  function api(path, body) {
    var opt = { cache: 'no-store' };
    if (body !== undefined) {
      opt.method = 'POST';
      opt.headers = { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf };
      opt.body = JSON.stringify(body);
    }
    return fetch(path, opt).then(function (r) {
      return r.json().then(function (j) { j.httpStatus = r.status; setConn(true, 'verbonden met daemon'); return j; });
    }, function (e) { setConn(false, 'daemon niet bereikbaar'); throw e; });
  }
  function errText(j) { return j && j.error ? j.error.message : 'onbekende fout'; }
  function setConn(ok, text) {
    var c = $('conn');
    c.textContent = text;
    c.className = 'pill ' + (ok ? 'ok' : 'bad');
  }
  function fmt(v, d) { return typeof v === 'number' && isFinite(v) ? v.toFixed(d === undefined ? 1 : d) : '-'; }
  function yn(b) { return b ? 'ja' : 'nee'; }
  function fill(dl, rows) {
    dl.textContent = '';
    rows.forEach(function (r) {
      var dt = document.createElement('dt'), dd = document.createElement('dd');
      dt.textContent = r[0];
      dd.textContent = r[1];
      if (r[2]) dd.className = r[2];
      dl.appendChild(dt);
      dl.appendChild(dd);
    });
  }
  function toast(msg) {
    var t = $('toast');
    t.textContent = msg;
    t.className = 'toast show';
    setTimeout(function () { t.className = 'toast'; }, 2500);
  }
  function dur(s) {
    if (typeof s !== 'number' || !isFinite(s)) return '-';
    s = Math.floor(s);
    var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
    return (d ? d + ' d ' : '') + (h || d ? h + ' u ' : '') + m + ' min ' + (s % 60) + ' s';
  }
  function setHidden(el, hidden, text) {
    el.hidden = hidden;
    if (!hidden) el.textContent = text;
  }
  function num(id, lo, hi, name, errs, integer) {
    var el = $(id), raw = el.value.trim().replace(',', '.');
    el.classList.remove('invalid');
    var v = raw === '' ? NaN : Number(raw);
    if (!isFinite(v) || v < lo || v > hi || (integer && Math.floor(v) !== v)) {
      errs.push(name + ' moet ' + (integer ? 'een geheel getal' : 'een getal') + ' tussen ' + lo + ' en ' + hi + ' zijn');
      el.classList.add('invalid');
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
      errEl.textContent = 'daemon niet bereikbaar';
    });
  }
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function rgbCss(r, g, b) { return 'rgb(' + r + ',' + g + ',' + b + ')'; }
  function hex2(n) { var s = n.toString(16); return s.length < 2 ? '0' + s : s; }

  // ---- Batterij: grove niveaus van PSMoveAPI (PSMove_Battery_Level), geen percentage ----
  var BATT = { min: 1, ge20: 2, ge40: 3, ge60: 4, ge80: 5, max: 5 };
  function showBattery(c) {
    var box = $('ov-batt-seg'), text = $('ov-batt-text'), note = $('ov-batt-note');
    box.textContent = '';
    var code = c && c.connected ? c.battery.code : 'unknown';
    var segs = BATT[code] || 0;
    var charging = code === 'charging' || code === 'charged';
    if (code === 'charged') segs = 5;
    for (var i = 0; i < 5; i++) box.appendChild(el('span', i < segs ? 'on' : ''));
    box.className = 'batt' + (code === 'charging' ? ' charging' : '') + (segs <= 1 && !charging && segs > 0 ? ' low' : (segs === 2 && !charging ? ' mid' : ''));
    if (!c || !c.connected) { text.textContent = 'Geen Move verbonden'; note.textContent = ''; return; }
    var label = c.battery.text;
    if (BATT[code]) label = 'niveau ' + Math.min(segs, 5) + ' van 5: ' + c.battery.text;
    text.textContent = label.charAt(0).toUpperCase() + label.slice(1);
    note.textContent = 'Grove meting: de Move meldt zes niveaus plus laden en vol, geen percentage. Elk segment is een band van 20%.';
  }

  // ---- Overzicht ----
  function refreshOverview() {
    Promise.all([api('/api/status'), api('/api/sample')]).then(function (res) {
      var st = res[0], sm = res[1];
      if (!st.ok || !sm.ok) { setConn(false, 'daemon geeft een fout'); return; }
      setConn(true, 'verbonden met daemon');
      var s = st.result, p = sm.result, c = s.controller, cam = s.camera || {};
      showBattery(c);
      fill($('ov-move'), [
        ['Verbonden', c.connected ? 'ja (' + c.model + ', ' + c.connection + ')' : 'nee, geen Move gevonden', c.connected ? 'ok' : 'warn'],
        ['IMU-rapporten', fmt(s.reports.rate_hz_5s) + ' per seconde'],
        ['Controller-ID', c.id || '-']
      ]);
      var fr = cam.frames || {};
      var pos = p.position;
      fill($('ov-cam'), [
        ['Camera', cam.enabled ? cam.state : 'uit', cam.state === 'streaming' ? 'ok' : (cam.enabled ? 'warn' : 'muted')],
        ['Camera-fps', cam.enabled ? fmt(fr.fps_5s) : '-'],
        ['Bol gevonden', yn(pos.valid), pos.valid ? 'ok' : 'warn'],
        ['Reden', pos.valid ? '-' : pos.reason],
        ['Volgstatus', pos.track_state],
        ['Fout', cam.error || '-', cam.error ? 'bad' : '']
      ]);
      showLedRows($('ov-led'), s);
      fill($('ov-track'), [
        ['Modus', p.tracking_mode],
        ['Oriëntatie geldig', yn(p.orientation.valid), p.orientation.valid ? 'ok' : 'warn'],
        ['Aim geldig', yn(p.aim.valid), p.aim.valid ? 'ok' : 'warn'],
        ['Midden bepaald door', p.aim.center_source],
        ['Filter', p.aim.filter_enabled ? 'aan' : 'uit']
      ]);
      fill($('ov-daemon'), [
        ['PID', String(s.daemon.pid)],
        ['Looptijd', dur(s.daemon.uptime_s)],
        ['HID', s.daemon.hid_enabled ? 'aan' : 'uit (--no-hid)']
      ]);
    }).catch(function () { setConn(false, 'daemon niet bereikbaar'); });
  }
  function showLedRows(dl, s) {
    var led = s.controller.led, tc = ((s.camera || {}).tracker || {}).config || {};
    dl.textContent = '';
    var dt = el('dt', '', 'LED-kleur'), dd = el('dd');
    var chip = el('span', 'led-chip');
    if (led.enabled) chip.style.background = rgbCss(led.rgb[0], led.rgb[1], led.rgb[2]);
    dd.appendChild(chip);
    dd.appendChild(document.createTextNode(led.enabled ? led.rgb.join(', ') : 'niet aangestuurd'));
    dl.appendChild(dt); dl.appendChild(dd);
    var rows = [
      ['Tracker-tint', typeof tc.hue_deg === 'number' ? fmt(tc.hue_deg, 0) + ' graden, tolerantie ' + fmt(tc.hue_tol_deg, 0) : '-'],
      ['LED-updates', String(led.updates) + (led.failures ? ' (' + led.failures + ' mislukt)' : '')]
    ];
    rows.forEach(function (r) { dl.appendChild(el('dt', '', r[0])); dl.appendChild(el('dd', '', r[1])); });
  }

  // ---- Live ----
  var last = null;
  function drawAim() {
    var cv = $('aim'), g = cv.getContext('2d'), w = cv.width, h = cv.height;
    g.clearRect(0, 0, w, h);
    g.strokeStyle = '#2a3242';
    g.lineWidth = 1;
    g.strokeRect(0.5, 0.5, w - 1, h - 1);
    g.beginPath();
    g.moveTo(w / 2, 0); g.lineTo(w / 2, h);
    g.moveTo(0, h / 2); g.lineTo(w, h / 2);
    g.stroke();
    var a = last && last.aim;
    if (!a || !a.valid) { $('aim-note').textContent = 'geen geldige aim (Move verbonden?)'; return; }
    $('aim-note').textContent = a.off_screen ? 'buiten het scherm' : '';
    function dot(x, y, color, r) {
      var px = Math.max(0, Math.min(1, x)) * w, py = Math.max(0, Math.min(1, y)) * h;
      g.fillStyle = color;
      g.beginPath(); g.arc(px, py, r, 0, 6.2832); g.fill();
    }
    dot(a.x_raw, a.y_raw, '#7d8799', 5);
    dot(a.x, a.y, '#4fb3ff', 8);
  }
  function drawMini() {
    var cv = $('mini'), g = cv.getContext('2d'), w = cv.width, h = cv.height;
    g.clearRect(0, 0, w, h);
    g.strokeStyle = '#2a3242';
    g.strokeRect(0.5, 0.5, w - 1, h - 1);
    var p = last && last.position;
    if (!p || !p.valid) {
      $('mini-note').textContent = 'Geen bol gevonden' + (p && p.reason && p.reason !== 'none' ? ' (' + p.reason + ')' : '') + '. Alleen coördinaten, geen beeld.';
      return;
    }
    $('mini-note').textContent = 'x ' + fmt(p.x_norm, 3) + ', y ' + fmt(p.y_norm, 3) + ' (genormaliseerd, links-boven 0,0). Alleen coördinaten, geen beeld.';
    var r = Math.max(4, (p.radius_px / p.image_w) * w);
    g.strokeStyle = '#5ad19a';
    g.lineWidth = 2;
    g.beginPath(); g.arc(p.x_norm * w, p.y_norm * h, r, 0, 6.2832); g.stroke();
    g.fillStyle = '#5ad19a';
    g.beginPath(); g.arc(p.x_norm * w, p.y_norm * h, 2, 0, 6.2832); g.fill();
  }
  function showButtons() {
    if (!last) return;
    $('trig').style.width = (last.trigger / 255 * 100) + '%';
    $('trig-v').textContent = String(last.trigger);
    var b = $('btns');
    b.textContent = '';
    last.buttons.down.forEach(function (n) { b.appendChild(el('span', '', n)); });
    if (!last.buttons.down.length) b.textContent = 'geen knoppen ingedrukt';
  }
  function onSample(ev) {
    var d;
    try { d = JSON.parse(ev.data); } catch (e) { return; }
    last = d.sample;
    drawAim(); drawMini(); showButtons();
  }
  function startStream() {
    stopStream();
    if (!window.EventSource) { $('stream-state').textContent = 'deze browser ondersteunt geen live stroom'; return; }
    es = new EventSource('/api/stream');
    es.addEventListener('sample', onSample);
    es.onopen = function () { $('stream-state').textContent = 'stroom actief (max 30 Hz)'; setConn(true, 'verbonden met daemon'); };
    es.onerror = function () { $('stream-state').textContent = 'stroom verbroken, opnieuw proberen...'; };
  }
  function stopStream() { if (es) { es.close(); es = null; } }

  // ---- Apparaten ----
  function moveRows(s) {
    var c = s.controller;
    return [
      ['Verbonden', c.connected ? 'ja' : 'nee, geen Move gevonden', c.connected ? 'ok' : 'warn'],
      ['Model', c.model || '-'],
      ['Verbinding', c.connection || '-'],
      ['Controller-ID (geredigeerd)', c.id || '-'],
      ['Kalibratie in de controller', c.connected ? yn(c.has_calibration) : '-'],
      ['Magnetometer', c.magnetometer]
    ];
  }
  function camRows(s) {
    var cam = s.camera || {}, d = cam.device || {}, m = cam.mode || {};
    if (!cam.enabled) return [['Camera', 'uit (--no-camera)', 'muted']];
    return [
      ['Toestand', cam.state, cam.state === 'streaming' ? 'ok' : 'warn'],
      ['Model', d.card || '-'],
      ['Driver', d.driver || '-'],
      ['Stabiel pad', d.by_id || d.by_path || d.node || '-'],
      ['Modus', m.width ? m.width + 'x' + m.height + ' ' + (m.format || '') + ' @ ' + fmt(m.driver_fps, 0) + ' fps' : '-']
    ];
  }
  function outRows(o) {
    var d = o.device || {};
    return [
      ['Armed', o.armed ? 'ja (stuurt invoer)' : 'nee (stuurt niets)', o.armed ? 'warn' : ''],
      ['Apparaat', d.name || '-'],
      ['Stabiele link', d.stable_link || '-'],
      ['Event-node', d.created ? (d.event_node || '-') : 'nog niet aangemaakt (eerste arm maakt het)'],
      ['USB-id', d.vendor ? d.vendor + ':' + d.product : '-']
    ];
  }
  function refreshDevices() {
    api('/api/status').then(function (st) {
      if (!st.ok) return;
      var s = st.result, c = s.controller;
      var rows = moveRows(s);
      rows.push(['Batterij', c.connected ? c.battery.text : '-']);
      rows.push(['Verbonden sinds', c.connected ? dur(c.connected_s) : '-']);
      rows.push(['Verbindingen / verbroken', c.connects + ' / ' + c.disconnects]);
      fill($('dev-move'), rows);
      var cr = camRows(s);
      fill($('dev-cam'), cr);
      fill($('dev-out'), outRows(s.output || {}));
    });
  }

  // ---- Camera ----
  function findCtrl(cam, test) {
    var list = cam.controls || [];
    for (var i = 0; i < list.length; i++) {
      if (test(list[i].name.toLowerCase())) return list[i];
    }
    return null;
  }
  function refreshCameraStatus() {
    api('/api/camera').then(function (j) {
      if (!j.ok) return;
      var cam = j.result, fr = cam.frames || {}, m = cam.mode || {};
      if (!cam.enabled) { fill($('cam-status'), [['Camera', 'uit (ravengund draait met --no-camera)', 'muted']]); return; }
      var pu = cam.processing_us || {}, age = cam.age_at_processing_ms || {};
      fill($('cam-status'), [
        ['Toestand', cam.state, cam.state === 'streaming' ? 'ok' : 'warn'],
        ['Fout', cam.error || '-', cam.error ? 'bad' : ''],
        ['Modus', m.width ? m.width + 'x' + m.height + ' @ ' + fmt(m.driver_fps, 0) + ' fps' : '-'],
        ['Frames per seconde (5 s)', fmt(fr.fps_5s)],
        ['Gemiste frames', String(fr.missed_by_sequence)],
        ['Foutframes / streamfouten', fr.error_frames + ' / ' + fr.stream_errors],
        ['Verwerking p50 / p95 / max (µs)', fmt(pu.p50, 0) + ' / ' + fmt(pu.p95, 0) + ' / ' + fmt(pu.max, 0)],
        ['Leeftijd bij verwerking p95 (ms)', fmt(age.p95, 1)]
      ]);
    });
  }
  function syncPair(rangeId, numId) {
    $(rangeId).addEventListener('input', function () { $(numId).value = $(rangeId).value; });
    $(numId).addEventListener('input', function () { $(rangeId).value = $(numId).value; });
  }
  function camAuto() {
    $('cam-exp').disabled = $('cam-exp-r').disabled = $('cam-ae').checked;
    $('cam-gain').disabled = $('cam-gain-r').disabled = $('cam-ag').checked;
  }
  function initCameraForm() {
    Promise.all([api('/api/camera'), api('/api/settings')]).then(function (res) {
      var cam = res[0].ok ? res[0].result : {}, cfg = res[1].ok ? res[1].result.values : null;
      var cc = (cfg && cfg.camera) || {};
      var ex = findCtrl(cam, function (n) { return n.indexOf('exposure') >= 0 && n.indexOf('auto') < 0; });
      var ga = findCtrl(cam, function (n) { return n.indexOf('gain') >= 0 && n.indexOf('auto') < 0; });
      var ag = findCtrl(cam, function (n) { return n.indexOf('gain') >= 0 && n.indexOf('auto') >= 0; });
      var ae = findCtrl(cam, function (n) { return n.indexOf('exposure') >= 0 && n.indexOf('auto') >= 0; });
      var wb = findCtrl(cam, function (n) { return n.indexOf('white') >= 0 && n.indexOf('auto') >= 0; });
      var live = cam.enabled && cam.state === 'streaming';
      var m = cam.mode || {};
      if (live && m.width) $('cam-mode').value = m.width + 'x' + m.height + '@' + m.requested_fps;
      else if (cc.mode) $('cam-mode').value = cc.mode;
      var exp = live && ex && ex.value !== null ? ex.value : (cc.exposure !== undefined ? cc.exposure : 50);
      var gain = live && ga && ga.value !== null ? ga.value : (cc.gain !== undefined ? cc.gain : 0);
      $('cam-exp').value = $('cam-exp-r').value = exp;
      $('cam-gain').value = $('cam-gain-r').value = gain;
      // auto_exposure is een menu: 0 = automatisch, 1 = handmatig.
      $('cam-ae').checked = live && ae && ae.value !== null ? ae.value !== 1 : !!cc.auto_exposure;
      $('cam-ag').checked = live && ag && ag.value !== null ? ag.value !== 0 : !!cc.auto_gain;
      $('cam-awb').checked = live && wb && wb.value !== null ? wb.value !== 0 : !!cc.auto_awb;
      camAuto();
    });
  }
  function showReadback(r) {
    var c = r.controls, m = r.mode;
    fill($('cam-readback'), [
      ['Modus', m.width + 'x' + m.height + ' @ ' + fmt(m.driver_fps, 0) + ' fps (driver)'],
      ['Capture herstart', yn(r.restarted)],
      ['Automatische belichting', c.readable ? yn(c.auto_exposure) : '?'],
      ['Belichting', c.readable ? String(c.exposure) : '?'],
      ['Automatische gain', c.readable ? yn(c.auto_gain) : '?'],
      ['Gain', c.readable ? String(c.gain) : '?'],
      ['Automatische witbalans', c.readable ? yn(c.auto_awb) : '?'],
      ['Bewaard', r.saved ? 'ja' : 'nee' + (r.save_error ? ': ' + r.save_error : '')]
    ]);
    setHidden($('cam-warn'), !r.warnings.length, r.warnings.join('. '));
  }
  $('f-camera').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var exp = num('cam-exp', 0, 255, 'Belichting', errs, true);
    var gain = num('cam-gain', 0, 63, 'Gain', errs, true);
    $('cam-err').textContent = errs.join('. ');
    if (errs.length) return;
    var body = { mode: $('cam-mode').value, auto_exposure: $('cam-ae').checked, auto_gain: $('cam-ag').checked,
                 auto_awb: $('cam-awb').checked, exposure: exp, gain: gain };
    submit('/api/camera', body, $('cam-err'), 'Camera ingesteld', e.target.querySelector('button'), function (r) {
      showReadback(r);
      refreshCameraStatus();
    });
  });
  syncPair('cam-exp-r', 'cam-exp');
  syncPair('cam-gain-r', 'cam-gain');
  $('cam-ae').addEventListener('change', camAuto);
  $('cam-ag').addEventListener('change', camAuto);

  // ---- LED ----
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
      t.appendChild(el('small', '', p.hue_deg === null ? 'geen tint (tracker blind)' : 'tint ' + p.hue_deg + ' graden'));
      b.appendChild(chip);
      b.appendChild(t);
      b.addEventListener('click', function () { applyLed({ preset: p.name }, $('led-err'), b); });
      box.appendChild(b);
    });
  }
  function showLedResult(r) {
    var box = $('led-result');
    box.textContent = '';
    box.hidden = false;
    var led = r.led, tr = r.tracker;
    box.appendChild(el('p', '', 'LED: ' + (led.custom ? 'eigen kleur ' : led.preset + ' ') + led.rgb.join(', ')));
    box.appendChild(el('p', '', tr.changed
      ? 'Tracker-tint: ' + fmt(tr.hue_deg, 0) + ' graden, tolerantie ' + fmt(tr.hue_tol_deg, 0) + ' (' + tr.source + ')' + (tr.applied_live ? ', direct toegepast' : ', bewaard voor de volgende start (geen camera actief)')
      : 'Tracker-tint niet gewijzigd (' + fmt(tr.hue_deg, 0) + ' graden).'));
    r.warnings.forEach(function (w) { box.appendChild(el('p', 'warnbox', w)); });
    box.appendChild(el('p', 'muted', r.saved ? 'Bewaard in het instellingenbestand.' : 'Niet bewaard' + (r.save_error ? ': ' + r.save_error : '') + '.'));
  }
  function applyLed(body, errEl, btn) {
    submit('/api/led', body, errEl, 'LED ingesteld', btn, function (r) {
      activeLed = r.led.rgb;
      renderPresets();
      showLedResult(r);
      refreshLedNow();
    });
  }
  function refreshLedNow() {
    api('/api/status').then(function (st) {
      if (!st.ok) return;
      var s = st.result;
      showLedRows($('led-now'), s);
      var led = s.controller.led;
      if (led.enabled && (!activeLed || activeLed.join() !== led.rgb.join())) { activeLed = led.rgb; renderPresets(); }
    });
  }
  function syncColor() {
    var r = Number($('led-r').value) || 0, g = Number($('led-g').value) || 0, b = Number($('led-b').value) || 0;
    $('led-color').value = '#' + hex2(Math.max(0, Math.min(255, r))) + hex2(Math.max(0, Math.min(255, g))) + hex2(Math.max(0, Math.min(255, b)));
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

  // ---- Uitvoer ----
  var outFormFilled = false;
  function refreshOutput() {
    api('/api/output').then(function (j) {
      if (!j.ok) return;
      var o = j.result, p = o.pattern || {}, pos = o.position || {};
      var rows = outRows(o);
      rows.push(['Laatste positie', typeof pos.nx === 'number' ? fmt(pos.nx, 3) + ', ' + fmt(pos.ny, 3) : '-']);
      rows.push(['Knoppen ingedrukt', o.buttons_down && o.buttons_down.length ? o.buttons_down.join(', ') : 'geen']);
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
    });
  }
  $('out-arm').addEventListener('click', function (e) {
    if (!window.confirm('De virtuele lightgun inschakelen? Games krijgen dan muisbeweging en knoppen van de Move.')) return;
    submit('/api/arm', {}, $('out-err'), 'Uitvoer ingeschakeld', e.target, refreshOutput);
  });
  $('out-disarm').addEventListener('click', function (e) {
    submit('/api/disarm', {}, $('out-err'), 'Uitvoer uitgeschakeld', e.target, refreshOutput);
  });
  Array.prototype.forEach.call(document.querySelectorAll('.pat'), function (b) {
    b.addEventListener('click', function () {
      var name = b.getAttribute('data-pattern');
      var body = { pattern: name };
      if (name === 'grid' || name === 'corners') body.dwell_ms = 600;
      submit('/api/test-pattern', body, $('pat-err'), name === 'stop' ? 'Patroon gestopt' : 'Patroon gestart', b, refreshOutput);
    });
  });
  $('f-output').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var body = { invert_x: $('o-ix').checked, invert_y: $('o-iy').checked };
    body.scale_x = num('o-sx', 0.05, 20, 'Schaal X', errs);
    body.scale_y = num('o-sy', 0.05, 20, 'Schaal Y', errs);
    body.offset_x = num('o-ox', -1, 1, 'Offset X', errs);
    body.offset_y = num('o-oy', -1, 1, 'Offset Y', errs);
    body.viewport_x0 = num('o-x0', 0, 1, 'Viewport links', errs);
    body.viewport_y0 = num('o-y0', 0, 1, 'Viewport boven', errs);
    body.viewport_x1 = num('o-x1', 0, 1, 'Viewport rechts', errs);
    body.viewport_y1 = num('o-y1', 0, 1, 'Viewport onder', errs);
    $('o-err').textContent = errs.join('. ');
    if (errs.length) return;
    submit('/api/output', body, $('o-err'), 'Transformatie toegepast', e.target.querySelector('button'), refreshOutput);
  });

  // ---- Instellingen ----
  function loadSettings() {
    api('/api/status').then(function (j) {
      if (!j.ok) return;
      var f = j.result.config.filter, a = j.result.config.aim;
      $('flt-enabled').checked = f.enabled;
      $('flt-min').value = f.min_cutoff_hz;
      $('flt-beta').value = f.beta;
      $('flt-d').value = f.d_cutoff_hz;
      $('aim-h').value = a.h_edge_deg;
      $('aim-v').value = a.v_edge_deg;
    });
    Promise.all([api('/api/settings'), api('/api/info')]).then(function (res) {
      if (!res[0].ok || !res[1].ok) return;
      var s = res[0].result, inf = res[1].result, i = s.info, w = inf.daemon;
      if (s.values) $('web-port').value = s.values.web.port;
      fill($('web-now'), [
        ['Actieve poort', w.web_port ? String(w.web_port) : 'uit'],
        ['LAN-toegang', w.web_lan ? 'aan (token vereist)' : 'uit (alleen localhost)']
      ]);
      fill($('cfg-info'), [
        ['Bestand', i.available ? i.path : 'geen (--no-config)'],
        ['Aanwezig', yn(i.file_present)],
        ['Toestand', i.state, i.state === 'loaded' ? 'ok' : (i.state === 'missing' ? '' : 'warn')],
        ['Schemaversie', String(i.schema)]
      ]);
      var msgs = [];
      if (i.message) msgs.push(i.message);
      (i.warnings || []).forEach(function (m) { msgs.push(m); });
      if (i.last_save_error) msgs.push('Laatste opslagfout: ' + i.last_save_error);
      setHidden($('cfg-msg'), !msgs.length, msgs.join('. '));
    });
  }
  $('f-filter').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var body = { enabled: $('flt-enabled').checked };
    var a = num('flt-min', 0.001, 100, 'Minimale afsnijfrequentie', errs);
    var b = num('flt-beta', 0, 1000, 'Beta', errs);
    var c = num('flt-d', 0.001, 100, 'Afsnijfrequentie snelheid', errs);
    $('flt-err').textContent = errs.join('. ');
    if (errs.length) return;
    body.min_cutoff_hz = a; body.beta = b; body.d_cutoff_hz = c;
    submit('/api/filter', body, $('flt-err'), 'Filter toegepast en bewaard', e.target.querySelector('button'), loadSettings);
  });
  $('f-aim').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var h = num('aim-h', 1, 89, 'Horizontale hoek', errs);
    var v = num('aim-v', 1, 89, 'Verticale hoek', errs);
    $('aim-err').textContent = errs.join('. ');
    if (errs.length) return;
    submit('/api/aim', { h_edge_deg: h, v_edge_deg: v }, $('aim-err'), 'Schaal toegepast en bewaard', e.target.querySelector('button'), loadSettings);
  });
  $('recenter').addEventListener('click', function (e) {
    submit('/api/recenter', {}, $('rc-err'), 'Recenter gedaan', e.target);
  });
  $('f-web').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var port = num('web-port', 1, 65535, 'Poort', errs, true);
    $('web-err').textContent = errs.join('. ');
    if (errs.length) return;
    submit('/api/web', { port: port }, $('web-err'), 'Poort bewaard (geldt na een herstart)', e.target.querySelector('button'), loadSettings);
  });

  // ---- Info ----
  function refreshInfo() {
    Promise.all([api('/api/status'), api('/api/info')]).then(function (res) {
      if (!res[0].ok || !res[1].ok) return;
      var s = res[0].result, inf = res[1].result, v = inf.version, cam = s.camera || {}, fr = cam.frames || {}, r = s.reports;
      fill($('inf-ver'), [
        ['Daemon', v.daemon],
        ['Git-commit', v.git_commit],
        ['PSMoveAPI-pin', v.psmoveapi_pin],
        ['PSMoveAPI gelinkt', yn(v.psmoveapi_linked), v.psmoveapi_linked ? 'ok' : 'warn'],
        ['IPC-protocol', String(v.ipc_protocol)],
        ['Invoerschema', String(v.input_schema)]
      ]);
      var rows = moveRows(s).slice(0, 4).map(function (x, i) { return i === 0 ? ['Move', x[1], x[2]] : [i === 1 ? 'Move-model' : (i === 2 ? 'Move-verbinding' : 'Controller-ID'), x[1]]; });
      camRows(s).forEach(function (x) { rows.push([x[0] === 'Camera' ? 'Camera' : 'Camera: ' + x[0].toLowerCase(), x[1], x[2]]); });
      outRows(s.output || {}).slice(0, 3).forEach(function (x) { rows.push(['Uitvoer: ' + x[0].toLowerCase(), x[1], x[2]]); });
      fill($('inf-dev'), rows);
      fill($('inf-daemon'), [
        ['Looptijd', dur(inf.daemon.uptime_s)],
        ['PID', String(inf.daemon.pid)],
        ['Webpoort', inf.daemon.web_port ? String(inf.daemon.web_port) : 'uit'],
        ['Instellingen', inf.settings.available ? inf.settings.path + ' (' + inf.settings.state + ')' : 'geen']
      ]);
      var iv = r.interval_ms || {}, hp = s.processing_us || {};
      fill($('inf-imu'), [
        ['Rapportfrequentie (5 s)', fmt(r.rate_hz_5s) + ' Hz'],
        ['Interval p50 / p95 / max (ms)', fmt(iv.p50, 2) + ' / ' + fmt(iv.p95, 2) + ' / ' + fmt(iv.max, 1)],
        ['Gaten (volgnummer overgeslagen)', String(r.dropped_by_seq)],
        ['Lange tussenpozen (50 tot 250 ms)', String(r.long_dt_50_250ms)],
        ['Gaten niet geïntegreerd', String(r.gaps_not_integrated)],
        ['Verwerking p50 / p95 / max (µs)', fmt(hp.p50, 0) + ' / ' + fmt(hp.p95, 0) + ' / ' + fmt(hp.max, 0)]
      ]);
      if (!cam.enabled) { fill($('inf-cam'), [['Camera', 'uit (--no-camera)', 'muted']]); return; }
      var pu = cam.processing_us || {}, age = cam.age_at_processing_ms || {}, q = cam.queue || {};
      fill($('inf-cam'), [
        ['Frames per seconde (5 s)', fmt(fr.fps_5s)],
        ['Frames ontvangen', String(fr.received)],
        ['Gemiste frames', String(fr.missed_by_sequence)],
        ['Weggegooid door de wachtrij', String((q.dropped_full || 0) + (q.dropped_stale || 0))],
        ['Verwerking p50 / p95 / max (µs)', fmt(pu.p50, 0) + ' / ' + fmt(pu.p95, 0) + ' / ' + fmt(pu.max, 0)],
        ['Leeftijd bij verwerking p95 (ms)', fmt(age.p95, 1)]
      ]);
    }).catch(function () { setConn(false, 'daemon niet bereikbaar'); });
  }

  // ---- Navigatie ----
  var handlers = {
    overzicht: { show: refreshOverview, poll: refreshOverview, ms: 1000 },
    live: { show: function () { drawAim(); drawMini(); startStream(); } },
    apparaten: { show: refreshDevices, poll: refreshDevices, ms: 1000 },
    camera: { show: function () { initCameraForm(); refreshCameraStatus(); }, poll: refreshCameraStatus, ms: 1000 },
    led: { show: function () { renderPresets(); refreshLedNow(); }, poll: refreshLedNow, ms: 2000 },
    uitvoer: { show: function () { outFormFilled = false; refreshOutput(); }, poll: refreshOutput, ms: 1000 },
    instellingen: { show: loadSettings },
    info: { show: refreshInfo, poll: refreshInfo, ms: 2000 }
  };
  function show() {
    var name = (location.hash.replace(/^#\//, '') || 'overzicht');
    if (pages.indexOf(name) < 0) name = 'overzicht';
    if (name === current) return;
    current = name;
    pages.forEach(function (p) {
      $('p-' + p).className = 'page' + (p === name ? ' active' : '');
    });
    Array.prototype.forEach.call(document.querySelectorAll('#nav a'), function (a) {
      a.className = a.getAttribute('data-page') === name ? 'active' : '';
    });
    clearInterval(pollTimer);
    pollTimer = null;
    stopStream();
    var h = handlers[name];
    if (h) {
      h.show();
      if (h.poll) pollTimer = setInterval(h.poll, h.ms);
    }
  }
  window.addEventListener('hashchange', show);
  document.addEventListener('visibilitychange', function () {
    if (document.hidden) { stopStream(); clearInterval(pollTimer); pollTimer = null; }
    else { var n = current; current = ''; location.hash = location.hash || '#/' + n; show(); }
  });
  api('/api/settings').then(function (j) {
    if (j.ok && j.result.presets) {
      presetList = j.result.presets.map(function (p) {
        var l = p.name.charAt(0).toUpperCase() + p.name.slice(1);
        return { name: p.name, rgb: p.rgb, hue_deg: p.hue_deg, label: l };
      });
    }
  }).catch(function () {}).then(show);
})();

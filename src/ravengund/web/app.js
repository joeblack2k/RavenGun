'use strict';
// RavenGun-webinterface: geen externe bronnen, geen inline code (CSP). Alle tekst via textContent.
(function () {
  var csrf = (document.querySelector('meta[name="csrf-token"]') || {}).content || '';
  var $ = function (id) { return document.getElementById(id); };
  var pages = ['overzicht', 'live', 'instellingen', 'kalibratie', 'apparaten', 'games'];
  var current = '';
  var pollTimer = null;
  var es = null;

  function api(path, body) {
    var opt = { cache: 'no-store' };
    if (body !== undefined) {
      opt.method = 'POST';
      opt.headers = { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf };
      opt.body = JSON.stringify(body);
    }
    return fetch(path, opt).then(function (r) {
      return r.json().then(function (j) { j.httpStatus = r.status; return j; });
    });
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

  // ---- Overzicht ----
  function refreshOverview() {
    Promise.all([api('/api/status'), api('/api/sample')]).then(function (res) {
      var st = res[0], sm = res[1];
      if (!st.ok || !sm.ok) { setConn(false, 'daemon geeft een fout'); return; }
      setConn(true, 'verbonden met daemon');
      var s = st.result, p = sm.result, c = s.controller, cam = s.camera || {};
      fill($('ov-move'), [
        ['Verbonden', c.connected ? 'ja (' + c.model + ', ' + c.connection + ')' : 'nee, geen Move gevonden', c.connected ? 'ok' : 'warn'],
        ['Batterij', c.connected ? c.battery.text : '-'],
        ['Resolutie batterij', c.battery.resolution],
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
      fill($('ov-track'), [
        ['Modus', p.tracking_mode],
        ['Oriëntatie geldig', yn(p.orientation.valid), p.orientation.valid ? 'ok' : 'warn'],
        ['Aim geldig', yn(p.aim.valid), p.aim.valid ? 'ok' : 'warn'],
        ['Midden bepaald door', p.aim.center_source],
        ['Filter', p.aim.filter_enabled ? 'aan' : 'uit']
      ]);
      fill($('ov-daemon'), [
        ['PID', String(s.daemon.pid)],
        ['Looptijd', fmt(s.daemon.uptime_s, 0) + ' s'],
        ['HID', s.daemon.hid_enabled ? 'aan' : 'uit (--no-hid)']
      ]);
    }).catch(function () { setConn(false, 'daemon niet bereikbaar'); });
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
    last.buttons.down.forEach(function (n) {
      var s = document.createElement('span');
      s.textContent = n;
      b.appendChild(s);
    });
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

  // ---- Instellingen ----
  function num(id, lo, hi, name, errs) {
    var el = $(id), raw = el.value.trim().replace(',', '.');
    el.classList.remove('invalid');
    var v = raw === '' ? NaN : Number(raw);
    if (!isFinite(v) || v < lo || v > hi) {
      errs.push(name + ' moet een getal tussen ' + lo + ' en ' + hi + ' zijn');
      el.classList.add('invalid');
      return null;
    }
    return v;
  }
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
  }
  function submit(path, body, errEl, okMsg, btn) {
    errEl.textContent = '';
    btn.disabled = true;
    api(path, body).then(function (j) {
      btn.disabled = false;
      if (j.ok) { toast(okMsg); loadSettings(); } else { errEl.textContent = errText(j); }
    }).catch(function () { btn.disabled = false; errEl.textContent = 'daemon niet bereikbaar'; });
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
    submit('/api/filter', body, $('flt-err'), 'Filter toegepast', e.target.querySelector('button'));
  });
  $('f-aim').addEventListener('submit', function (e) {
    e.preventDefault();
    var errs = [];
    var h = num('aim-h', 1, 89, 'Horizontale hoek', errs);
    var v = num('aim-v', 1, 89, 'Verticale hoek', errs);
    $('aim-err').textContent = errs.join('. ');
    if (errs.length) return;
    submit('/api/aim', { h_edge_deg: h, v_edge_deg: v }, $('aim-err'), 'Schaal toegepast', e.target.querySelector('button'));
  });
  $('recenter').addEventListener('click', function (e) {
    submit('/api/recenter', {}, $('rc-err'), 'Recenter gedaan', e.target);
  });

  // ---- Navigatie ----
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
    if (name === 'overzicht') { refreshOverview(); pollTimer = setInterval(refreshOverview, 1000); }
    if (name === 'live') { drawAim(); drawMini(); startStream(); }
    if (name === 'instellingen') loadSettings();
  }
  window.addEventListener('hashchange', show);
  document.addEventListener('visibilitychange', function () {
    if (document.hidden) { stopStream(); clearInterval(pollTimer); pollTimer = null; }
    else { var n = current; current = ''; location.hash = location.hash || '#/' + n; show(); }
  });
  show();
})();

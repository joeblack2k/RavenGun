'use strict';
// RavenGun app: pure logic without DOM (testable with node, tests/js/app_core_test.js).
// The page (app.js) uses this as window.RGApp. No external resources.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.RGApp = factory();
})(typeof self !== 'undefined' ? self : this, function () {

  // ---- Tabs ----
  var TABS = [
    { id: 'overzicht', label: 'Overview' },
    { id: 'richten', label: 'Aiming' },
    { id: 'trillen', label: 'Rumble' },
    { id: 'knoppen', label: 'Buttons' },
    { id: 'led', label: 'LED and camera' },
    { id: 'games', label: 'Games' },
    { id: 'controllers', label: 'Controllers' },
    { id: 'info', label: 'Info (technical)' }
  ];
  // Tabs with a player choice at the top (the settings apply per controller).
  var PLAYER_TABS = ['overzicht', 'richten', 'trillen', 'knoppen', 'led'];
  function hasPlayerBar(tab) { return PLAYER_TABS.indexOf(tab) >= 0; }
  function isTab(id) {
    for (var i = 0; i < TABS.length; i++) if (TABS[i].id === id) return true;
    return false;
  }
  // Priority: the hash in the URL, then the remembered tab, otherwise the overview.
  function pickTab(hash, stored) {
    var h = String(hash || '').replace(/^#\/?/, '');
    if (isTab(h)) return h;
    if (isTab(stored)) return stored;
    return 'overzicht';
  }

  // ---- Numbers ----
  function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }
  // English display: a dot as the decimal separator.
  function fmtNl(v, digits) {
    if (typeof v !== 'number' || !isFinite(v)) return '-';
    var d = digits === undefined ? 1 : digits;
    return v.toFixed(d);
  }
  // Round to `sig` significant digits (for logarithmic sliders).
  function niceRound(v, sig) {
    if (!isFinite(v) || v === 0) return v;
    var p = Math.pow(10, (sig || 2) - 1 - Math.floor(Math.log10(Math.abs(v))));
    return Math.round(v * p) / p;
  }
  function percent255(v) { return Math.round(clamp(v, 0, 255) / 255 * 100); }
  // Logarithmic scale: position 0..steps <-> value lo..hi.
  function logScale(lo, hi, steps) {
    var a = Math.log(lo), b = Math.log(hi);
    return {
      steps: steps,
      toValue: function (pos) { return niceRound(Math.exp(a + (b - a) * clamp(pos, 0, steps) / steps), 2); },
      toPos: function (val) { return Math.round(clamp((Math.log(clamp(val, lo, hi)) - a) / (b - a), 0, 1) * steps); }
    };
  }

  // ---- Battery ----
  // Codes of PSMove_Battery_Level (six levels plus charging and full on the charger); never an invented percentage.
  var BATT_LEVELS = {
    min: { seg: 1, level: 'low', label: 'Almost empty', detail: 'less than 20%' },
    ge20: { seg: 2, level: 'mid', label: 'Low', detail: 'at least 20%' },
    ge40: { seg: 3, level: 'ok', label: 'Half full', detail: 'at least 40%' },
    ge60: { seg: 4, level: 'ok', label: 'Good', detail: 'at least 60%' },
    ge80: { seg: 5, level: 'ok', label: 'Almost full', detail: 'at least 80%' },
    max: { seg: 5, level: 'ok', label: 'Full', detail: '100%' }
  };
  // percent: optional number 0..100 (Navigation Controller); then the percentage is shown in the text.
  function batteryView(code, connected, percent) {
    if (!connected) return { segments: 0, level: 'none', label: 'Unknown', detail: 'not connected', charging: false };
    if (code === 'charging') return { segments: 5, level: 'charging', label: 'Charging', detail: '', charging: true };
    if (code === 'charged') return { segments: 5, level: 'full', label: 'Full', detail: 'on the charger', charging: false };
    var b = BATT_LEVELS[code];
    if (!b) return { segments: 0, level: 'none', label: 'Unknown', detail: '', charging: false };
    var detail = typeof percent === 'number' && percent >= 0 ? percent + '%' : b.detail;
    return { segments: b.seg, level: b.level, label: b.label, detail: detail, charging: false };
  }

  // ---- Sliders: debounce with confirmation ----
  // input(v): while dragging; at most one request per `delay` ms and always the latest value.
  // commit(v): after release; send immediately. There is never more than one request at a time. onDone(resp, v)
  // follows only when no newer value is waiting, so the confirmation shows the actual value from the daemon.
  function createLiveSender(o) {
    var st = o.setTimeout || setTimeout, ct = o.clearTimeout || clearTimeout;
    var delay = o.delay === undefined ? 150 : o.delay;
    var timer = null, inFlight = false, hasPending = false, pending;
    function flush() {
      timer = null;
      if (inFlight || !hasPending) return;
      var v = pending;
      hasPending = false;
      inFlight = true;
      var p;
      try { p = Promise.resolve(o.send(v)); } catch (e) { p = Promise.reject(e); }
      p.then(function (resp) {
        inFlight = false;
        if (hasPending) { schedule(); return; }
        if (o.onDone) o.onDone(resp, v);
      }, function (err) {
        inFlight = false;
        if (o.onError) o.onError(err);
        if (hasPending) schedule();
      });
    }
    function schedule() { if (!timer && !inFlight) timer = st(flush, delay); }
    return {
      input: function (v) { pending = v; hasPending = true; schedule(); },
      commit: function (v) { pending = v; hasPending = true; if (timer) { ct(timer); timer = null; } flush(); },
      busy: function () { return inFlight || hasPending; }
    };
  }

  // ---- Button layout ----
  var BUTTONS = [
    { id: 'move', label: 'Move button (large button on the front)', group: 'move' },
    { id: 'cross', label: 'Cross', group: 'move' },
    { id: 'circle', label: 'Circle', group: 'move' },
    { id: 'square', label: 'Square', group: 'move' },
    { id: 'triangle', label: 'Triangle', group: 'move' },
    { id: 'select', label: 'Select', group: 'move' },
    { id: 'start', label: 'Start', group: 'move' },
    { id: 'pump', label: 'Pump', group: 'sharp' },
    { id: 'ss_rl', label: 'RL button', group: 'sharp' },
    { id: 'ss_fire', label: 'Fire button', group: 'sharp' },
    { id: 'sel1', label: 'Fire mode 1', group: 'sharp' },
    { id: 'sel2', label: 'Fire mode 2', group: 'sharp' },
    { id: 'sel3', label: 'Fire mode 3', group: 'sharp' },
    { id: 'nav_l1', label: 'L1', group: 'nav' },
    { id: 'nav_l2', label: 'L2', group: 'nav' },
    { id: 'nav_cross', label: 'Cross', group: 'nav' },
    { id: 'nav_circle', label: 'Circle', group: 'nav' },
    { id: 'nav_ps', label: 'PS button', group: 'nav' },
    { id: 'nav_l3', label: 'Stick press (L3)', group: 'nav' },
    { id: 'nav_up', label: 'Arrow up', group: 'nav' },
    { id: 'nav_down', label: 'Arrow down', group: 'nav' },
    { id: 'nav_left', label: 'Arrow left', group: 'nav' },
    { id: 'nav_right', label: 'Arrow right', group: 'nav' },
    { id: 'nav_stick_up', label: 'Stick up', group: 'nav' },
    { id: 'nav_stick_down', label: 'Stick down', group: 'nav' },
    { id: 'nav_stick_left', label: 'Stick left', group: 'nav' },
    { id: 'nav_stick_right', label: 'Stick right', group: 'nav' }
  ];
  var KEY_LABELS = {
    '5': 'Coin (key 5)', '1': 'Start player 1 (key 1)', '2': 'Start player 2 (key 2)', f2: 'Test (key F2)', f3: 'Service (key F3)',
    esc: 'Esc', enter: 'Enter', space: 'Space', tab: 'Tab', backspace: 'Backspace',
    up: 'Arrow up', down: 'Arrow down', left: 'Arrow left', right: 'Arrow right',
    lshift: 'Left Shift', lctrl: 'Left Ctrl', lalt: 'Left Alt'
  };
  var MAIN_KEYS = ['5', '1', '2', 'f2', 'f3'];
  var OTHER_KEYS = ['esc', 'enter', 'space', 'tab', 'backspace', 'up', 'down', 'left', 'right', 'lshift', 'lctrl', 'lalt',
    'f1', 'f4', 'f5', 'f6', 'f7', 'f8', 'f9', 'f10', 'f11', 'f12', '0', '3', '4', '6', '7', '8', '9',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'];
  function keyLabel(k) {
    if (KEY_LABELS[k]) return KEY_LABELS[k];
    return 'Key ' + k.toUpperCase();
  }
  // Choice list: groups with options { value, label }.
  function actionGroups() {
    var g = [
      { label: 'Shooting and playing', options: [
        { value: 'none', label: 'Do nothing' }, { value: 'reload', label: 'Reload' }, { value: 'grenade', label: 'Grenade' },
        { value: 'aux1', label: 'Extra button 1' }, { value: 'aux2', label: 'Extra button 2' }, { value: 'recenter', label: 'Center' }] },
      { label: 'Game menu (keyboard)', options: MAIN_KEYS.map(function (k) { return { value: 'key:' + k, label: keyLabel(k) }; }) },
      { label: 'Other keys', options: OTHER_KEYS.map(function (k) { return { value: 'key:' + k, label: keyLabel(k) }; }) }
    ];
    return g;
  }
  function actionLabel(a) {
    var gs = actionGroups();
    for (var i = 0; i < gs.length; i++)
      for (var j = 0; j < gs[i].options.length; j++) if (gs[i].options[j].value === a) return gs[i].options[j].label;
    return a;
  }
  // Only the buttons whose action differs from the base.
  function mapChanges(base, edit) {
    var out = {}, n = 0;
    BUTTONS.forEach(function (b) {
      var e = edit[b.id], a = base[b.id];
      if (e !== undefined && e !== (a === undefined ? 'none' : a)) { out[b.id] = e; n++; }
    });
    return { changes: out, count: n };
  }
  // Simple text for "Move button -> function".
  function mapSummary(map) {
    return BUTTONS.map(function (b) { return b.label + ' -> ' + actionLabel(map[b.id] || 'none'); });
  }
  // Which buttons are pressed now? (names from sample.buttons.down); T counts as the trigger.
  function pressedSet(down, trigger) {
    var s = {};
    (down || []).forEach(function (n) { s[n] = true; });
    if (trigger >= 140) s.t = true;
    return s;
  }

  // ---- Drift correction ----
  var FUSION_CHOICES = [
    { id: 'vqf_mag', label: 'Compass (recommended)', help: 'Uses the compass in the Move to stop the aim point from drifting slowly.' },
    { id: 'vqf', label: 'Without compass (new method)', help: 'For when the compass is disturbed by nearby metal or magnets.' },
    { id: 'mahony_mag', label: 'Compass (old method)', help: 'The earlier compass method. Only useful for comparison.' },
    { id: 'mahony', label: 'Gyro only', help: 'No compass: the aim point can drift slowly. Center more often.' }
  ];
  function fusionLabel(id) {
    for (var i = 0; i < FUSION_CHOICES.length; i++) if (FUSION_CHOICES[i].id === id) return FUSION_CHOICES[i].label;
    return id || '-';
  }

  // ---- Output clock ----
  function clockChoices(current) {
    var base = [0, 125, 250, 500];
    var list = base.slice();
    if (typeof current === 'number' && isFinite(current) && list.indexOf(current) < 0) { list.push(current); list.sort(function (a, b) { return a - b; }); }
    return list.map(function (hz) { return { hz: hz, label: hz === 0 ? 'Off' : hz + ' Hz' }; });
  }

  // ---- Games ----
  // status: { state, text, profile, message }. What the interface shows and allows per state.
  function gameStateView(st) {
    st = st || {};
    var busy = st.state === 'starting' || st.state === 'waiting_move' || st.state === 'stopping';
    var tone = st.state === 'running' ? 'ok' : st.state === 'failed' ? 'bad' : busy ? 'warn' : 'info';
    var text = st.text || 'Unknown';
    if (st.state === 'failed' && st.message) text += ' ' + st.message;
    return {
      text: text, tone: tone, busy: busy,
      canStart: st.state === 'idle' || st.state === 'failed' || st.state === undefined,
      canStop: st.state === 'running' || st.state === 'starting' || st.state === 'waiting_move' || !!st.stoppable
    };
  }
  // For a game card: is this game central in the current state?
  function gameCardState(st, profile) {
    var cur = st && st.profile === profile;
    var state = (st && st.state) || 'idle';
    return {
      active: !!cur && (state === 'running' || state === 'starting' || state === 'waiting_move' || state === 'stopping'),
      failed: !!cur && state === 'failed'
    };
  }

  // ---- Plain-language summary (Overview) ----
  function plainSettings(status) {
    var cfg = (status && status.config) || {};
    var f = cfg.filter || {}, a = cfg.aim || {}, fu = cfg.fusion || {};
    var rum = (status && status.controller && status.controller.rumble) || {};
    var clk = (status && status.output && status.output.live && status.output.live.clock) || {};
    var lines = [];
    lines.push(['Smooth cursor', f.enabled ? 'on' : 'off']);
    lines.push(['Drift correction', fusionLabel(fu.mode)]);
    lines.push(['Prediction', typeof a.predict_ms === 'number' ? fmtNl(a.predict_ms, 0) + ' ms' : '-']);
    lines.push(['Rumble', rum.enabled ? 'on, strength ' + percent255(rum.strength) + '%' : 'off']);
    if (status && status.yaw_trim) lines.push(['Learn from centering', status.yaw_trim.learning ? 'on' : 'off']);
    if (typeof clk.enabled === 'boolean') lines.push(['Output clock', clk.enabled ? clk.hz + ' Hz' : 'off']);
    return lines;
  }

  // ---- Players and controllers ----
  var MAX_PLAYERS = 4;
  // The remembered player number (1..4), otherwise 1.
  function pickPlayer(stored) {
    var n = Number(stored);
    return n >= 1 && n <= MAX_PLAYERS && Math.floor(n) === n ? n : 1;
  }
  // Choices for the player bar from status.assignment.players: [{player, controller: name|null}]. Player 1 is always there;
  // players without a Move only when selected (then the user sees that there is no Move).
  function playerOptions(players, current) {
    var out = [];
    (players || []).forEach(function (p) {
      if (p.player === 1 || p.controller || p.player === current)
        out.push({ player: p.player, label: 'Player ' + p.player, sub: p.controller || 'no Move' });
    });
    if (!out.length) out.push({ player: 1, label: 'Player 1', sub: 'no Move' });
    return out;
  }
  // Adds the selected player to a request (a copy; the original stays unchanged).
  function withPlayer(body, player) {
    var o = {};
    Object.keys(body || {}).forEach(function (k) { o[k] = body[k]; });
    o.player = pickPlayer(player);
    return o;
  }
  function controllerTypeLabel(type) { return type === 'navigation' ? 'Navigation Controller' : 'Move'; }
  // "Last seen" in plain language; nowS and unixS in seconds.
  function lastSeenText(unixS, nowS) {
    if (typeof unixS !== 'number' || !(unixS > 0)) return 'never';
    var d = Math.max(0, Math.round(nowS - unixS));
    if (d < 90) return 'just now';
    if (d < 3600) return Math.round(d / 60) + ' minutes ago';
    if (d < 2 * 3600) return 'an hour ago';
    if (d < 86400) return Math.round(d / 3600) + ' hours ago';
    if (d < 2 * 86400) return 'yesterday';
    return Math.round(d / 86400) + ' days ago';
  }
  // Battery of a controller from the list (/api/controllers): the same display as for the Move.
  function controllerBattery(c) {
    var b = (c && c.battery) || {};
    return batteryView(b.code, !!(c && c.connected && b.known), typeof b.percent === 'number' ? b.percent : undefined);
  }
  // The pairing wizard: step (0 cable in, 1 pair, 2 cable out, 3 PS button, 4 done), what is shown and what to do.
  var PAIR_STEPS = ['Cable in', 'Pairing', 'Cable out', 'PS button'];
  function pairView(p) {
    p = p || {};
    var st = p.state || 'idle';
    var v = { state: st, step: 0, tone: 'info', busy: !!p.busy, active: !!p.active, canStart: false, startLabel: 'Pair a new controller',
      canConfirm: false, confirmLabel: '', canCancel: false, showInstall: false, text: p.instruction || '', message: p.message || '' };
    switch (st) {
      case 'wait_usb': v.step = 0; v.canCancel = true; break;
      case 'already_paired': v.step = 1; v.canConfirm = true; v.confirmLabel = 'Pair again'; v.canCancel = true; v.tone = 'warn'; break;
      case 'set_master': case 'register': v.step = 1; v.busy = true; v.canCancel = false; break;
      case 'need_helper': v.step = 1; v.showInstall = true; v.canConfirm = true; v.confirmLabel = 'Try again'; v.canCancel = true; v.tone = 'warn'; break;
      case 'wait_unplug': v.step = 2; v.canCancel = true; break;
      case 'wait_bluetooth': v.step = 3; v.canCancel = true; break;
      case 'done': v.step = 4; v.tone = 'ok'; v.canStart = true; v.startLabel = 'Pair another controller'; break;
      case 'failed': v.step = 0; v.tone = 'bad'; v.canStart = true; v.startLabel = 'Start again'; break;
      default: v.step = 0; v.canStart = true;  // idle, cancelled
    }
    if (st === 'cancelled' || st === 'idle') v.text = 'Connect a new Move or Navigation Controller with a USB cable and press Pair a new controller.';
    return v;
  }

  return {
    TABS: TABS, isTab: isTab, pickTab: pickTab, hasPlayerBar: hasPlayerBar,
    MAX_PLAYERS: MAX_PLAYERS, pickPlayer: pickPlayer, playerOptions: playerOptions, withPlayer: withPlayer,
    controllerTypeLabel: controllerTypeLabel, lastSeenText: lastSeenText, controllerBattery: controllerBattery,
    PAIR_STEPS: PAIR_STEPS, pairView: pairView,
    clamp: clamp, fmtNl: fmtNl, niceRound: niceRound, percent255: percent255, logScale: logScale,
    batteryView: batteryView, createLiveSender: createLiveSender,
    BUTTONS: BUTTONS, actionGroups: actionGroups, actionLabel: actionLabel, keyLabel: keyLabel, mapChanges: mapChanges,
    mapSummary: mapSummary, pressedSet: pressedSet,
    FUSION_CHOICES: FUSION_CHOICES, fusionLabel: fusionLabel, clockChoices: clockChoices,
    gameStateView: gameStateView, gameCardState: gameCardState, plainSettings: plainSettings
  };
});

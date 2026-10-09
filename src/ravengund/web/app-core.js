'use strict';
// RavenGun-app: pure logica zonder DOM (testbaar met node, tests/js/app_core_test.js).
// De pagina (app.js) gebruikt dit als window.RGApp. Geen externe bronnen.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.RGApp = factory();
})(typeof self !== 'undefined' ? self : this, function () {

  // ---- Tabbladen ----
  var TABS = [
    { id: 'overzicht', label: 'Overzicht' },
    { id: 'richten', label: 'Richten' },
    { id: 'trillen', label: 'Trillen' },
    { id: 'knoppen', label: 'Knoppen' },
    { id: 'led', label: 'LED en camera' },
    { id: 'games', label: 'Games' },
    { id: 'controllers', label: 'Controllers' },
    { id: 'info', label: 'Info (technisch)' }
  ];
  // Tabbladen met een spelerkeuze bovenaan (de instellingen gelden per controller).
  var PLAYER_TABS = ['overzicht', 'richten', 'trillen', 'knoppen', 'led'];
  function hasPlayerBar(tab) { return PLAYER_TABS.indexOf(tab) >= 0; }
  function isTab(id) {
    for (var i = 0; i < TABS.length; i++) if (TABS[i].id === id) return true;
    return false;
  }
  // Voorrang: de hash in de URL, dan het onthouden tabblad, anders het overzicht.
  function pickTab(hash, stored) {
    var h = String(hash || '').replace(/^#\/?/, '');
    if (isTab(h)) return h;
    if (isTab(stored)) return stored;
    return 'overzicht';
  }

  // ---- Getallen ----
  function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }
  // Nederlandse weergave: komma als decimaalteken.
  function fmtNl(v, digits) {
    if (typeof v !== 'number' || !isFinite(v)) return '-';
    var d = digits === undefined ? 1 : digits;
    return v.toFixed(d).replace('.', ',');
  }
  // Rond af op `sig` significante cijfers (voor logaritmische schuifjes).
  function niceRound(v, sig) {
    if (!isFinite(v) || v === 0) return v;
    var p = Math.pow(10, (sig || 2) - 1 - Math.floor(Math.log10(Math.abs(v))));
    return Math.round(v * p) / p;
  }
  function percent255(v) { return Math.round(clamp(v, 0, 255) / 255 * 100); }
  // Logaritmische schaal: positie 0..steps <-> waarde lo..hi.
  function logScale(lo, hi, steps) {
    var a = Math.log(lo), b = Math.log(hi);
    return {
      steps: steps,
      toValue: function (pos) { return niceRound(Math.exp(a + (b - a) * clamp(pos, 0, steps) / steps), 2); },
      toPos: function (val) { return Math.round(clamp((Math.log(clamp(val, lo, hi)) - a) / (b - a), 0, 1) * steps); }
    };
  }

  // ---- Batterij ----
  // Codes van PSMove_Battery_Level (zes niveaus plus laden en vol aan de lader); nooit een verzonnen percentage.
  var BATT_LEVELS = {
    min: { seg: 1, level: 'low', label: 'Bijna leeg', detail: 'minder dan 20%' },
    ge20: { seg: 2, level: 'mid', label: 'Laag', detail: 'minstens 20%' },
    ge40: { seg: 3, level: 'ok', label: 'Halfvol', detail: 'minstens 40%' },
    ge60: { seg: 4, level: 'ok', label: 'Goed', detail: 'minstens 60%' },
    ge80: { seg: 5, level: 'ok', label: 'Bijna vol', detail: 'minstens 80%' },
    max: { seg: 5, level: 'ok', label: 'Vol', detail: '100%' }
  };
  // percent: optioneel getal 0..100 (Navigation Controller); dan staat het percentage in de tekst.
  function batteryView(code, connected, percent) {
    if (!connected) return { segments: 0, level: 'none', label: 'Onbekend', detail: 'niet verbonden', charging: false };
    if (code === 'charging') return { segments: 5, level: 'charging', label: 'Laadt', detail: '', charging: true };
    if (code === 'charged') return { segments: 5, level: 'full', label: 'Vol', detail: 'aan de lader', charging: false };
    var b = BATT_LEVELS[code];
    if (!b) return { segments: 0, level: 'none', label: 'Onbekend', detail: '', charging: false };
    var detail = typeof percent === 'number' && percent >= 0 ? percent + '%' : b.detail;
    return { segments: b.seg, level: b.level, label: b.label, detail: detail, charging: false };
  }

  // ---- Schuifjes: debounce met bevestiging ----
  // input(v): tijdens het slepen; hoogstens een verzoek per `delay` ms en altijd de laatste waarde.
  // commit(v): na loslaten; meteen versturen. Er loopt nooit meer dan een verzoek tegelijk. onDone(resp, v)
  // volgt pas als er geen nieuwere waarde meer wacht, zodat de bevestiging de werkelijke waarde uit de daemon toont.
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

  // ---- Knoppenindeling ----
  var BUTTONS = [
    { id: 'move', label: 'Move-knop (grote knop voorop)', group: 'move' },
    { id: 'cross', label: 'Kruis', group: 'move' },
    { id: 'circle', label: 'Cirkel', group: 'move' },
    { id: 'square', label: 'Vierkant', group: 'move' },
    { id: 'triangle', label: 'Driehoek', group: 'move' },
    { id: 'select', label: 'Select', group: 'move' },
    { id: 'start', label: 'Start', group: 'move' },
    { id: 'pump', label: 'Pomp', group: 'sharp' },
    { id: 'ss_rl', label: 'RL-knop', group: 'sharp' },
    { id: 'ss_fire', label: 'Vuurknop', group: 'sharp' },
    { id: 'sel1', label: 'Vuurstand 1', group: 'sharp' },
    { id: 'sel2', label: 'Vuurstand 2', group: 'sharp' },
    { id: 'sel3', label: 'Vuurstand 3', group: 'sharp' },
    { id: 'nav_l1', label: 'L1', group: 'nav' },
    { id: 'nav_l2', label: 'L2', group: 'nav' },
    { id: 'nav_cross', label: 'Kruis', group: 'nav' },
    { id: 'nav_circle', label: 'Cirkel', group: 'nav' },
    { id: 'nav_ps', label: 'PS-knop', group: 'nav' },
    { id: 'nav_l3', label: 'Stick indrukken (L3)', group: 'nav' },
    { id: 'nav_up', label: 'Pijltje omhoog', group: 'nav' },
    { id: 'nav_down', label: 'Pijltje omlaag', group: 'nav' },
    { id: 'nav_left', label: 'Pijltje links', group: 'nav' },
    { id: 'nav_right', label: 'Pijltje rechts', group: 'nav' },
    { id: 'nav_stick_up', label: 'Stick omhoog', group: 'nav' },
    { id: 'nav_stick_down', label: 'Stick omlaag', group: 'nav' },
    { id: 'nav_stick_left', label: 'Stick naar links', group: 'nav' },
    { id: 'nav_stick_right', label: 'Stick naar rechts', group: 'nav' }
  ];
  var KEY_LABELS = {
    '5': 'Coin (toets 5)', '1': 'Start speler 1 (toets 1)', '2': 'Start speler 2 (toets 2)', f2: 'Test (toets F2)', f3: 'Service (toets F3)',
    esc: 'Esc', enter: 'Enter', space: 'Spatiebalk', tab: 'Tab', backspace: 'Backspace',
    up: 'Pijl omhoog', down: 'Pijl omlaag', left: 'Pijl links', right: 'Pijl rechts',
    lshift: 'Linker Shift', lctrl: 'Linker Ctrl', lalt: 'Linker Alt'
  };
  var MAIN_KEYS = ['5', '1', '2', 'f2', 'f3'];
  var OTHER_KEYS = ['esc', 'enter', 'space', 'tab', 'backspace', 'up', 'down', 'left', 'right', 'lshift', 'lctrl', 'lalt',
    'f1', 'f4', 'f5', 'f6', 'f7', 'f8', 'f9', 'f10', 'f11', 'f12', '0', '3', '4', '6', '7', '8', '9',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'];
  function keyLabel(k) {
    if (KEY_LABELS[k]) return KEY_LABELS[k];
    return 'Toets ' + k.toUpperCase();
  }
  // Keuzelijst: groepen met opties { value, label }.
  function actionGroups() {
    var g = [
      { label: 'Schieten en spelen', options: [
        { value: 'none', label: 'Niets doen' }, { value: 'reload', label: 'Herladen' }, { value: 'grenade', label: 'Granaat' },
        { value: 'aux1', label: 'Extra knop 1' }, { value: 'aux2', label: 'Extra knop 2' }, { value: 'recenter', label: 'Centreren' }] },
      { label: 'Spelmenu (toetsenbord)', options: MAIN_KEYS.map(function (k) { return { value: 'key:' + k, label: keyLabel(k) }; }) },
      { label: 'Overige toetsen', options: OTHER_KEYS.map(function (k) { return { value: 'key:' + k, label: keyLabel(k) }; }) }
    ];
    return g;
  }
  function actionLabel(a) {
    var gs = actionGroups();
    for (var i = 0; i < gs.length; i++)
      for (var j = 0; j < gs[i].options.length; j++) if (gs[i].options[j].value === a) return gs[i].options[j].label;
    return a;
  }
  // Alleen de knoppen waarvan de actie verschilt van de basis.
  function mapChanges(base, edit) {
    var out = {}, n = 0;
    BUTTONS.forEach(function (b) {
      var e = edit[b.id], a = base[b.id];
      if (e !== undefined && e !== (a === undefined ? 'none' : a)) { out[b.id] = e; n++; }
    });
    return { changes: out, count: n };
  }
  // Eenvoudige tekst voor "Move-knop -> functie".
  function mapSummary(map) {
    return BUTTONS.map(function (b) { return b.label + ' -> ' + actionLabel(map[b.id] || 'none'); });
  }
  // Welke knoppen zijn nu ingedrukt? (namen uit sample.buttons.down); T geldt als trekker.
  function pressedSet(down, trigger) {
    var s = {};
    (down || []).forEach(function (n) { s[n] = true; });
    if (trigger >= 140) s.t = true;
    return s;
  }

  // ---- Driftcorrectie ----
  var FUSION_CHOICES = [
    { id: 'vqf_mag', label: 'Kompas (aanbevolen)', help: 'Gebruikt het kompas in de Move om te voorkomen dat het richtpunt langzaam wegdraait.' },
    { id: 'vqf', label: 'Zonder kompas (nieuwe methode)', help: 'Voor als het kompas last heeft van metaal of magneten in de buurt.' },
    { id: 'mahony_mag', label: 'Kompas (oude methode)', help: 'De eerdere manier met kompas. Alleen nuttig om te vergelijken.' },
    { id: 'mahony', label: 'Alleen gyro', help: 'Geen kompas: het richtpunt kan langzaam wegdrijven. Centreer dan vaker.' }
  ];
  function fusionLabel(id) {
    for (var i = 0; i < FUSION_CHOICES.length; i++) if (FUSION_CHOICES[i].id === id) return FUSION_CHOICES[i].label;
    return id || '-';
  }

  // ---- Uitvoerklok ----
  function clockChoices(current) {
    var base = [0, 125, 250, 500];
    var list = base.slice();
    if (typeof current === 'number' && isFinite(current) && list.indexOf(current) < 0) { list.push(current); list.sort(function (a, b) { return a - b; }); }
    return list.map(function (hz) { return { hz: hz, label: hz === 0 ? 'Uit' : hz + ' Hz' }; });
  }

  // ---- Games ----
  // status: { state, text, profile, message }. Wat de interface per toestand toont en toestaat.
  function gameStateView(st) {
    st = st || {};
    var busy = st.state === 'starting' || st.state === 'waiting_move' || st.state === 'stopping';
    var tone = st.state === 'running' ? 'ok' : st.state === 'failed' ? 'bad' : busy ? 'warn' : 'info';
    var text = st.text || 'Onbekend';
    if (st.state === 'failed' && st.message) text += ' ' + st.message;
    return {
      text: text, tone: tone, busy: busy,
      canStart: st.state === 'idle' || st.state === 'failed' || st.state === undefined,
      canStop: st.state === 'running' || st.state === 'starting' || st.state === 'waiting_move' || !!st.stoppable
    };
  }
  // Voor een kaart van een game: staat deze game centraal in de huidige toestand?
  function gameCardState(st, profile) {
    var cur = st && st.profile === profile;
    var state = (st && st.state) || 'idle';
    return {
      active: !!cur && (state === 'running' || state === 'starting' || state === 'waiting_move' || state === 'stopping'),
      failed: !!cur && state === 'failed'
    };
  }

  // ---- Samenvatting in gewone taal (Overzicht) ----
  function plainSettings(status) {
    var cfg = (status && status.config) || {};
    var f = cfg.filter || {}, a = cfg.aim || {}, fu = cfg.fusion || {};
    var rum = (status && status.controller && status.controller.rumble) || {};
    var clk = (status && status.output && status.output.live && status.output.live.clock) || {};
    var lines = [];
    lines.push(['Cursor gladstrijken', f.enabled ? 'aan' : 'uit']);
    lines.push(['Driftcorrectie', fusionLabel(fu.mode)]);
    lines.push(['Vooruitkijken', typeof a.predict_ms === 'number' ? fmtNl(a.predict_ms, 0) + ' ms' : '-']);
    lines.push(['Trillen', rum.enabled ? 'aan, sterkte ' + percent255(rum.strength) + '%' : 'uit']);
    if (status && status.yaw_trim) lines.push(['Leren van centreren', status.yaw_trim.learning ? 'aan' : 'uit']);
    if (typeof clk.enabled === 'boolean') lines.push(['Uitvoerklok', clk.enabled ? clk.hz + ' Hz' : 'uit']);
    return lines;
  }

  // ---- Spelers en controllers ----
  var MAX_PLAYERS = 4;
  // Het onthouden spelernummer (1..4), anders 1.
  function pickPlayer(stored) {
    var n = Number(stored);
    return n >= 1 && n <= MAX_PLAYERS && Math.floor(n) === n ? n : 1;
  }
  // Keuzes voor de spelerbalk uit status.assignment.players: [{player, controller: naam|null}]. Speler 1 staat er altijd;
  // spelers zonder Move alleen als ze gekozen zijn (dan ziet de gebruiker dat er geen Move is).
  function playerOptions(players, current) {
    var out = [];
    (players || []).forEach(function (p) {
      if (p.player === 1 || p.controller || p.player === current)
        out.push({ player: p.player, label: 'Speler ' + p.player, sub: p.controller || 'geen Move' });
    });
    if (!out.length) out.push({ player: 1, label: 'Speler 1', sub: 'geen Move' });
    return out;
  }
  // Voegt de gekozen speler toe aan een verzoek (een kopie; het origineel blijft ongewijzigd).
  function withPlayer(body, player) {
    var o = {};
    Object.keys(body || {}).forEach(function (k) { o[k] = body[k]; });
    o.player = pickPlayer(player);
    return o;
  }
  function controllerTypeLabel(type) { return type === 'navigation' ? 'Navigation Controller' : 'Move'; }
  // "Laatst gezien" in gewone taal; nowS en unixS in seconden.
  function lastSeenText(unixS, nowS) {
    if (typeof unixS !== 'number' || !(unixS > 0)) return 'nog nooit';
    var d = Math.max(0, Math.round(nowS - unixS));
    if (d < 90) return 'zojuist';
    if (d < 3600) return Math.round(d / 60) + ' minuten geleden';
    if (d < 2 * 3600) return 'een uur geleden';
    if (d < 86400) return Math.round(d / 3600) + ' uur geleden';
    if (d < 2 * 86400) return 'gisteren';
    return Math.round(d / 86400) + ' dagen geleden';
  }
  // Batterij van een controller uit de lijst (/api/controllers): dezelfde weergave als bij de Move.
  function controllerBattery(c) {
    var b = (c && c.battery) || {};
    return batteryView(b.code, !!(c && c.connected && b.known), typeof b.percent === 'number' ? b.percent : undefined);
  }
  // De koppelwizard: stap (0 kabel erin, 1 koppelen, 2 kabel eruit, 3 PS-knop, 4 klaar), wat er te zien en te doen is.
  var PAIR_STEPS = ['Kabel erin', 'Koppelen', 'Kabel eruit', 'PS-knop'];
  function pairView(p) {
    p = p || {};
    var st = p.state || 'idle';
    var v = { state: st, step: 0, tone: 'info', busy: !!p.busy, active: !!p.active, canStart: false, startLabel: 'Nieuwe controller koppelen',
      canConfirm: false, confirmLabel: '', canCancel: false, showInstall: false, text: p.instruction || '', message: p.message || '' };
    switch (st) {
      case 'wait_usb': v.step = 0; v.canCancel = true; break;
      case 'already_paired': v.step = 1; v.canConfirm = true; v.confirmLabel = 'Opnieuw koppelen'; v.canCancel = true; v.tone = 'warn'; break;
      case 'set_master': case 'register': v.step = 1; v.busy = true; v.canCancel = false; break;
      case 'need_helper': v.step = 1; v.showInstall = true; v.canConfirm = true; v.confirmLabel = 'Opnieuw proberen'; v.canCancel = true; v.tone = 'warn'; break;
      case 'wait_unplug': v.step = 2; v.canCancel = true; break;
      case 'wait_bluetooth': v.step = 3; v.canCancel = true; break;
      case 'done': v.step = 4; v.tone = 'ok'; v.canStart = true; v.startLabel = 'Nog een controller koppelen'; break;
      case 'failed': v.step = 0; v.tone = 'bad'; v.canStart = true; v.startLabel = 'Opnieuw beginnen'; break;
      default: v.step = 0; v.canStart = true;  // idle, cancelled
    }
    if (st === 'cancelled' || st === 'idle') v.text = 'Sluit een nieuwe Move of Navigation Controller met een USB-kabel aan en druk op Nieuwe controller koppelen.';
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

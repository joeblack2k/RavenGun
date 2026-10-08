'use strict';
// RavenGun kalibratie-UI: pure logica zonder DOM (knop-debounce, tempo, stilstand, bolweergave, kwaliteitsoordeel).
// Draait in de browser (window.RGCal) en onder node (module.exports) voor de unittests (tools/test-js.sh).
(function (root, factory) {
  var api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.RGCal = api;
})(typeof self !== 'undefined' ? self : this, function () {

  // Bitposities van de Move-knoppen (PSMoveAPI, zie src/ravengun/core/buttons.hpp).
  var BIT = { triangle: 4, circle: 5, cross: 6, square: 7, select: 8, start: 11, ps: 16, move: 19, t: 20 };
  var BIT_NAME = {};
  Object.keys(BIT).forEach(function (n) { BIT_NAME[BIT[n]] = n; });

  // Nederlandse namen en symbolen voor de weergave.
  var LABEL = {
    triangle: 'Driehoek', circle: 'Cirkel', cross: 'Kruis', square: 'Vierkant', select: 'Select', start: 'Start',
    ps: 'PS', move: 'Move-knop', t: 'Trekker'
  };

  // ---- Knoppen: flanken uit cumulatieve tellers, met debounce ----

  // Bits waarvan de teller sinds de vorige stand is opgelopen. De daemon stuurt cumulatieve tellers
  // (Snapshot::press_total), dus een druk tussen twee SSE-frames gaat niet verloren.
  function pressedBits(prev, cur) {
    var out = [];
    for (var b = 0; b < 32; b++) {
      if ((cur[b] | 0) > (prev[b] | 0)) out.push(b);
    }
    return out;
  }

  // Welke actie hoort bij deze druk? Vaste voorrang: afbreken, terug, overslaan, volgende.
  function actionFor(bits) {
    function has(b) { return bits.indexOf(b) >= 0; }
    if (has(BIT.start)) return 'abort';
    if (has(BIT.circle)) return 'back';
    if (has(BIT.select)) return 'skip';
    if (has(BIT.t) || has(BIT.move)) return 'next';
    return null;
  }

  // Eén druk = één actie: na een geaccepteerde actie worden alle drukken binnen lockoutMs genegeerd
  // (contactdender, dubbele druk). De eerste stand na (her)verbinden is alleen een basislijn: drukken
  // die vóór het verbinden gebeurden, of tijdens een onderbreking, worden nooit alsnog uitgevoerd.
  function ButtonNav(opts) {
    opts = opts || {};
    this.lockoutMs = opts.lockoutMs === undefined ? 350 : opts.lockoutMs;
    this.prev = null;
    this.until = -Infinity;
  }
  ButtonNav.prototype.reset = function () { this.prev = null; };
  // allowed: object {next:true, ...}; een niet-toegestane actie wordt genegeerd en start geen lockout.
  ButtonNav.prototype.feed = function (press, nowMs, allowed) {
    if (!this.prev) { this.prev = press.slice(); return null; }
    var bits = pressedBits(this.prev, press);
    this.prev = press.slice();
    if (!bits.length) return null;
    if (nowMs < this.until) return null;
    var act = actionFor(bits);
    if (!act || (allowed && !allowed[act])) return null;
    this.until = nowMs + this.lockoutMs;
    return act;
  };

  // Lang indrukken: vuurt één keer zodra een knop holdMs achtereen is ingedrukt; opnieuw pas na loslaten.
  function HoldTracker(holdMs) {
    this.holdMs = holdMs || 1500;
    this.since = {};
    this.fired = {};
  }
  HoldTracker.prototype.reset = function () { this.since = {}; this.fired = {}; };
  // mask: buttons.mask; bits: lijst bitnummers die gevolgd worden. Resultaat: {fired: bit|null, progress: {bit: 0..1}}
  HoldTracker.prototype.update = function (mask, nowMs, bits) {
    var res = { fired: null, progress: {} };
    for (var i = 0; i < bits.length; i++) {
      var b = bits[i];
      if ((mask >>> b) & 1) {
        if (this.since[b] === undefined) this.since[b] = nowMs;
        var p = Math.min(1, (nowMs - this.since[b]) / this.holdMs);
        res.progress[b] = p;
        if (p >= 1 && !this.fired[b]) { this.fired[b] = true; if (res.fired === null) res.fired = b; }
      } else {
        delete this.since[b];
        delete this.fired[b];
        res.progress[b] = 0;
      }
    }
    return res;
  };

  // Mapping-meting: verzamelt de drukken kort na elkaar en beslist of het precies één knop was.
  function MappingCollector(windowMs) {
    this.windowMs = windowMs || 200;
    this.t0 = null;
    this.bits = [];
  }
  MappingCollector.prototype.reset = function () { this.t0 = null; this.bits = []; };
  MappingCollector.prototype.add = function (bits, nowMs) {
    if (!bits.length) return;
    if (this.t0 === null) this.t0 = nowMs;
    for (var i = 0; i < bits.length; i++) if (this.bits.indexOf(bits[i]) < 0) this.bits.push(bits[i]);
  };
  // null zolang er niets (of nog gemeten wordt); daarna {status:'single', name} of {status:'multi', names}
  MappingCollector.prototype.poll = function (nowMs) {
    if (this.t0 === null || nowMs - this.t0 < this.windowMs) return null;
    var names = this.bits.map(function (b) { return BIT_NAME[b]; }).filter(Boolean);
    this.reset();
    if (names.length === 1) return { status: 'single', name: names[0] };
    if (names.length === 0) return null;
    return { status: 'multi', names: names };
  };

  // ---- Tempo en stilstand ----

  function gyroDps(sample) {
    var g = sample && sample.source && sample.source.gyro_deg_s;
    var b = sample && sample.orientation && sample.orientation.gyro_bias_deg_s;
    if (!g) return null;
    var x = g[0] - (b ? b[0] : 0), y = g[1] - (b ? b[1] : 0), z = g[2] - (b ? b[2] : 0);
    return Math.sqrt(x * x + y * y + z * z);
  }

  // Exponentieel gemiddelde met tijdconstante tauMs (onafhankelijk van de framesnelheid).
  function Ema(tauMs) { this.tau = tauMs; this.v = null; this.t = null; }
  Ema.prototype.reset = function () { this.v = null; this.t = null; };
  Ema.prototype.add = function (x, nowMs) {
    if (this.v === null) { this.v = x; this.t = nowMs; return x; }
    var dt = Math.max(0, nowMs - this.t);
    var a = 1 - Math.exp(-dt / this.tau);
    this.v += a * (x - this.v);
    this.t = nowMs;
    return this.v;
  };

  // Tempo bij magnetometer-draaien (graden/s). Grenzen: onder 8 staat het geweer praktisch stil, boven 120
  // verstoort versnelling de dipcontrole en smeert de meting.
  var TEMPO = { still: 8, good_max: 120, fast_max: 220 };
  function classifyTempo(dps) {
    if (dps === null || !isFinite(dps)) return { level: 'onbekend', ok: false, text: 'geen gyro' };
    if (dps < TEMPO.still) return { level: 'stil', ok: false, text: 'Draai verder' };
    if (dps <= TEMPO.good_max) return { level: 'goed', ok: true, text: 'Goed tempo' };
    if (dps <= TEMPO.fast_max) return { level: 'snel', ok: false, text: 'Iets rustiger' };
    return { level: 'te_snel', ok: false, text: 'Veel te snel' };
  }
  // Positie (0..1) op de tempo-balk: 0 stil, 1 = fast_max of meer.
  function tempoPosition(dps) {
    if (dps === null || !isFinite(dps)) return 0;
    return Math.max(0, Math.min(1, dps / TEMPO.fast_max));
  }

  // Stilstandmeting tijdens het aftellen. De eerste graceMs (trekkerdruk, uitzwaaien) tellen niet mee.
  function StillTracker(opts) {
    opts = opts || {};
    this.graceMs = opts.graceMs === undefined ? 1000 : opts.graceMs;
    this.thr = opts.thr === undefined ? 5 : opts.thr;            // graden/s: hieronder telt een sample als stil
    this.spike = opts.spike === undefined ? 20 : opts.spike;     // graden/s: één uitschieter hierboven keurt af
    this.minFraction = opts.minFraction === undefined ? 0.85 : opts.minFraction;
    this.t0 = null;
    this.n = 0; this.still = 0; this.max = 0; this.sum = 0;
  }
  StillTracker.prototype.start = function (nowMs) { this.t0 = nowMs; this.n = 0; this.still = 0; this.max = 0; this.sum = 0; };
  StillTracker.prototype.isStill = function (dps) { return dps !== null && dps < this.thr; };
  StillTracker.prototype.add = function (dps, nowMs) {
    if (this.t0 === null || dps === null || nowMs - this.t0 < this.graceMs) return;
    this.n++; this.sum += dps;
    if (dps < this.thr) this.still++;
    if (dps > this.max) this.max = dps;
  };
  StillTracker.prototype.result = function () {
    var frac = this.n ? this.still / this.n : 0;
    var ok = this.n >= 10 && frac >= this.minFraction && this.max <= this.spike;
    var why = '';
    if (this.n < 10) why = 'te weinig gyro-data';
    else if (this.max > this.spike) why = 'schok of zwaai (' + Math.round(this.max) + ' graden/s)';
    else if (frac < this.minFraction) why = 'te onrustig (' + Math.round(frac * 100) + ' % stil)';
    return { ok: ok, fraction: frac, max_dps: this.max, mean_dps: this.n ? this.sum / this.n : 0, samples: this.n, why: why };
  };

  // Volgorde van de stilstandmetingen en de labels voor de opname (record mark).
  // Label begin = base[_pogingN]; einde = begin + (_einde of _onrustig).
  function stillPlan() {
    var plan = [];
    for (var i = 1; i <= 5; i++) plan.push({ key: 'stil_midden_' + i, where: 'midden', n: i, of: 5 });
    plan.push({ key: 'stil_links_1', where: 'links', n: 1, of: 1 });
    plan.push({ key: 'stil_rechts_1', where: 'rechts', n: 1, of: 1 });
    return plan;
  }
  function stillLabel(key, attempt, end) {
    var l = key + (attempt > 1 ? '_poging' + attempt : '');
    if (end === 'ok') l += '_einde';
    else if (end === 'fail') l += '_onrustig';
    return l;
  }

  // ---- Bolweergave (dekking per richting) ----

  // Dezelfde 20 richtingen en volgorde als SphereCoverage::directions() in de daemon.
  var DIRS = (function () {
    var p = (1 + Math.sqrt(5)) / 2, ip = 1 / p, r = [], s1, s2, sx, sy, sz;
    var sg = [-1, 1];
    for (var a = 0; a < 2; a++) for (var b = 0; b < 2; b++) for (var c = 0; c < 2; c++) r.push([sg[a], sg[b], sg[c]]);
    for (a = 0; a < 2; a++) for (b = 0; b < 2; b++) {
      s1 = sg[a]; s2 = sg[b];
      r.push([0, s1 * ip, s2 * p]);
      r.push([s1 * ip, s2 * p, 0]);
      r.push([s1 * p, 0, s2 * ip]);
    }
    return r.map(function (v) { var n = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); return [v[0] / n, v[1] / n, v[2] / n]; });
  })();

  // Draait v eerst om de y-as (yaw), dan om de x-as (tilt).
  function rotate(v, yaw, tilt) {
    var cy = Math.cos(yaw), sy = Math.sin(yaw), ct = Math.cos(tilt), st = Math.sin(tilt);
    var x = v[0] * cy + v[2] * sy, z = -v[0] * sy + v[2] * cy, y = v[1];
    return [x, y * ct - z * st, y * st + z * ct];
  }
  function unrotate(v, yaw, tilt) {
    var ct = Math.cos(-tilt), st = Math.sin(-tilt);
    var y = v[1] * ct - v[2] * st, z = v[1] * st + v[2] * ct, x = v[0];
    var cy = Math.cos(-yaw), sy = Math.sin(-yaw);
    return [x * cy + z * sy, y, -x * sy + z * cy];
  }
  function nearestCell(v) {
    var best = 0, bd = -2, second = -2;
    for (var i = 0; i < DIRS.length; i++) {
      var d = DIRS[i][0] * v[0] + DIRS[i][1] * v[1] + DIRS[i][2] * v[2];
      if (d > bd) { second = bd; bd = d; best = i; } else if (d > second) second = d;
    }
    return { cell: best, margin: bd - second };
  }

  // Tekent de bol (orthografisch, kijkrichting -z) als Voronoi-vlakken in een RGBA-buffer van size x size.
  // cells: tellingen per richting; minCount: vanaf hoeveel samples een vlak "gevuld" is.
  function renderSphere(buf, size, cells, minCount, yaw, tilt) {
    var half = size / 2, rad = half - 2;
    for (var py = 0; py < size; py++) {
      for (var px = 0; px < size; px++) {
        var x = (px + 0.5 - half) / rad, y = -(py + 0.5 - half) / rad, i = (py * size + px) * 4;
        var rr = x * x + y * y;
        if (rr > 1) { buf[i] = 0; buf[i + 1] = 0; buf[i + 2] = 0; buf[i + 3] = 0; continue; }
        var z = Math.sqrt(1 - rr);
        var n = nearestCell(unrotate([x, y, z], yaw, tilt));
        var cnt = cells[n.cell] | 0;
        var r, g, b;
        if (cnt >= minCount) { r = 61; g = 220; b = 151; }          // gedekt
        else if (cnt > 0) { r = 255; g = 180; b = 84; }            // begonnen
        else { r = 38; g = 46; b = 64; }                           // leeg
        var shade = 0.45 + 0.55 * z;                                // eenvoudige belichting
        if (n.margin < 0.035) { shade *= 0.35; }                    // celrand
        buf[i] = r * shade; buf[i + 1] = g * shade; buf[i + 2] = b * shade; buf[i + 3] = 255;
      }
    }
  }

  // Positie van een meetrichting op de bol: {x,y} in -1..1 (y omhoog) en z (>0 = voorkant).
  function projectDir(dir, yaw, tilt) {
    var v = rotate(dir, yaw, tilt);
    return { x: v[0], y: v[1], z: v[2] };
  }

  // ---- Voortgang en uitkomst van de magnetometerkalibratie ----

  // Eén percentage voor de tv: de zwakste van dekking, tijd en samples.
  function magProgress(m) {
    if (!m || !m.active) return { pct: 0, cov: 0, time: 0, samples: 0 };
    var cov = Math.min(1, (m.coverage_pct || 0) / (m.target_coverage_pct || 80));
    var time = Math.min(1, (m.elapsed_s || 0) / (m.min_seconds || 40));
    var smp = Math.min(1, (m.samples || 0) / (m.target_samples || 300));
    return { pct: Math.floor(Math.min(cov, time, smp) * 100), cov: cov, time: time, samples: smp };
  }

  // Uitkomst van calib_mag stop -> oordeel voor de tv. Gebruikt de 'problems' van de daemon letterlijk.
  function fitVerdict(r) {
    if (!r || r.aborted) return { level: 'fail', headline: 'Afgebroken', tips: [(r && r.aborted) ? 'Reden: ' + r.aborted : 'Geen resultaat'] };
    var tips = (r.problems || []).slice();
    if (r.ok && r.saved) return { level: 'ok', headline: 'Geslaagd', tips: tips };
    if (r.marginal && r.saved) {
      return { level: 'marginal', headline: 'Marginaal, wel opgeslagen', tips: tips.length ? tips : ['De kalibratie is bruikbaar maar niet scherp; herhaal hem als je de tijd hebt.'] };
    }
    if (!tips.length) tips.push('De fit is niet goed genoeg om op te slaan. Probeer het opnieuw.');
    if (r.save_error) tips.push('Opslaan mislukt: ' + r.save_error);
    return { level: 'fail', headline: 'Mislukt', tips: tips };
  }

  // Aanvullende, concrete handelingstip bij een probleemtekst van de daemon.
  function actionTip(problem) {
    var p = String(problem || '').toLowerCase();
    if (p.indexOf('dekking') >= 0) return 'Draai ruimer: ook ondersteboven, loop naar het plafond en naar de vloer, en rollen om de loop.';
    if (p.indexOf('homogeen') >= 0 || p.indexOf('verstoor') >= 0 || p.indexOf('veld') >= 0)
      return 'Ga verder weg van metaal, speakers en de tv (minstens 1 meter) en doe het opnieuw.';
    if (p.indexOf('samples') >= 0 || p.indexOf('gedraaid') >= 0) return 'Draai langer en rustig door; houd het geweer niet stil.';
    if (p.indexOf('dip') >= 0 || p.indexOf('versnelling') >= 0) return 'Draai langzamer en zonder te schudden.';
    return '';
  }

  // ---- Schermdoelen (docs/aim-fusion-design.md 4.4): 3x3, 10 % van de randen af ----
  function screenTargets(w, h) {
    var xs = [0.1, 0.5, 0.9], ys = [0.1, 0.5, 0.9], out = [], n = 1;
    for (var j = 0; j < 3; j++) for (var i = 0; i < 3; i++) out.push({ n: n++, x: Math.round(xs[i] * w), y: Math.round(ys[j] * h), fx: xs[i], fy: ys[j] });
    return out;
  }

  // Schaal van het 1920x1080-podium naar het venster (gecontroleerde viewport, letterbox).
  function stageFit(winW, winH, stageW, stageH) {
    var s = Math.min(winW / stageW, winH / stageH);
    return { scale: s, left: (winW - stageW * s) / 2, top: (winH - stageH * s) / 2 };
  }

  // ---- Oefenen: vizier, doelen, treffers (stap "practice") ----

  // Aim uit een sample, genormaliseerd (0..1 = scherm). null als de richting ongeldig is.
  // x,y: gefilterd en unclamped; rx,ry: ruw (ongefilterd); cx,cy: gefilterd en geclampt.
  function aimView(sample) {
    var a = sample && sample.aim;
    if (!a || !a.valid || typeof a.x !== 'number' || typeof a.y !== 'number') return null;
    function n(v, d) { return typeof v === 'number' && isFinite(v) ? v : d; }
    if (!isFinite(a.x) || !isFinite(a.y)) return null;
    return {
      x: a.x, y: a.y,
      rx: n(a.x_raw, a.x), ry: n(a.y_raw, a.y),
      cx: n(a.x_clamped, Math.min(1, Math.max(0, a.x))), cy: n(a.y_clamped, Math.min(1, Math.max(0, a.y))),
      off: !!a.off_screen || a.x < 0 || a.x > 1 || a.y < 0 || a.y > 1
    };
  }

  // Pijl aan de rand voor een richting buiten het scherm (x,y genormaliseerd, unclamped).
  // null als het punt op het scherm ligt. margin in px: afstand van de pijlpunt tot de rand.
  // angle: richting vanaf het schermmidden naar het punt (radialen, 0 = rechts, pi/2 = omlaag).
  function offscreenArrow(x, y, w, h, margin) {
    if (x >= 0 && x <= 1 && y >= 0 && y <= 1) return null;
    var m = margin === undefined ? 60 : margin;
    var dx = (x - 0.5) * w, dy = (y - 0.5) * h;
    var px = Math.min(w - m, Math.max(m, x * w)), py = Math.min(h - m, Math.max(m, y * h));
    var over = Math.max(x < 0 ? -x : (x > 1 ? x - 1 : 0), y < 0 ? -y : (y > 1 ? y - 1 : 0));
    return { px: px, py: py, angle: Math.atan2(dy, dx), over: over };
  }

  // Interpolatie tussen SSE-frames voor een vloeiend vizier bij rAF-weergave. De weergave glijdt na elk
  // nieuw frame in precies één (gemeten) frame-interval van het vorige naar het nieuwe punt: één frame
  // vertraging (~17 ms bij 60 Hz), geen extrapolatie (geen doorschieten). Sprongen (bijv. recenter) en
  // een ongeldige richting zetten het punt meteen.
  function Interpolator(opts) {
    opts = opts || {};
    this.snapDist = opts.snapDist === undefined ? 0.25 : opts.snapDist;   // genormaliseerd
    this.minDt = opts.minDt || 8; this.maxDt = opts.maxDt || 80;
    this.dt = opts.dt || 33;
    this.prev = null; this.cur = null; this.tCur = 0;
  }
  Interpolator.prototype.push = function (tMs, p) {
    if (!p) { this.prev = this.cur = null; return; }
    if (this.cur) {
      var d = tMs - this.tCur;
      if (d > 0) {
        var c = Math.min(this.maxDt, Math.max(this.minDt, d));
        this.dt += (c - this.dt) * 0.2;
      }
      if (Math.hypot(p[0] - this.cur[0], p[1] - this.cur[1]) > this.snapDist) this.prev = p;
      else this.prev = this.cur;
    } else {
      this.prev = p;
    }
    this.cur = p;
    this.tCur = tMs;
  };
  Interpolator.prototype.at = function (tMs) {
    if (!this.cur) return null;
    var a = Math.min(1, Math.max(0, (tMs - this.tCur) / this.dt));
    return [this.prev[0] + (this.cur[0] - this.prev[0]) * a, this.prev[1] + (this.cur[1] - this.prev[1]) * a];
  };

  // Frames per seconde over een glijdend venster (SSE-frames die de pagina echt ontvangt).
  function RateMeter(windowMs) { this.win = windowMs || 1000; this.t = []; }
  RateMeter.prototype.add = function (tMs) { this.t.push(tMs); this.trim(tMs); };
  RateMeter.prototype.trim = function (tMs) { while (this.t.length && this.t[0] < tMs - this.win) this.t.shift(); };
  RateMeter.prototype.hz = function (tMs) {
    this.trim(tMs);
    if (this.t.length < 2) return 0;
    var span = this.t[this.t.length - 1] - this.t[0];
    return span > 0 ? (this.t.length - 1) * 1000 / span : 0;
  };
  RateMeter.prototype.reset = function () { this.t = []; };

  // Volgend doel: willekeurig, binnen een rand (fractie van het scherm; onderaan breder voor de knoppenbalk) en minstens minDist px van het vorige.
  // rnd: functie die 0..1 geeft (Math.random of een vaste reeks in de test).
  function nextTarget(rnd, prev, w, h, opts) {
    opts = opts || {};
    var r = opts.r || 60, edge = opts.edge === undefined ? 0.12 : opts.edge, minDist = opts.minDist || 400;
    var edgeBottom = opts.edgeBottom === undefined ? 0.18 : opts.edgeBottom;   // onder: ruimte voor de knoppenbalk
    var x = w / 2, y = h / 2;
    for (var i = 0; i < 30; i++) {
      x = Math.round(w * edge + rnd() * w * (1 - 2 * edge));
      y = Math.round(h * edge + rnd() * h * (1 - edge - edgeBottom));
      if (!prev || Math.hypot(x - prev.x, y - prev.y) >= minDist) break;
    }
    return { x: x, y: y, r: r };
  }

  // Een schot (px op het podium, unclamped) tegen het doel. Treffer als de afstand tot het doelmidden <= straal.
  function shotResult(sx, sy, target, w) {
    var d = Math.hypot(sx - target.x, sy - target.y);
    return { hit: d <= target.r, distPx: d, distPct: d / (w || 1920) * 100 };
  }

  // Treffers, missers en gemiddelde afstand van de inslag tot het doelmidden (alle schoten).
  function PracticeStats() { this.reset(); }
  PracticeStats.prototype.reset = function () { this.hits = 0; this.misses = 0; this.sumPx = 0; this.sumPct = 0; };
  PracticeStats.prototype.add = function (res) {
    if (res.hit) this.hits++; else this.misses++;
    this.sumPx += res.distPx; this.sumPct += res.distPct;
  };
  PracticeStats.prototype.summary = function () {
    var n = this.hits + this.misses;
    return { shots: n, hits: this.hits, misses: this.misses, avgPx: n ? this.sumPx / n : null, avgPct: n ? this.sumPct / n : null };
  };

  // Knoppen in de oefenstap: trekker = schieten, PS = midden zetten (dat doet de daemon zelf; hier alleen een
  // melding, zodat het nooit dubbel gebeurt), Cirkel = terug, Start = sluiten, Select = ruwe stip aan/uit.
  // De Move-knop doet in deze stap niets. Zelfde flankentellers en baseline als ButtonNav (drukken vóór het verbinden of
  // tijdens een onderbreking tellen nooit), met een eigen debounce per actie (schieten is korter dan de rest).
  function PracticeButtons(opts) {
    opts = opts || {};
    this.lock = { shoot: 120, centered: 350, back: 350, close: 350, toggle_raw: 350 };
    if (opts.lockoutMs) for (var k in opts.lockoutMs) this.lock[k] = opts.lockoutMs[k];
    this.prev = null; this.until = {};
  }
  PracticeButtons.prototype.reset = function () { this.prev = null; this.until = {}; };
  PracticeButtons.prototype.feed = function (press, nowMs) {
    if (!this.prev) { this.prev = press.slice(); return []; }
    var bits = pressedBits(this.prev, press);
    this.prev = press.slice();
    var map = [[BIT.start, 'close'], [BIT.circle, 'back'], [BIT.select, 'toggle_raw'], [BIT.ps, 'centered'], [BIT.t, 'shoot']];
    var out = [];
    for (var i = 0; i < map.length; i++) {
      var act = map[i][1];
      if (bits.indexOf(map[i][0]) < 0) continue;
      if (nowMs < (this.until[act] === undefined ? -Infinity : this.until[act])) continue;
      this.until[act] = nowMs + this.lock[act];
      out.push(act);
    }
    return out;
  };

  // Statusregel: aim-modus en middenbron uit het sample.
  function aimModeText(sample) {
    var m = sample && sample.tracking_mode;
    var c = sample && sample.aim && sample.aim.center_source;
    var mt = m === 'orientation' ? 'oriëntatie (voorlopig)' : (m === 'hybrid' ? 'hybride' : (m === 'geometric_ray' ? 'geometrische straal' :
             (m === 'optical_diagnostic' ? 'optisch (diagnose)' : (m ? String(m) : 'onbekend'))));
    var ct = c === 'auto_initial' ? 'auto-midden' : (c === 'user' ? 'midden door jou gezet' : 'nog geen midden');
    return mt + ', ' + ct;
  }

  return {
    BIT: BIT, BIT_NAME: BIT_NAME, LABEL: LABEL, DIRS: DIRS, TEMPO: TEMPO,
    pressedBits: pressedBits, actionFor: actionFor, ButtonNav: ButtonNav, HoldTracker: HoldTracker,
    MappingCollector: MappingCollector, gyroDps: gyroDps, Ema: Ema, classifyTempo: classifyTempo,
    tempoPosition: tempoPosition, StillTracker: StillTracker, stillPlan: stillPlan, stillLabel: stillLabel,
    rotate: rotate, unrotate: unrotate, nearestCell: nearestCell, renderSphere: renderSphere, projectDir: projectDir,
    magProgress: magProgress, fitVerdict: fitVerdict, actionTip: actionTip, screenTargets: screenTargets, stageFit: stageFit,
    aimView: aimView, offscreenArrow: offscreenArrow, Interpolator: Interpolator, RateMeter: RateMeter,
    nextTarget: nextTarget, shotResult: shotResult, PracticeStats: PracticeStats, PracticeButtons: PracticeButtons,
    aimModeText: aimModeText
  };
});

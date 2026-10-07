'use strict';
// Unittests van src/ravengund/web/calibrate-core.js. Draait via tools/test-js.sh (node in een wegwerp-container);
// het script zet RGCal (require van calibrate-core.js) vooraf.
const assert = require('assert');
const C = RGCal;
let n = 0;
function t(name, fn) {
  try { fn(); } catch (e) { console.log('MISLUKT: ' + name); throw e; }
  n++;
}

function counts(over) { const a = new Array(32).fill(0); Object.keys(over || {}).forEach(k => { a[k] = over[k]; }); return a; }

t('pressedBits ziet alleen opgelopen tellers', () => {
  assert.deepStrictEqual(C.pressedBits(counts({ 20: 1 }), counts({ 20: 2, 19: 1 })), [19, 20]);
  assert.deepStrictEqual(C.pressedBits(counts({ 20: 5 }), counts({ 20: 5 })), []);
  assert.deepStrictEqual(C.pressedBits(counts({ 20: 5 }), counts({ 20: 0 })), []); // daemon herstart: geen valse druk
});

t('actionFor: voorrang en toewijzing', () => {
  assert.strictEqual(C.actionFor([C.BIT.t]), 'next');
  assert.strictEqual(C.actionFor([C.BIT.move]), 'next');
  assert.strictEqual(C.actionFor([C.BIT.circle]), 'back');
  assert.strictEqual(C.actionFor([C.BIT.start]), 'abort');
  assert.strictEqual(C.actionFor([C.BIT.select]), 'skip');
  assert.strictEqual(C.actionFor([C.BIT.t, C.BIT.start]), 'abort');
  assert.strictEqual(C.actionFor([C.BIT.cross]), null);
  assert.strictEqual(C.actionFor([]), null);
});

t('ButtonNav: eerste stand is alleen een basislijn', () => {
  const nav = new C.ButtonNav({ lockoutMs: 350 });
  assert.strictEqual(nav.feed(counts({ 20: 7 }), 0, { next: 1 }), null);
  assert.strictEqual(nav.feed(counts({ 20: 7 }), 100, { next: 1 }), null);
  assert.strictEqual(nav.feed(counts({ 20: 8 }), 200, { next: 1 }), 'next');
});

t('ButtonNav: een druk is een actie (debounce)', () => {
  const nav = new C.ButtonNav({ lockoutMs: 350 });
  nav.feed(counts(), 0, { next: 1 });
  assert.strictEqual(nav.feed(counts({ 20: 1 }), 1000, { next: 1 }), 'next');
  // dender en dubbele druk binnen 350 ms: genegeerd
  assert.strictEqual(nav.feed(counts({ 20: 2 }), 1010, { next: 1 }), null);
  assert.strictEqual(nav.feed(counts({ 20: 3 }), 1300, { next: 1 }), null);
  // na de lockout telt een nieuwe druk weer
  assert.strictEqual(nav.feed(counts({ 20: 4 }), 1400, { next: 1 }), 'next');
});

t('ButtonNav: meerdere drukken tussen twee frames geven toch een actie', () => {
  const nav = new C.ButtonNav({ lockoutMs: 350 });
  nav.feed(counts(), 0, { next: 1 });
  assert.strictEqual(nav.feed(counts({ 20: 3 }), 500, { next: 1 }), 'next');
});

t('ButtonNav: niet-toegestane actie wordt genegeerd zonder lockout', () => {
  const nav = new C.ButtonNav({ lockoutMs: 350 });
  nav.feed(counts(), 0, { abort: 1 });
  assert.strictEqual(nav.feed(counts({ 20: 1 }), 100, { abort: 1 }), null);
  assert.strictEqual(nav.feed(counts({ 20: 1, 11: 1 }), 120, { abort: 1 }), 'abort');
});

t('ButtonNav: na reset geen drukken uit de onderbreking', () => {
  const nav = new C.ButtonNav({ lockoutMs: 350 });
  nav.feed(counts(), 0, { next: 1 });
  nav.reset();
  assert.strictEqual(nav.feed(counts({ 20: 9 }), 5000, { next: 1 }), null);
  assert.strictEqual(nav.feed(counts({ 20: 10 }), 5100, { next: 1 }), 'next');
});

t('HoldTracker: vuurt eenmaal na 1,5 s en pas opnieuw na loslaten', () => {
  const h = new C.HoldTracker(1500);
  const down = 1 << C.BIT.t;
  assert.strictEqual(h.update(down, 0, [C.BIT.t]).fired, null);
  assert.strictEqual(h.update(down, 1400, [C.BIT.t]).fired, null);
  assert.strictEqual(h.update(down, 1600, [C.BIT.t]).fired, C.BIT.t);
  assert.strictEqual(h.update(down, 2000, [C.BIT.t]).fired, null);
  h.update(0, 2100, [C.BIT.t]);
  assert.strictEqual(h.update(down, 2200, [C.BIT.t]).fired, null);
  assert.strictEqual(h.update(down, 3800, [C.BIT.t]).fired, C.BIT.t);
});

t('HoldTracker: korte druk vuurt niet; voortgang loopt op', () => {
  const h = new C.HoldTracker(1000);
  const down = 1 << C.BIT.circle;
  assert.strictEqual(h.update(down, 0, [C.BIT.circle]).progress[C.BIT.circle], 0);
  assert.strictEqual(h.update(down, 500, [C.BIT.circle]).progress[C.BIT.circle], 0.5);
  assert.strictEqual(h.update(0, 600, [C.BIT.circle]).fired, null);
});

t('MappingCollector: een knop, meerdere knoppen en niets', () => {
  const m = new C.MappingCollector(200);
  m.add([C.BIT.cross], 1000);
  assert.strictEqual(m.poll(1100), null);
  assert.deepStrictEqual(m.poll(1250), { status: 'single', name: 'cross' });
  m.add([C.BIT.cross], 2000);
  m.add([C.BIT.square], 2050);
  const r = m.poll(2300);
  assert.strictEqual(r.status, 'multi');
  assert.deepStrictEqual(r.names.sort(), ['cross', 'square']);
  assert.strictEqual(m.poll(5000), null);
});

t('gyroDps trekt de bias af', () => {
  const s = { source: { gyro_deg_s: [3, 4, 0] }, orientation: { gyro_bias_deg_s: [0, 0, 0] } };
  assert.strictEqual(C.gyroDps(s), 5);
  s.orientation.gyro_bias_deg_s = [3, 4, 0];
  assert.strictEqual(C.gyroDps(s), 0);
  assert.strictEqual(C.gyroDps({}), null);
});

t('classifyTempo: stil, goed, snel, te snel', () => {
  assert.strictEqual(C.classifyTempo(2).level, 'stil');
  assert.strictEqual(C.classifyTempo(40).level, 'goed');
  assert.strictEqual(C.classifyTempo(40).ok, true);
  assert.strictEqual(C.classifyTempo(150).level, 'snel');
  assert.strictEqual(C.classifyTempo(400).level, 'te_snel');
  assert.strictEqual(C.classifyTempo(null).level, 'onbekend');
  assert.strictEqual(C.tempoPosition(1000), 1);
});

t('Ema is tijdsafhankelijk', () => {
  const e = new C.Ema(100);
  assert.strictEqual(e.add(10, 0), 10);
  const v = e.add(0, 100);
  assert.ok(v > 3 && v < 4, 'verwacht ~3,68, kreeg ' + v);
});

t('StillTracker: stil geslaagd, schok en onrust afgekeurd, grace telt niet', () => {
  let s = new C.StillTracker({});
  s.start(0);
  for (let i = 0; i < 100; i++) s.add(i < 10 ? 30 : 1.5, i * 50);  // eerste 0,5 s onrustig (binnen de grace)
  let r = s.result();
  assert.ok(r.ok, r.why);
  s = new C.StillTracker({});
  s.start(0);
  for (let i = 0; i < 100; i++) s.add(i === 60 ? 40 : 1.5, i * 50);
  r = s.result();
  assert.ok(!r.ok && /schok/.test(r.why), r.why);
  s = new C.StillTracker({});
  s.start(0);
  for (let i = 0; i < 100; i++) s.add(i % 3 === 0 ? 8 : 1, i * 50);
  r = s.result();
  assert.ok(!r.ok && /onrustig/.test(r.why), r.why);
  s = new C.StillTracker({});
  s.start(0);
  s.add(1, 5000);
  assert.ok(!s.result().ok);
});

t('stillPlan en labels', () => {
  const p = C.stillPlan();
  assert.strictEqual(p.length, 7);
  assert.strictEqual(p[0].key, 'stil_midden_1');
  assert.strictEqual(p[4].key, 'stil_midden_5');
  assert.strictEqual(p[5].key, 'stil_links_1');
  assert.strictEqual(p[6].key, 'stil_rechts_1');
  assert.strictEqual(C.stillLabel('stil_midden_1', 1, null), 'stil_midden_1');
  assert.strictEqual(C.stillLabel('stil_midden_1', 1, 'ok'), 'stil_midden_1_einde');
  assert.strictEqual(C.stillLabel('stil_midden_1', 2, 'fail'), 'stil_midden_1_poging2_onrustig');
  p.forEach(x => assert.ok(/^[a-z0-9_]{1,80}$/.test(C.stillLabel(x.key, 3, 'fail'))));
});

t('DIRS: 20 eenheidsvectoren in de volgorde van de daemon', () => {
  assert.strictEqual(C.DIRS.length, 20);
  C.DIRS.forEach(v => assert.ok(Math.abs(Math.hypot(v[0], v[1], v[2]) - 1) < 1e-12));
  const s = 1 / Math.sqrt(3);
  assert.deepStrictEqual(C.DIRS[0].map(x => Math.round(x / s)), [-1, -1, -1]);
  assert.deepStrictEqual(C.DIRS[7].map(x => Math.round(x / s)), [1, 1, 1]);
  for (let i = 0; i < 20; i++) assert.strictEqual(C.nearestCell(C.DIRS[i]).cell, i);
});

t('rotate en unrotate zijn elkaars inverse', () => {
  const v = [0.3, -0.5, 0.8];
  const r = C.unrotate(C.rotate(v, 1.1, 0.4), 1.1, 0.4);
  v.forEach((x, i) => assert.ok(Math.abs(x - r[i]) < 1e-12));
});

t('renderSphere kleurt gedekte, begonnen en lege vlakken', () => {
  const size = 64, buf = new Uint8ClampedArray(size * size * 4);
  const cells = new Array(20).fill(0);
  cells[7] = 10; cells[3] = 1;
  C.renderSphere(buf, size, cells, 3, 0, 0);
  assert.strictEqual(buf[3], 0, 'hoek is transparant');
  let green = 0, amber = 0, empty = 0;
  for (let i = 0; i < size * size; i++) {
    const r = buf[i * 4], g = buf[i * 4 + 1], b = buf[i * 4 + 2], a = buf[i * 4 + 3];
    if (!a) continue;
    if (g > r * 1.5 && g > b) green++; else if (r > g && g > b * 1.4) amber++; else empty++;
  }
  assert.ok(green > 0 && amber > 0 && empty > green, `groen ${green} amber ${amber} leeg ${empty}`);
});

t('magProgress: de zwakste maat telt', () => {
  const m = { active: true, coverage_pct: 80, elapsed_s: 20, samples: 400, target_coverage_pct: 80, target_samples: 300, min_seconds: 40 };
  assert.strictEqual(C.magProgress(m).pct, 50);
  m.elapsed_s = 60; m.coverage_pct = 40;
  assert.strictEqual(C.magProgress(m).pct, 50);
  m.coverage_pct = 100; m.samples = 150;
  assert.strictEqual(C.magProgress(m).pct, 50);
  m.samples = 900;
  assert.strictEqual(C.magProgress(m).pct, 100);
  assert.strictEqual(C.magProgress({ active: false }).pct, 0);
});

t('fitVerdict: geslaagd, marginaal, mislukt', () => {
  assert.strictEqual(C.fitVerdict({ ok: true, saved: true, problems: [] }).level, 'ok');
  const m = C.fitVerdict({ ok: false, marginal: true, saved: true, problems: ['veld niet homogeen'] });
  assert.strictEqual(m.level, 'marginal');
  assert.deepStrictEqual(m.tips, ['veld niet homogeen']);
  const f = C.fitVerdict({ ok: false, marginal: false, saved: false, problems: ['te weinig dekking: 40 %'] });
  assert.strictEqual(f.level, 'fail');
  assert.strictEqual(f.tips[0], 'te weinig dekking: 40 %');
  assert.strictEqual(C.fitVerdict({ aborted: 'door gebruiker' }).level, 'fail');
  assert.ok(C.actionTip('te weinig dekking: 40 %').indexOf('ondersteboven') >= 0);
  assert.ok(C.actionTip('veld niet homogeen').indexOf('1 meter') >= 0);
});

t('screenTargets: 3x3 op 10, 50 en 90 procent van 1920x1080', () => {
  const g = C.screenTargets(1920, 1080);
  assert.strictEqual(g.length, 9);
  assert.deepStrictEqual([g[0].x, g[0].y], [192, 108]);
  assert.deepStrictEqual([g[4].x, g[4].y], [960, 540]);
  assert.deepStrictEqual([g[8].x, g[8].y], [1728, 972]);
});

t('stageFit: letterbox van het 16:9-podium', () => {
  assert.deepStrictEqual(C.stageFit(1920, 1080, 1920, 1080), { scale: 1, left: 0, top: 0 });
  const f = C.stageFit(1280, 1024, 1920, 1080);
  assert.ok(Math.abs(f.scale - 1280 / 1920) < 1e-12 && f.left === 0 && f.top > 0);
});

t('aimView: geldig, ongeldig en off-screen', () => {
  assert.strictEqual(C.aimView(null), null);
  assert.strictEqual(C.aimView({ aim: { valid: false, x: null, y: null } }), null);
  const v = C.aimView({ aim: { valid: true, x: 0.25, y: 0.75, x_raw: 0.3, y_raw: 0.7, x_clamped: 0.25, y_clamped: 0.75, off_screen: false } });
  assert.deepStrictEqual([v.x, v.y, v.rx, v.ry, v.off], [0.25, 0.75, 0.3, 0.7, false]);
  const o = C.aimView({ aim: { valid: true, x: 1.2, y: 0.5, x_raw: 1.25, y_raw: 0.5, x_clamped: 1, y_clamped: 0.5, off_screen: true } });
  assert.ok(o.off && o.cx === 1);
});

t('offscreenArrow: alleen buiten 0..1, aan de rand, richting klopt', () => {
  assert.strictEqual(C.offscreenArrow(0.5, 0.5, 1920, 1080), null);
  assert.strictEqual(C.offscreenArrow(0, 1, 1920, 1080), null);   // precies op de rand is nog op het scherm
  const r = C.offscreenArrow(1.3, 0.5, 1920, 1080, 60);
  assert.strictEqual(r.px, 1860); assert.strictEqual(r.py, 540);
  assert.ok(Math.abs(r.angle) < 1e-9 && Math.abs(r.over - 0.3) < 1e-9);
  const l = C.offscreenArrow(-0.2, 0.5, 1920, 1080, 60);
  assert.strictEqual(l.px, 60); assert.ok(Math.abs(Math.abs(l.angle) - Math.PI) < 1e-9);
  const u = C.offscreenArrow(0.5, -0.4, 1920, 1080, 60);
  assert.strictEqual(u.py, 60); assert.ok(Math.abs(u.angle + Math.PI / 2) < 1e-9);
  const d = C.offscreenArrow(0.5, 1.4, 1920, 1080, 60);
  assert.strictEqual(d.py, 1020); assert.ok(Math.abs(d.angle - Math.PI / 2) < 1e-9);
  const h = C.offscreenArrow(2, -1, 1920, 1080, 60);   // hoek: geklemd op beide assen
  assert.deepStrictEqual([h.px, h.py], [1860, 60]);
});

t('Interpolator: glijdt in één interval, geen doorschieten, snapt bij sprong en ongeldig', () => {
  const ip = new C.Interpolator({ dt: 20 });
  assert.strictEqual(ip.at(0), null);
  ip.push(1000, [0.5, 0.5]);
  assert.deepStrictEqual(ip.at(1000), [0.5, 0.5]);          // eerste punt: geen glijden
  ip.push(1020, [0.6, 0.5]);
  assert.ok(Math.abs(ip.at(1020)[0] - 0.5) < 1e-9);        // begint bij het vorige punt
  assert.ok(Math.abs(ip.at(1030)[0] - 0.55) < 0.01);       // halverwege (dt ~20)
  assert.ok(Math.abs(ip.at(1500)[0] - 0.6) < 1e-9);        // blijft op het laatste punt staan
  ip.push(1040, [0.9, 0.9]);                               // sprong (> 0.25): meteen
  assert.deepStrictEqual(ip.at(1040), [0.9, 0.9]);
  ip.push(1060, null);
  assert.strictEqual(ip.at(1060), null);
  ip.push(1080, [0.1, 0.1]);
  assert.deepStrictEqual(ip.at(1080), [0.1, 0.1]);         // na ongeldig: geen glijden vanaf oud punt
  const mid = new C.Interpolator({ dt: 20 }); mid.push(0, [0, 0]); mid.push(20, [0.1, 0]);
  assert.ok(mid.at(30)[0] > 0.049 && mid.at(30)[0] < 0.051 && mid.at(10)[0] === 0);   // nooit voor het vorige punt
});

t('Interpolator: leert het frame-interval', () => {
  const ip = new C.Interpolator({ dt: 33 });
  let tt = 0;
  for (let i = 0; i < 60; i++) { ip.push(tt, [0.5 + i * 0.001, 0.5]); tt += 16.7; }
  assert.ok(ip.dt > 16 && ip.dt < 19, 'dt=' + ip.dt);
});

t('RateMeter: frames per seconde in een glijdend venster', () => {
  const m = new C.RateMeter(1000);
  assert.strictEqual(m.hz(0), 0);
  for (let i = 0; i < 120; i++) m.add(i * (1000 / 60));
  assert.ok(Math.abs(m.hz(119 * 1000 / 60) - 60) < 1);
  const k = new C.RateMeter(1000);
  for (let i = 0; i < 40; i++) k.add(i * (1000 / 30));
  assert.ok(Math.abs(k.hz(39 * 1000 / 30) - 30) < 1);
  assert.strictEqual(k.hz(1e6), 0);   // niets meer ontvangen
});

t('nextTarget: binnen de rand en ver genoeg van het vorige', () => {
  let seed = 7;
  const rnd = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
  let prev = null;
  for (let i = 0; i < 200; i++) {
    const g = C.nextTarget(rnd, prev, 1920, 1080, { r: 60, edge: 0.12, edgeBottom: 0.18, minDist: 400 });
    assert.ok(g.x >= 230 && g.x <= 1690 && g.y >= 129 && g.y <= 886, JSON.stringify(g));
    if (prev) assert.ok(Math.hypot(g.x - prev.x, g.y - prev.y) >= 400);
    assert.strictEqual(g.r, 60);
    prev = g;
  }
});

t('shotResult en PracticeStats: treffer, mis, gemiddelde afstand in px en %', () => {
  const tg = { x: 960, y: 540, r: 60 };
  const a = C.shotResult(960, 540, tg, 1920);
  assert.ok(a.hit && a.distPx === 0 && a.distPct === 0);
  const b = C.shotResult(960 + 60, 540, tg, 1920);          // precies op de rand telt als treffer
  assert.ok(b.hit && b.distPx === 60);
  const c = C.shotResult(960 + 300, 540 + 400, tg, 1920);   // 3-4-5: 500 px
  assert.ok(!c.hit && Math.abs(c.distPx - 500) < 1e-9 && Math.abs(c.distPct - 500 / 1920 * 100) < 1e-9);
  const st = new C.PracticeStats();
  assert.deepStrictEqual(st.summary(), { shots: 0, hits: 0, misses: 0, avgPx: null, avgPct: null });
  st.add(a); st.add(b); st.add(c);
  const s = st.summary();
  assert.deepStrictEqual([s.shots, s.hits, s.misses], [3, 2, 1]);
  assert.ok(Math.abs(s.avgPx - 560 / 3) < 1e-9 && Math.abs(s.avgPct - (560 / 3) / 1920 * 100) < 1e-9);
  st.reset();
  assert.strictEqual(st.summary().shots, 0);
  const off = C.shotResult(-300, 540, tg, 1920);            // schot buiten het scherm: echte afstand, mis
  assert.ok(!off.hit && Math.abs(off.distPx - 1260) < 1e-9);
});

t('PracticeButtons: toewijzing, baseline en debounce', () => {
  const pb = new C.PracticeButtons();
  assert.deepStrictEqual(pb.feed(counts({ 20: 3 }), 0), []);                       // baseline: oude drukken tellen niet
  assert.deepStrictEqual(pb.feed(counts({ 20: 4 }), 100), ['shoot']);
  assert.deepStrictEqual(pb.feed(counts({ 20: 5 }), 150), []);                     // denderen binnen 120 ms
  assert.deepStrictEqual(pb.feed(counts({ 20: 6 }), 260), ['shoot']);
  assert.deepStrictEqual(pb.feed(counts({ 20: 6, 19: 1 }), 270), ['recenter']);    // Move-knop: midden, geen schot
  assert.deepStrictEqual(pb.feed(counts({ 20: 6, 19: 1, 5: 1 }), 300), ['back']);
  assert.deepStrictEqual(pb.feed(counts({ 20: 6, 19: 1, 5: 1, 11: 1 }), 320), ['close']);
  assert.deepStrictEqual(pb.feed(counts({ 20: 6, 19: 1, 5: 1, 11: 1, 8: 1 }), 340), ['toggle_raw']);
  assert.deepStrictEqual(pb.feed(counts({ 20: 6, 19: 2, 5: 1, 11: 1, 8: 1 }), 400), []);   // recenter binnen 350 ms
  assert.deepStrictEqual(pb.feed(counts({ 20: 6, 19: 3, 5: 1, 11: 1, 8: 1 }), 700), ['recenter']);
  assert.deepStrictEqual(pb.feed(counts({ 20: 7, 19: 3, 6: 1, 5: 1, 11: 1, 8: 1 }), 800), ['shoot']);   // kruis: genegeerd
  pb.reset();
  assert.deepStrictEqual(pb.feed(counts({ 20: 9 }), 900), []);                     // na reset weer baseline
  assert.deepStrictEqual(pb.feed(counts({ 20: 10, 19: 1 }), 1500), ['recenter', 'shoot']);
});

t('aimModeText: modus en middenbron', () => {
  assert.strictEqual(C.aimModeText({ tracking_mode: 'orientation', aim: { center_source: 'auto_initial' } }), 'oriëntatie (voorlopig), auto-midden');
  assert.strictEqual(C.aimModeText({ tracking_mode: 'orientation', aim: { center_source: 'user' } }), 'oriëntatie (voorlopig), midden door jou gezet');
  assert.ok(C.aimModeText(null).indexOf('onbekend') === 0);
});
console.log('JS_TESTS_OK ' + n);

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

console.log('JS_TESTS_OK ' + n);

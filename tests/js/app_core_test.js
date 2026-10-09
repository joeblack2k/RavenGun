'use strict';
// Unittests van src/ravengund/web/app-core.js (tabbladen, batterij, schuifjes met debounce, knoppenindeling,
// games-toestanden). Draait via tools/test-js.sh; het script zet RGApp (require van app-core.js) vooraf.
const assert = require('assert');
const A = RGApp;
let n = 0;
function t(name, fn) {
  try { fn(); } catch (e) { console.log('MISLUKT: ' + name); throw e; }
  n++;
}
// Asynchrone tests lopen achter elkaar.
const queue = [];
function ta(name, fn) { queue.push([name, fn]); }

t('tabbladen: zeven, in de afgesproken volgorde', () => {
  assert.deepStrictEqual(A.TABS.map(x => x.id), ['overzicht', 'richten', 'trillen', 'knoppen', 'led', 'games', 'info']);
});
t('pickTab: hash gaat voor, dan het onthouden tabblad, anders overzicht', () => {
  assert.strictEqual(A.pickTab('#/trillen', 'games'), 'trillen');
  assert.strictEqual(A.pickTab('', 'games'), 'games');
  assert.strictEqual(A.pickTab('#/bestaatniet', 'games'), 'games');
  assert.strictEqual(A.pickTab('', 'rommel'), 'overzicht');
  assert.strictEqual(A.pickTab(null, null), 'overzicht');
  assert.strictEqual(A.pickTab('#/__proto__', undefined), 'overzicht');
});

t('getallen: komma, afronden, logschaal', () => {
  assert.strictEqual(A.fmtNl(0.3, 2), '0,30');
  assert.strictEqual(A.fmtNl(NaN), '-');
  assert.strictEqual(A.percent255(255), 100);
  assert.strictEqual(A.percent255(180), 71);
  assert.strictEqual(A.percent255(999), 100);
  const s = A.logScale(0.05, 5, 1000);
  assert.strictEqual(s.toValue(0), 0.05);
  assert.strictEqual(s.toValue(1000), 5);
  assert.ok(Math.abs(s.toValue(s.toPos(0.3)) - 0.3) < 0.02);
  assert.strictEqual(s.toPos(0.0001), 0);     // buiten het bereik: aan de rand
  assert.strictEqual(s.toPos(100), 1000);
});

t('batterij: niveaus, laden, niet verbonden', () => {
  assert.deepStrictEqual(A.batteryView('ge60', true), { segments: 4, level: 'ok', label: 'Goed', detail: 'minstens 60%', charging: false });
  assert.strictEqual(A.batteryView('min', true).level, 'low');
  assert.strictEqual(A.batteryView('ge20', true).level, 'mid');
  assert.strictEqual(A.batteryView('max', true).segments, 5);
  const c = A.batteryView('charging', true);
  assert.ok(c.charging && c.label === 'Laadt' && c.level === 'charging');
  assert.strictEqual(A.batteryView('charged', true).label, 'Vol');
  const off = A.batteryView('ge80', false);
  assert.ok(off.segments === 0 && off.level === 'none' && off.detail === 'niet verbonden');
  assert.strictEqual(A.batteryView('onzin', true).segments, 0);
  assert.strictEqual(A.batteryView('ge40', true, 55).detail, '55%');   // Navigation Controller met percentage
});

t('knoppen: dertien indeelbare knoppen, geen PS of T', () => {
  const ids = A.BUTTONS.map(b => b.id);
  assert.strictEqual(ids.length, 13);
  assert.ok(ids.indexOf('ps') < 0 && ids.indexOf('t') < 0);
  assert.deepStrictEqual(ids.filter(i => ['pump', 'ss_rl', 'ss_fire', 'sel1', 'sel2', 'sel3'].indexOf(i) >= 0).length, 6);
});
t('acties: keuzelijst met vaste menutoetsen en alle toetsen, zonder dubbele waarden', () => {
  const groups = A.actionGroups();
  const all = [];
  groups.forEach(g => g.options.forEach(o => all.push(o.value)));
  assert.strictEqual(new Set(all).size, all.length);
  ['none', 'reload', 'grenade', 'aux1', 'aux2', 'recenter', 'key:5', 'key:1', 'key:f2', 'key:f3', 'key:a', 'key:esc'].forEach(v => assert.ok(all.indexOf(v) >= 0, v));
  assert.ok(all.indexOf('ps') < 0 && all.indexOf('key:ps') < 0);
  assert.strictEqual(A.actionLabel('key:5'), 'Coin (toets 5)');
  assert.strictEqual(A.actionLabel('key:f2'), 'Test (toets F2)');
  assert.strictEqual(A.actionLabel('key:f3'), 'Service (toets F3)');
  assert.strictEqual(A.actionLabel('key:1'), 'Start (toets 1)');
  assert.strictEqual(A.actionLabel('recenter'), 'Centreren');
  assert.strictEqual(A.actionLabel('key:q'), 'Toets Q');
  assert.strictEqual(A.actionLabel('onbekend'), 'onbekend');
  // alle waarden in de lijst zijn geldig voor de daemon-parser: none/reload/.. of key:<naam>
  all.forEach(v => assert.ok(/^(none|reload|grenade|aux1|aux2|recenter|key:[a-z0-9]+)$/.test(v), v));
});
t('mapChanges: alleen echte wijzigingen', () => {
  const base = { cross: 'reload', circle: 'none', move: 'grenade' };
  assert.deepStrictEqual(A.mapChanges(base, { cross: 'reload', circle: 'key:f2' }), { changes: { circle: 'key:f2' }, count: 1 });
  assert.strictEqual(A.mapChanges(base, {}).count, 0);
  assert.strictEqual(A.mapChanges(base, { sel1: 'none' }).count, 0);          // ontbrekend in de basis telt als 'none'
  assert.strictEqual(A.mapChanges(base, { sel1: 'reload' }).count, 1);
  assert.strictEqual(A.mapChanges(base, { ps: 'reload' }).count, 0);          // PS bestaat niet in de lijst
});
t('pressedSet: knoppen en trekker', () => {
  assert.deepStrictEqual(Object.keys(A.pressedSet(['cross', 'pump'], 0)).sort(), ['cross', 'pump']);
  assert.ok(A.pressedSet([], 200).t);
  assert.ok(!A.pressedSet([], 50).t);
  assert.deepStrictEqual(A.pressedSet(null, 0), {});
});
t('driftcorrectie en klok', () => {
  assert.deepStrictEqual(A.FUSION_CHOICES.map(f => f.id), ['vqf_mag', 'vqf', 'mahony_mag', 'mahony']);
  assert.strictEqual(A.fusionLabel('vqf_mag'), 'Kompas (aanbevolen)');
  assert.strictEqual(A.fusionLabel('mahony'), 'Alleen gyro');
  assert.deepStrictEqual(A.clockChoices(250).map(c => c.hz), [0, 125, 250, 500]);
  assert.deepStrictEqual(A.clockChoices(333).map(c => c.hz), [0, 125, 250, 333, 500]);
  assert.strictEqual(A.clockChoices(0)[0].label, 'Uit');
});

t('games: toestanden', () => {
  let v = A.gameStateView({ state: 'idle', text: 'Er draait geen game.' });
  assert.ok(v.canStart && !v.canStop && !v.busy);
  v = A.gameStateView({ state: 'waiting_move', text: 'Wacht op de Move: druk op de PS-knop.' });
  assert.ok(!v.canStart && v.canStop && v.busy && v.tone === 'warn');
  v = A.gameStateView({ state: 'running', text: 'De game draait.' });
  assert.ok(!v.canStart && v.canStop && v.tone === 'ok');
  v = A.gameStateView({ state: 'failed', text: 'Starten is mislukt.', message: 'Geen Move gevonden.' });
  assert.ok(v.canStart && v.tone === 'bad' && v.text.indexOf('Geen Move gevonden.') > 0);
  v = A.gameStateView({ state: 'stopping', text: 'De game wordt gestopt...' });
  assert.ok(!v.canStart && !v.canStop && v.busy);
  assert.ok(A.gameCardState({ state: 'running', profile: 'Halo' }, 'Halo').active);
  assert.ok(!A.gameCardState({ state: 'running', profile: 'Halo' }, 'TombRaider').active);
  assert.ok(A.gameCardState({ state: 'failed', profile: 'Halo' }, 'Halo').failed);
  assert.ok(!A.gameCardState({ state: 'idle', profile: null }, 'Halo').active);
});

t('plainSettings: echte instellingen, ook zonder Move', () => {
  const st = {
    config: { filter: { enabled: true }, aim: { predict_ms: 40 }, fusion: { mode: 'vqf_mag' } },
    controller: { connected: false, rumble: { enabled: true, strength: 180 } },
    output: { live: { clock: { enabled: true, hz: 250 } } }, yaw_trim: { learning: true }
  };
  const lines = A.plainSettings(st);
  const m = {};
  lines.forEach(l => { m[l[0]] = l[1]; });
  assert.strictEqual(m['Cursor gladstrijken'], 'aan');
  assert.strictEqual(m['Driftcorrectie'], 'Kompas (aanbevolen)');
  assert.strictEqual(m['Vooruitkijken'], '40 ms');
  assert.strictEqual(m['Trillen'], 'aan, sterkte 71%');
  assert.strictEqual(m['Uitvoerklok'], '250 Hz');
  assert.strictEqual(A.plainSettings({}).length >= 4, true);
});

// ---- createLiveSender met nep-timers ----
function fakeClock() {
  let now = 0, id = 0; const timers = {};
  return {
    setTimeout: (fn, ms) => { id++; timers[id] = { fn, at: now + ms }; return id; },
    clearTimeout: (i) => { delete timers[i]; },
    advance: (ms) => { now += ms; Object.keys(timers).forEach(k => { if (timers[k] && timers[k].at <= now) { const f = timers[k].fn; delete timers[k]; f(); } }); },
    pending: () => Object.keys(timers).length
  };
}
const tick = () => new Promise(r => setImmediate(r));

ta('schuifje: slepen geeft hoogstens een verzoek per interval, de laatste waarde komt altijd', async () => {
  const clk = fakeClock(); const sent = []; const done = [];
  const s = A.createLiveSender({ delay: 150, setTimeout: clk.setTimeout, clearTimeout: clk.clearTimeout,
    send: v => { sent.push(v); return Promise.resolve({ ok: true, v }); }, onDone: (r, v) => done.push([r.v, v]) });
  for (let i = 1; i <= 50; i++) s.input(i);        // 50 "pixels" in een keer
  assert.strictEqual(sent.length, 0);              // nog niets verstuurd
  clk.advance(150); await tick();
  assert.deepStrictEqual(sent, [50]);              // een verzoek met de laatste waarde
  for (let i = 51; i <= 60; i++) s.input(i);
  clk.advance(150); await tick();
  assert.deepStrictEqual(sent, [50, 60]);
  assert.strictEqual(done.length, 2);
});
ta('schuifje: loslaten verstuurt meteen en bevestigt met de waarde uit de daemon', async () => {
  const clk = fakeClock(); const done = [];
  const s = A.createLiveSender({ setTimeout: clk.setTimeout, clearTimeout: clk.clearTimeout,
    send: v => Promise.resolve({ ok: true, actual: Math.min(v, 100) }), onDone: (r) => done.push(r.actual) });
  s.input(30); s.input(250);
  s.commit(250);                                   // loslaten: geen 150 ms wachten
  await tick();
  assert.deepStrictEqual(done, [100]);             // de daemon begrensde de waarde; die komt terug
  assert.strictEqual(clk.pending(), 0);
});
ta('schuifje: nooit twee verzoeken tegelijk; wat ondertussen binnenkomt volgt daarna', async () => {
  const clk = fakeClock(); const sent = []; const resolvers = []; const done = [];
  const s = A.createLiveSender({ setTimeout: clk.setTimeout, clearTimeout: clk.clearTimeout,
    send: v => { sent.push(v); return new Promise(r => resolvers.push(() => r({ v }))); }, onDone: (r) => done.push(r.v) });
  s.commit(1);
  assert.deepStrictEqual(sent, [1]);
  s.input(2); s.commit(3);                         // verzoek 1 loopt nog
  assert.deepStrictEqual(sent, [1]);
  resolvers[0](); await tick();
  clk.advance(150); await tick();
  assert.deepStrictEqual(sent, [1, 3]);
  resolvers[1](); await tick();
  assert.deepStrictEqual(done, [3]);               // alleen de eindstand wordt bevestigd
  assert.ok(!s.busy());
});
ta('schuifje: een geweigerde waarde geeft een foutmelding en blokkeert niets', async () => {
  const clk = fakeClock(); const errs = []; let fail = true;
  const s = A.createLiveSender({ setTimeout: clk.setTimeout, clearTimeout: clk.clearTimeout,
    send: v => fail ? Promise.reject(new Error('geweigerd')) : Promise.resolve({ ok: true }),
    onError: e => errs.push(e.message), onDone: () => errs.push('klaar') });
  s.commit(5); await tick();
  assert.deepStrictEqual(errs, ['geweigerd']);
  fail = false;
  s.commit(6); await tick();
  assert.deepStrictEqual(errs, ['geweigerd', 'klaar']);
});

(async () => {
  for (const [name, fn] of queue) {
    try { await fn(); } catch (e) { console.log('MISLUKT: ' + name); console.log(e); process.exit(1); }
    n++;
  }
  console.log('JS_TESTS_OK ' + n);
})();

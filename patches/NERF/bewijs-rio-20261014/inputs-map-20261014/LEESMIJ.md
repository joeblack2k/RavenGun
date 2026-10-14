# NERF RIO inputs — what the switch reports do, as far as 51 bench runs measure it

**Date:** 2026-10-14. The inputs step for NERF (the "RIO DISCONNECTED" banner was the earlier item).
Question: feed the board's switch reports and find which switch does what. Run-by-run table:
`RESULTS-TABLE.md`. This file is what it means, in the order it matters.
**Labels:** MEASURED = seen on screen or in a log; INTERPRETATION = mine, not measured; NOT MEASURED.

> **The per-button map is in `MAP.md`** (written after this note, with the discrete-tap series). Short
> version, all MEASURED: a single discrete press (tap) on **id 7 starts a Player-1 round**, **id 8 a
> Player-2 round** — the clean single-button Start neither mash nor hold could show; **id 3** held = the
> operator menu (TEST); **id 5/6** tapped = Attract Volume. Gun aim = **`RIO_ADC_Val` channels 19,20,21,22**
> (logged during a round). The "no small set starts a round" result below is true only of the **mash**
> stimulus, which the tap series showed is the wrong model of a button.

## The findings, top first

1. **MEASURED — the game acts on injected switch reports.** Feeding `RIO_SW_Count` makes NERF leave
   "Press Start" and run a round: TIME counter counting down 180→177, Player 1/2 active, score 0
   (`sheet-sAllB.jpg`, `evidence-game-start-player2.jpg`; first seen in ni3, reproduced in sAllB).
   This is the game's own reading changing — the only grade of result that counts here.

2. **MEASURED — id 3 is the switch the game treats as TEST.** Holding id 3 steady, alone, opens the
   operator MAIN MENU (`hold3-TEST-opens-menu.jpg`); holding each of the other twelve polled ids alone
   does not (`holdsingle-0-5-overview.jpg`, `holdsingle-6-17-overview.jpg`). The menu's own footer reads
   "Navigation: Vol +/−  Select Item: Test, Trigger  Go Back: Service, Shoulder". That is "the test menu
   opened", not "the screen changed". NOT MEASURED: that id 3 is wired to the cabinet's physical Test button.

3. **MEASURED — the round is real play, not the operator path.** The round in run lGE5 was reached with
   id 3 **not** asserted (mashed {5,6,7,8,13,15,16,17}; id 3 on its real, idle value; TIME 167→141 in
   `sheet-lGE5.jpg`). It does not need TEST, so it is not the operator/test entry. This answers the
   play-vs-test question that was open earlier in the day (see "History" below).

4. **MEASURED — a volume overlay is a diagnostic, kept apart from (1).** Mashing the low group {0–8}
   brings up "Attract Volume" ("Game Volume" during a round) with no round started (`sheet-gLE8.jpg`).
   That is the operator volume reacting, not the game being driven.

5. **MEASURED, with the toggle stimulus only — no small switch set starts a round.** With every
   chosen switch toggled every frame (rdtsc), no single, pair, triple, 5-set or 6-set starts one; only
   sets of eight or more do, and the boundary is non-monotone: `{5,6,7,8,13}` does not start, though it
   is a subset of two sets that do. id 5 and id 13 are each necessary within those sets.
   **This describes how the game reacts to a toggle, which is not how a button behaves** (see 6). It does
   not show that the board has no per-button structure — finding (2) is a clean per-button entry at this
   same layer.

6. **MEASURED — a single discrete press is the right model, and it gives the per-button map.** Mash is a
   toggle; hold is pressed-and-never-released; neither is a press. A **discrete tap** (the switch's count
   ticking up ≈ once a second) is. Tapping **id 7 starts a Player-1 round**, tapping **id 8 a Player-2
   round** (TIME counting down — `tap7-id7-starts-round.jpg`, `tap-sweep-overview.jpg`); the all-mash
   control in the same batch started a round, so the silent taps are real negatives. This is the clean
   single-button Start the mash hid. It also confirms (5) is a property of the mash stimulus, not of the
   board: the board **does** have per-button structure. Earlier I guessed Start might be unreachable as a
   single id — withdrawn; it is id 7 / id 8. Full map and the other buttons: `MAP.md`.

7. **MEASURED — the gun channels.** During a round the game reads `RIO_ADC_Val` channels **19, 20, 21, 22**
   every frame (`adc-channel-log.txt`, 39 reads each) — four channels, two players × two aim axes. Which
   is X vs Y per player is NOT measured. This is the piece that decides aiming can work at all: the aim
   values have a home, and we know exactly which four to fill.

## What it means for the bridge

**MEASURED (disassembly):** every input read the game uses — `RIO_SW_Count` (switches), `RIO_GetValue`
(ids 33/34), `RIO_ADC_Val` (gun) — indexes one per-id descriptor table into the same 957-byte,
double-buffered report buffer (`rsample`). On the real cabinet the board's read thread fills that buffer.

**INTERPRETATION:** an input bridge should fill that buffer with a synthesized report — per-switch
transitions, levels and ADC values — rather than patch each read function. That gives every read a
coherent picture at once. Jurassic Park has the same RIO library code inlined, so the **buffer
architecture** carries over; the **id meanings** (id 3 = Test) are NERF's own game code and must be
re-measured for JP, not assumed.

## Open, not interpreted

* ids 7 and 8 held alone each bring up a scrolling **Credits** roll (`sheet-h7-credits.jpg`,
  `sheet-h8-credits.jpg`). Same reaction on two ids, so not a distinct map entry. It may simply be an
  attract page the shorter baseline runs didn't reach; not checked.
* `RIO_GetValue(33)`/`(34)` forced to all-ones or all-zeros does nothing (`gvONES`, `gvZEROS`). Whatever
  they carry, it is not Test and not Start.

## Not established

* How a real Start is delivered (next test, above), and which ids are coin, trigger, service, volume.
* The exact minimal start-set under the toggle (≥7; deliberately not pinned — it describes the wrong model).
* The gun: `RIO_ADC_Val` (`0xa6d0`) reads 4 bytes per channel from the same buffer; which channels are
  aim X/Y is unmeasured. Recoil exports: `RIO_GunModeSet`, `RIO_GunSolenoidCfgSet`.

## History — what was open earlier and what settled it

Until the held-level runs, this note said the TIME-counter round was **unresolved**: the mash showed the
"Hold test button for 2 seconds to enter test menu" prompt (`evidence-testbutton-prompt.jpg`), so it
might have been the operator path. Two runs settled it: **wSTEADY** (commit `40a33a2`) showed a held level
opens the real operator menu, which the round does not resemble; **h3 + lGE5** (commit `04cb803`) showed
id 3 is Test and the round starts with id 3 idle. A note in between called the mash round a toggle-noise
artifact; that was wrong and is withdrawn — it is real play, reached without Test.

## Where the raw is

Selected sheets and close-ups are here. Full raw frame sets and run records (all 51 runs) are out of git
under `~/.local/share/ravengun/bench-captures/NERF/inputs-map/` (`OPNAMES.txt` in the parent folder).

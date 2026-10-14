# NERF RIO inputs — how far the id→action map goes, and the question it leaves open

**Date:** 2026-10-14. This is the **inputs** step for NERF (the banner is already gone — that was the
earlier RIO item). The question was: feed the board's switch reports and find which switch is start,
coin, trigger, service. The honest answer, measured over 35 bench runs, is below. The full run-by-run
table is `RESULTS-TABLE.md`; this file is what it means.

## Two results, kept apart (one is real, one is a diagnostic)

1. **The game does read and act on `RIO_SW_Count`.** Feeding ever-changing counts makes NERF leave
   "Press Start" and start a round — a **TIME counter** counts down 180→177, Player 1/2 active, score 0
   (`sheet-sAllB.jpg`, `evidence-game-start-player2.jpg`; reproduced from ni3). This is the game's own
   reading changing, not just bytes sent. It is the one grade of result that counts.

2. **A service/volume overlay** appears when the **low group {0–8}** is mashed (`sheet-gLE8.jpg`):
   "Attract Volume" in attract, "Game Volume" during a round. This is a **diagnostic** reacting — the
   operator volume being changed — **not** the game being driven. It is reported separately on purpose.

## What a single switch does: nothing

All thirteen polled ids {0,1,2,3,4,5,6,7,8,13,15,16,17}, mashed one at a time, do **nothing** — no
start, no overlay (`sheet-s5.jpg` is representative; all thirteen are in the table). So there is no
"this id = start" / "this id = coin" at the single-switch level. That is a measured negative, and the
all-mash control (which **does** react, run in the same session: sAllB) proves the instrument was live
while the singles stayed quiet — the negatives are real, not a dead instrument.

## The game-start needs a broad set — there is no small combination to put on the board

This is the core finding, and it is not the clean "start = {a,b}" we went looking for:

* **No single, no pair, no triple, no 5-set, no 6-set starts the game.** Pairs `{4,13}`, `{5,13}`,
  `{5,15}`, `{13,15}` — nothing. Triples `{5,6,13}`, `{5,7,13}`, `{5,8,13}`, `{5,13,15}` — nothing. The
  5-set `{5,6,7,8,13}` — nothing. The 6-sets `{5,6,7,8,13,15}` and `{4,5,6,7,8,13}` — nothing.
* **Only sets of ≥8 switches start it:** `{5,6,7,8,13,15,16,17}`, `{4,…,8,13,15,16,17}`, `{0–8,13}`, all 13.
* **id 5 and id 13 are each necessary** (clean one-switch contrasts, table), and the remaining
  contributors come from the **outer** switches ({0–4} and/or {15–17}), not the middle {6,7,8}.
* **The boundary is non-monotone:** `{5,6,7,8,13}` does **not** start, yet it is a subset of two sets
  that do (`{0–8,13}` and `{5,6,7,8,13,15,16,17}`). So there is **no small minimal set** — the start
  is reached only by a broad pattern across many switches at once.

**What I read into that (my interpretation, not a measurement):** starting a game by hammering eight
switches at once is not how a game is played, and per-switch `RIO_SW_Count` values do not map to single
logical buttons. The real board delivers one **coherent report** (the 957-byte `rsample` buffer the
game indexes), and the game's start-gating depends on the whole pattern, not on one "start bit." So an
input bridge will have to reproduce that report structure with real per-switch semantics — it cannot
just toggle individual `RIO_SW_Count` ids. The clean per-button map does not exist at this layer.

## UNRESOLVED — and it must be settled before anything is wired

**Whether the TIME-counter state is real gameplay or the operator/test path is not resolved.** When the
low switches are mashed, the game shows **"Hold test button for 2 seconds to enter test menu"**
(`evidence-testbutton-prompt.png` / `.jpg`) — so a **TEST switch is among the mashed ids**. That the
"game-start" needs a broad held combination, not a single Start press, is exactly what a test/operator
entry looks like.

Against that: the rdtsc mash toggles each switch every frame, so it cannot hold the test button cleanly
for two full seconds — which suggests the test **menu** is never actually entered, and the TIME-counter
state may be a real round after all. But that is an argument, not a measurement. **Until it is settled,
this is a path that *looks like* the input, not the input.** If it turns out to be the operator screen,
it is a way to *configure* the cabinet — useful — but it is **not** the thing that makes the gun shoot,
and it must not go on the board as if it were.

## UPDATE (same day) — the correct input model is a HELD level, and it opens the operator menu

Two more runs settled the shape of the question. First, forcing `RIO_GetValue(33)/(34)` (the only other
read path the game uses) to all-ones and to all-zeros does **nothing** (`gvONES`, `gvZEROS`) — so the
test/service switches are **not** read through `RIO_GetValue`; that hypothesis is refuted.

Then the real correction: a real button is a **held level**, not a rapid toggle. Feeding `RIO_SW_Count` a
**steady constant** for every id (no press activity — the opposite of the mash) **opens the operator
MAIN MENU** and holds it there (`steady-operator-menu.jpg`, `sheet-wSTEADY-steady-opens-menu.jpg`):
FACTORY SET UP / DIAGNOSTICS / PRICING / GAME SETTINGS / AUDITS / VOLUME / **CALIBRATION** / UTILITIES /
SYSTEM INFORMATION, "COST TO PLAY: FREE PLAY." Its own footer names the operator buttons:
**"Navigation: Vol +/−   Select Item: Test, Trigger   Go Back: Service, Shoulder."**

So the rdtsc **mash was the wrong input model** — a toggle, not a press. The right model is a steady held
level. Under it, **held switches → the operator menu** (a `Test` button held), which is a different state
from the mash's TIME-counter. That makes the mash "game-start" look like an artifact of toggle-noise, not
a real round — but which single held switch is `Test`, and whether a single held `Start` begins a real
round, is the steady-single isolation now in progress. **The positive control the question needed — the
operator menu actually opening — is in hand** (`wSTEADY`); naming each button is the next measurement.

## What this run did NOT establish

* The exact minimal start-set (it is broad, ≥7 switches; bounded but deliberately not pinned — chasing
  an 8-to-10-switch set with remove-one controls is many more bench slots for little more meaning).
* Whether the TIME-counter state is play or test (above) — the next measurement, and the one that matters.
* The **gun** (`RIO_ADC_Val`) — not instrumented at all this session.

## Where the raw is

Selected contact sheets + the two close-up frames are here. The full raw frame sets and run records are
out of git under `~/.local/share/ravengun/bench-captures/NERF/inputs-map/` (`OPNAMES.txt` in the parent).

## UPDATE 2 (same day) — the discriminator is answered, and what that does and does not settle

**TEST is id 3, with its control.** Holding id 3 alone opens the operator MAIN MENU (`hold3-TEST-opens-menu.jpg`);
holding each of the other twelve polled ids alone does not (`holdsingle-0-5-overview.jpg`,
`holdsingle-6-17-overview.jpg`). That is the claim "the test menu opened", not just "the screen changed",
and the footer text ("Select Item: Test, Trigger") agrees with it. Measured: the id-3 → menu link.
Not measured: that id 3 is *physically* the cabinet's Test button (it is the id the game treats as Test).

**Play or test — the one yes/no:** the TIME-counter round was reached **without** id 3 asserted. Run lGE5
mashed {5,6,7,8,13,15,16,17} — no id 3 — and a round started (TIME 167→141 in `sheet-lGE5.jpg`).
So the round state does **not** need TEST; it is not the operator path. That is the result the earlier
"unresolved" section was waiting for, and it points to real play.
What it does **not** show: *how* a real round is meant to be started. The mash that did it hit eight
switches in rapid toggles; no single held id starts one. A single Start press probably has to be a
**transition** (the function is a transition counter), which a steady level can't make — **that is a guess,
not a measurement**, and it is the next test (one id at a time, one discrete press every few seconds).

**Corrections to what I wrote above:** the mash "game-start" is *not* a toggle-noise artifact of the test
path, as I suggested in the first UPDATE — it is real play reached through a path that doesn't need TEST.
The first UPDATE's reading of the steady/held model was right about the menu and over-reached about the
round.

**Open and not interpreted:** ids 7 and 8 each bring up the scrolling Credits roll (`sheet-h7-credits.jpg`,
`sheet-h8-credits.jpg`). I did not look at every other frame for Credits, so "only these" is not established.
Same reaction on two ids — not a distinct map entry yet.

**Still not touched:** the gun. `RIO_ADC_Val` (0xa6d0) reads a 4-byte value per channel from the same
957-byte report buffer as the switches; which channels are aim X/Y is unmeasured.


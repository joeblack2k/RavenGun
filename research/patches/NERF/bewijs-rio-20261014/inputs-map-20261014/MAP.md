# NERF RIO inputs — the map: every stimulus tried, and the per-button result (2026-10-14)

Method throughout: patch `librio.so` so a chosen set of `RIO_SW_Count` ids returns a chosen pattern,
run the native route on our own core, read the screen. 66 bench runs. The decisive move was trying a
**discrete tap** (a real press, ~once a second) after mash and hold — that is the stimulus that behaves
like a button, and it is what produced the map below.

## The three stimuli, and what each proved

| stimulus | what the switch value does | the key result |
|---|---|---|
| **mash** (rdtsc raw) | flips every frame (~60/s) — a toggle, not a press | no single id does anything; only sets of ≥8 ids start a round (a wrong-model artifact); non-monotone. |
| **hold** (steady `1`) | pressed and never released | **id 3 held alone opens the operator menu**; no single id starts a round. |
| **discrete tap** (rdtsc high bits) | one press ≈ every 1 s | **id 7 → Player-1 round, id 8 → Player-2 round**; id 3 → "hold test" prompt; id 5, id 6 → "Attract Volume". |

The all-mash control ran in the tap batch and started a round (`tapCTL`), so the empty taps are real
negatives, not a dead instrument.

## The per-button map (MEASURED by discrete tap unless noted)

| id (`RIO_SW_Count`) | button | how it was shown |
|---|---|---|
| **3** | **TEST** | held alone → operator MAIN MENU (`hold3-TEST-opens-menu.jpg`); tapped → "Hold test button…" prompt (tap3) |
| **5** | **VOLUME** (one of +/−) | tapped → "Attract Volume" overlay (tap5) |
| **6** | **VOLUME** (one of +/−) | tapped → "Attract Volume" overlay (tap6) |
| **7** | **Player-1 start** (trigger or start button) | tapped → Player-1 round starts, TIME 178→139 (`tap7-id7-starts-round.jpg`) |
| **8** | **Player-2 start** | tapped → Player-2 round starts, TIME ~140 (tap8, `tap-sweep-overview.jpg`) |
| 0,1,2,4,13,15,16,17 | no attract-visible action | tapped → normal attract (coin / service / shoulder / 2nd-trigger are plausible but NOT shown — attract may not surface them) |

**Gun aim: `RIO_ADC_Val` channels 19, 20, 21, 22** — MEASURED: the game read exactly these four every
frame of a round (`adc-channel-log.txt`, 39 reads each). Four channels = two players × two axes. Which
channel is P1-X vs P1-Y etc. is NOT measured (needs feed-and-watch on the crosshair).
Gun trigger is a switch, not an axis — ids 7/8 are the per-player trigger-or-start press above.
Recoil: `RIO_GunModeSet`, `RIO_GunSolenoidCfgSet` (exports; untested).

## Labels
INTERPRETATION (not measured): that 7/8 are specifically the *trigger* vs a dedicated Start; that 5/6 are
Vol-up vs Vol-down; the player/axis assignment of ADC 19–22; that the eight silent ids are coin/service/etc.
MEASURED: every "how it was shown" cell above, and the ADC channel set. `RIO_GetValue(33)/(34)` forced to
all-ones/zeros does nothing — not Test, not Start.

## For the bridge (and for Jurassic Park)
All of these — switch ids and ADC channels — index one 957-byte report buffer (`rsample`). A bridge fills
that buffer; it does not patch each read. JP shares the RIO library, so the buffer architecture carries;
the id/channel *meanings* are NERF game code and must be re-measured for JP.

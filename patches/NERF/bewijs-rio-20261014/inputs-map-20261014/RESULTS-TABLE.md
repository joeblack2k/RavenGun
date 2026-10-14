# NERF RIO inputs — every run, the set mashed, and what it did (2026-10-14)

**Method.** The game reads switches through `RIO_SW_Count(id)` (measured, run ni2). Each run binds a
patched `librio.so` that makes `RIO_SW_Count` return an ever-changing (rdtsc, monotonic) count for a
chosen **set** of ids — those switches read as continuously pressed — while every other id runs the
untouched real body (stable → not pressed). So each run mashes exactly one declared set of switches and
nothing else. Read strictly by the **TIME counter** (a game round started, 180→177 countdown) vs the
**volume overlay** (a service/operator reaction) vs nothing. "librio.so mapped" confirmed in every run's
watch record (our own native core; no Wine/TeknoParrot/OpenParrot). Each run 70 s, captures every 6 s.

The polled switch ids are {0,1,2,3,4,5,6,7,8,13,15,16,17} (measured, ni2). `RIO_SW_Count` serves ids
0–18 and 36–75 through the same table (measured from disassembly: the `a5f0` gate re-admits 0–18), so all
of these are in range.

| run | set mashed | GAME-START (TIME) | volume overlay | sheet |
|---|---|:--:|:--:|---|
| s0 … s8, s13, s15, s16, s17 | each single id, alone (13 runs) | no | no | `sheet-s5.jpg` (representative) |
| gLE8 | {0,1,2,3,4,5,6,7,8} | no | **yes — "Attract Volume"** | `sheet-gLE8.jpg` |
| gGE13 | {13,15,16,17} | no | no | `sheet-gGE13.jpg` |
| ni3 / sAllB | all 13 | **yes** (180→177) | **yes — "Game Volume"** | `sheet-sAllB.jpg` |
| lLE13 | {0,1,2,3,4,5,6,7,8,13} | **yes** (Player 2) | yes + **"Hold test button…" prompt** | `sheet-lLE13.jpg` |
| lGE8 | {8,13,15,16,17} | no | no | — |
| lGE6 | {6,7,8,13,15,16,17} | no | no | `sheet-lGE6.jpg` |
| lGE5 | {5,6,7,8,13,15,16,17} | **yes** | — | `sheet-lGE5.jpg` |
| lGE4 | {4,5,6,7,8,13,15,16,17} | **yes** | — | — |
| pAB413 | {4,13} | no | no | — |
| pAB513 | {5,13} | no | no | `sheet-pAB513.jpg` |
| t5_6_13 | {5,6,13} | no | no | — |
| t5_7_13 | {5,7,13} | no | no | — |
| t5_8_13 | {5,8,13} | no | no | — |
| t5_6_7_8_13 | {5,6,7,8,13} | no | no | `sheet-t5_6_7_8_13.jpg` |
| u5_13_15 | {5,13,15} | no | no | — |
| u5_15 | {5,15} | no | no | — |
| u13_15 | {13,15} | no | no | — |
| v678_13_15 | {5,6,7,8,13,15} | no | no | — |
| v4_5678_13 | {4,5,6,7,8,13} | no | no | `sheet-v4_5678_13.jpg` |

## What the table says, read straight off it

* **No single switch does anything** — 13 ids, each inert.
* **Two different reactions, and they live in different places.** The **volume/service** overlay (a
  diagnostic) comes from the **low group {0–8}** (gLE8), labelled "Attract Volume" in attract and
  "Game Volume" once a round is running. The **game-start** (TIME counter) never comes from {0–8} alone
  nor from {13,15,16,17} alone — only from broad sets.
* **Game-start needs a broad set — no small combination works.** No single, no pair (`{4,13}`, `{5,13}`,
  `{5,15}`, `{13,15}`), no triple (`{5,6,13}`, `{5,7,13}`, `{5,8,13}`, `{5,13,15}`), no 5-set
  `{5,6,7,8,13}`, no 6-set (`{5,6,7,8,13,15}`, `{4,5,6,7,8,13}`) starts the game. Only sets of **≥8**
  switches do: `{5,6,7,8,13,15,16,17}` (lGE5), `{4,…,8,13,15,16,17}` (lGE4), `{0–8,13}` (lLE13), all 13.
* **id 5 and id 13 are each necessary** (clean one-switch contrasts): lGE5 starts, lGE6 = lGE5−{5} does
  not; lLE13 starts, gLE8 = lLE13−{13} does not. The extra contributors come from the **outer** switches
  ({0–4} and/or {15–17}), **not** the middle {6,7,8} ({5,6,7,8,13} and the {5,x,13} triples do not start).
* **The boundary is non-monotone in the awkward way:** `{5,6,7,8,13}` does not start even though it is a
  subset of two sets that do (lLE13 and lGE5). So there is **no small minimal start-set** to name.

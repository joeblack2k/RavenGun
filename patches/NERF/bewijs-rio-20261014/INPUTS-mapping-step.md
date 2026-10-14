# NERF RIO inputs — mapping step and first result (2026-10-14)

The RIO board item for NERF was "banner gone" (done). This is the **inputs** step on the same board.
Two separate claims, both stated per their kind:

## Measured: which function and which ids the game reads

**MEASURED** (instrumented `librio.so` that logs each read call's id to stderr; run ni2, NERF in attract):
* The game reads switches via **`RIO_SW_Count(id)`** (not `RIO_SW_State`), polling **13 ids every frame:
  {0,1,2,3,4,5,6,7,8,13,15,16,17}** (5,572 polls each over the run; raw id bytes in the run log).
* It also reads **`RIO_GetValue(33)` and `RIO_GetValue(34)`** rarely (4 each).
* The gun is `RIO_ADC_Val` (not yet instrumented).
* `RIO_SW_State` / `RIO_SW_StateEx`: **0** calls — the game does not use them.

**Not yet measured (would be INFERRED):** which of those 13 ids is start / trigger / coin / service /
test. The next step isolates each by feeding one id at a time and watching which game action results.

## Result: the game's own reading CHANGED (not just "bytes sent")

**MEASURED** (run ni3): `RIO_SW_Count` patched to return an ever-changing count (every switch reads as
pressed each frame). NERF **left "Press Start" and started a game** — a TIME counter runs (179 → 177),
Player 1/2 active, the volume slider pops (service also mashed). So the game **reads and acts on** the
values fed through `RIO_SW_Count`; the injection path works end to end
(`ni3-game-started-reading-changed.jpg`, `ni3-attract-to-gameplay-sheet.jpg`).

This is the game's own reading changing, not merely that bytes were sent. It is **all switches mashed at
once**, not a clean single input — clean start/trigger/coin needs the id→action map above, then a bridge
from the daemon's input state into `RIO_SW_Count`'s return (same shape for JP).

# Infinity Blade FX — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** correction. This game was believed handled; the *names* are handled, the *ordinals* are not.
**Verdict:** the Matrix dongle demand is a **subset of the Rabbids Hollywood demand** (`Init_MatrixAPI`,
`Dongle_Count`, `Dongle_ReadSerNr`) — but this is the **only game in the inventory that imports
`matrix32.dll` by ordinal**, and our DLL's ordinals do not match the original's. Ordinal 40 / 55 / 61 resolve
to three JNI stubs that all return `0`, so `Dongle_Count` and `Dongle_ReadSerNr` would answer **0** and the
check would fail. This is a latent break, not today's blocker (the game is parked on a graphics/UE3 issue),
but it must be fixed before the dongle can be called done.

---

## 1. What is present (MEASURED)

`Binaries/Win32/matrix32.dll` — **TeknoParrot's own emulator**, 109056 bytes, md5
`3a5bed5fc3de3797073fced37b33d41f` (MSVC 14.24, 2023-02-04). This is the only copy of that file anywhere on
the share.
`Binaries/Win32/matrix32.TEKNOPARROT.dll` — **the original 2009 LPT-dongle DLL**, 114688 bytes, md5
`6e75f453dca9380144620d80f467a0e6`.

That pair is the arrival shape of a TeknoParrot-managed dump: TeknoParrot renamed the original and dropped
its own emulator in its place. (Contrast Drakons Realm Keepers, where the original is still in place and
TeknoParrot shipped nothing.)

The importing binary is `Binaries/Win32/SwordGame.exe`, 49878016 bytes, md5
`613f3b4aced85a3758b396e5d1dd13e8`. It is a **native 32-bit UE3 game** — no Unity, no BepInEx, so the
managed-patch route does not exist for it.

## 2. What it asks for (MEASURED) — and the reason this note exists

`objdump -p SwordGame.exe` shows the import descriptor:

```
	DLL Name: matrix32.dll
	vma:     Ordinal  Hint  Member-Name  Bound-To
	021c5dc0     40  <none> <none>
	021c5dc4     55  <none> <none>
	021c5dc8     61  <none> <none>
```

**The import is by ordinal. There is no name.** Calibration that this reading is real and not an `objdump`
artefact: the descriptor immediately after it, `msvcm90.dll`, prints the other form —
`<none>  0005  ?ThrowNestedModuleLoadException@...` — i.e. `objdump` prints Hint and Member-Name when they
exist and `<none>` when they do not. `matrix32.dll`'s three entries have both as `<none>`.

So the game will resolve export **ordinal 40, 55 and 61** of whatever `matrix32.dll` it finds. What sits
there:

| DLL | size | md5 | ordinal 40 | ordinal 55 | ordinal 61 |
|---|---|---|---|---|---|
| `matrix32.TEKNOPARROT.dll` (original 2009) | 114688 | `6e75f453…` | **`Init_MatrixAPI`** | **`Dongle_Count`** | **`Dongle_ReadSerNr`** |
| `matrix32.dll` (TeknoParrot emulator) | 109056 | `3a5bed5f…` | **`Init_MatrixAPI`** | **`Dongle_Count`** | **`Dongle_ReadSerNr`** |
| **our drop-in `patches/InfinityBladeFX/dropin/matrix32.dll`** | 25614 | `08e25d18…` | `Java_Matrix_Dongle_1GetKeyFlag` | `Java_Matrix_GetVersionAPI` | `Java_Matrix_MasterKey_1Find` |
| **our `matrix/out/matrix32.dll`** | 25614 | `8416d0e5…` | `Java_Matrix_Dongle_1GetKeyFlag` | `Java_Matrix_GetVersionAPI` | `Java_Matrix_MasterKey_1Find` |

TeknoParrot's emulator reproduces the original's ordinal layout exactly — that is what makes it a drop-in.
**Ours does not.** Our names sit at ordinals 4 (`Dongle_Count`), 17 (`Dongle_ReadSerNr`) and 32
(`Init_MatrixAPI`), because `matrix/src/matrix.def` lists the exports **alphabetically**.

Calibration of those two `objdump` greps: the pattern `\+base\[ *(40|55|61)\]` returns 6 entries in the
original (3 in the Export Address Table, 3 in the Ordinal/Name Pointer Table) and **0** for a deliberately
absent ordinal such as `+base[200]`. Format confirmed from the table header our build prints:
`[Ordinal/Name Pointer] Table -- Ordinal Base 1`, with `[   3] +base[   4]  0003 Dongle_Count` — i.e.
`+base[N]` **is** ordinal N.

Consequence (INFERRED, but the reasoning is closed): an ordinal import cannot fail to load here, because our
DLL does export all 113 ordinals 1–113. It binds to the **wrong function** instead. All three of ours at
40/55/61 are `MX_JNI_*` stubs whose body is `{ (void)a; …; return 0; }`. So:

* `Init_MatrixAPI` → 0 — coincidentally fine (0 means "in order");
* `Dongle_Count` → **0** — the check requires > 0 → fail;
* `Dongle_ReadSerNr` → **0** — the check requires > 0 → fail.

The prior work was aware of the ordinals: `patches/InfinityBladeFX/dropin/dropin.ini` states the import table
"noemt de module `matrix32.dll` met drie ordinalen (#40, #55 en #61)". But `matrix.def`'s own header asserts
the opposite of what that implies:

> *"De volgorde hieronder is alfabetisch … de ordinalen liggen daardoor anders dan in het origineel. Dat is
> geen probleem: de games importeren op naam (P/Invoke doet GetProcAddress met de naam), niet op ordinal."*

That is true of every other game in the inventory — the Unity games resolve `DllImport` **by name** (which is
why Drakons, Rabbids, Rampage and Tomb Raider are unaffected) — and **false for this one**. The two statements
were never reconciled, and the drop-in was written on the by-name assumption.

What the game demands, semantically, is still only three calls: `Init_MatrixAPI`, `Dongle_Count`,
`Dongle_ReadSerNr`. No `Dongle_ReadData`/`WriteData`, so no dongle memory; `patches/InfinityBladeFX/dropin/matrix/matrix.ini`
already records that.

## 3. Do we already answer it?

**The names: yes. The ordinals: no.** Our library answers the right three calls with the right values
(`Init_MatrixAPI`→0, `Dongle_Count`→2, `Dongle_ReadSerNr`→69420), and it is ABI-checked against the original
for name table and `ret N` — but the game will not reach those functions through this import table.

So of the four games in the Matrix family, this is the one where "we answer it" is **false in practice** while
looking true on paper. Drakons, Rabbids, Rampage and Tomb Raider are unaffected because they bind by name.

## 4. What is missing (and cost)

1. **An ordinal-faithful export layout.** Two ways, both small:
   * *Preferred:* regenerate `matrix.def` from the **original's** ordinal table, so ordinals 1–113 match
     `matrix32.TEKNOPARROT.dll` exactly and the DLL is a true drop-in for ordinal importers as well as name
     importers. The original is on the NAS and its full 113-entry ordinal table can be dumped mechanically.
   * *Minimum:* pin the three that are imported — `Init_MatrixAPI @40`, `Dongle_Count @55`,
     `Dongle_ReadSerNr @61` — and move every other export clear of those three ordinals (`@114` upwards).
     A `.def` may assign any ordinal, so this needs no code change.
2. **An ordinal check in the ABI gate.** `matrix/src/controleer-abi.py` currently compares the *name* table and
   each function's `ret N`; `scripts/build-matrix.sh` runs it after every build, and it has been reporting
   "113 names identical, 0 differences in argument sizes". It must also compare **ordinals**, and the build
   must fail if a game that imports by ordinal would bind wrongly. Otherwise this class of bug cannot be
   caught: the current check passes on the broken DLL.
3. **Re-verify against the importer.** A check that walks `SwordGame.exe`'s import table and asserts that each
   ordinal resolves to the intended name inside our DLL. Cheap, and it is the only end-to-end proof.

**Cost:** about half a day — a `.def` regeneration (or three `@` pins), roughly twenty lines in
`controleer-abi.py`, a rebuild, and the ordinal comparison re-run. No new emulation code.

**Priority note:** Infinity Blade FX is parked on a graphics problem, not on the dongle. Its own inventory
entry says UE3 starts, then white or black screen, and the log stops at `Kaboom: InitLi…`. So this ordinal gap
is **not what blocks the game today**. It becomes the blocker the moment the graphics issue is solved, and it
would then present as a dongle failure — i.e. it would waste exactly the kind of time this note is meant to
save. It also silently affects any future native PE/UE3 Matrix title, since those are the ones that import
from a static table.

## 5. Proposed test (NOT RUN) + positive control

**One test, entirely static — no game run needed.**

```
# for each of the three, and for every export we care about:
objdump -p <our matrix32.dll> | grep -E '\+base\[ *(40|55|61)\]'
```
* **Passing result (positive control).** The command prints exactly the three lines the original prints —
  `[  39] +base[  40] … Init_MatrixAPI`, `[  54] +base[  55] … Dongle_Count`,
  `[  60] +base[  61] … Dongle_ReadSerNr` — and a comparison of the full 113-entry ordinal table against
  `matrix32.TEKNOPARROT.dll` reports **0 differences**. Additionally, the extended
  `controleer-abi.py` must exit 0 and `scripts/build-matrix.sh` must pass.
* **Failing result (negative control).** Run the same command on the **current** build: it prints
  `Java_Matrix_Dongle_1GetKeyFlag` / `Java_Matrix_GetVersionAPI` / `Java_Matrix_MasterKey_1Find`. The check
  must therefore fail on today's artefacts — that is the calibration that proves the test can detect the
  fault at all. A version of this check that *passes* on the current DLL is a broken check.

Once the layout is fixed, the run-level test is the same as the other Matrix games: start it with the drop-in
and `RAVENGUN_MATRIX_LOG`; our own `matrix.log` must show `Init_MatrixAPI -> 0`, `Dongle_Count -> aantal, poort = N`,
`Dongle_ReadSerNr -> 69420`. That run is currently pointless while the game stops at `Kaboom: InitLi…`.

## 6. Evidence log

```
# the import is by ordinal, with no name
objdump -p .../Infinity Blade FX/Binaries/Win32/SwordGame.exe | grep -B2 -A5 matrix32
	DLL Name: matrix32.dll
	vma:     Ordinal  Hint  Member-Name  Bound-To
	021c5dc0     40  <none> <none>
	021c5dc4     55  <none> <none>
	021c5dc8     61  <none> <none>
# calibration: the very next descriptor shows the by-name form
	DLL Name: msvcm90.dll
	021c5dd8  <none>  0005  ?ThrowNestedModuleLoadException@<CrtImplementationDetails>@@YAXP$AAVException@System@@0@Z

# what sits at those ordinals (+base[N] IS ordinal N)
forty=$(objdump -p D | grep -E '\+base\[ *(40|55|61)\]' | grep -v 'Export RVA')
  original  matrix32.TEKNOPARROT.dll : [39]+base[40] Init_MatrixAPI ; [54]+base[55] Dongle_Count ; [60]+base[61] Dongle_ReadSerNr
  tp-emu    matrix32.dll            : same three
  ours      dropin/matrix32.dll     : [39]+base[40] Java_Matrix_Dongle_1GetKeyFlag
                                      [54]+base[55] Java_Matrix_GetVersionAPI
                                      [60]+base[61] Java_Matrix_MasterKey_1Find
  ours      matrix/out/matrix32.dll : same three Java_* stubs

# calibration of that grep
objdump -p original | grep -cE '\+base\[ *(40|55|61)\]'   -> 6   (3 RVA + 3 named)
objdump -p original | grep -cE '\+base\[ *200\]'           -> 0   (absent ordinal)
objdump -p /home/deck/projects/ravengun/matrix/out/matrix32.dll | grep -A6 'Ordinal/Name Pointer'
  -> [Ordinal/Name Pointer] Table -- Ordinal Base 1
     	[   0] +base[   1]  0000 CloseIWPort
     	[   1] +base[   2]  0001 DllRegisterServer
     	[   3] +base[   4]  0003 Dongle_Count        (alphabetical order => ordinals are not the original's)

# our three intended functions, from the same table
  ours: [3]+base[4] Dongle_Count ; [16]+base[17] Dongle_ReadSerNr ; [31]+base[32] Init_MatrixAPI
  orig: [54]+base[55] Dongle_Count ; [60]+base[61] Dongle_ReadSerNr ; [39]+base[40] Init_MatrixAPI

# the DLL identities used above
  Infinity Blade FX/Binaries/Win32/matrix32.dll              109056  3a5bed5fc3de3797073fced37b33d41f  (TeknoParrot emu)
  Infinity Blade FX/Binaries/Win32/matrix32.TEKNOPARROT.dll  114688  6e75f453dca9380144620d80f467a0e6  (original 2009)
  patches/InfinityBladeFX/dropin/matrix32.dll                 25614  08e25d18d61fb36103ffa72a9ac7e40e  (ours, shipped)
  ravengun/matrix/out/matrix32.dll                            25614  8416d0e5b89f0872923c111a3a0d4c7a  (ours, current build)

# the claim in our own source that this contradicts
  matrix/src/matrix.def, header:
    "De volgorde hieronder is alfabetisch (zoals de naam-tabel); de ordinalen liggen daardoor anders dan in
     het origineel. Dat is geen probleem: de games importeren op naam (P/Invoke doet GetProcAddress met de
     naam), niet op ordinal."
```

**Also measured, and worth recording:** we searched TeknoParrot's own decompiled core strings
(`findings/decompiled/dump_strings.txt`, 1612319 bytes) for the Matrix API and found **none** —
`Dongle_Count` 0, `Dongle_ReadSerNr` 0, `Init_MatrixAPI` 0, `matrix32` 0, `matrix64` 0.
Calibration: `hasp_login` 1 hit, `amdaemon` 1 hit, absent marker 0. That is consistent with TeknoParrot
handling Matrix through its **drop-in DLL** rather than through the core. The calibration is weak (only one
hit for a known-present string in a 1.6 MB dump), so this is recorded as **INFERRED**, not proven.

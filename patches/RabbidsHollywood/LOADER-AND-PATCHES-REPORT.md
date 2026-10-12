# Rabbids Hollywood — loader & patches state (report)

Author: per-game loader specialist (delegated agent). Date of survey: against the workspace mirrors
present on the Windows bench machine; the authoritative Deck sources are
`~/projects/ravengun` and `~/projects/ravengun-patches`.

Scope rules honoured: the NAS was only read; no dongle/HASP/keychip/integrity work was done; the game
was NOT started; nothing was committed.

Evidence tags: **[M]** = measured and recorded in a project file (quoted with path:line) · **[R]** =
read from source/config in this survey · **[I]** = inferred, not measured.

---

## 1. What the game is, and which route it needs

Adrenaline Amusements, 2018, Unity 2017.2, 64-bit Mono Windows build. Entry point `Game.exe`
(650,752 B, sha256 `979f9736...8da95d` **[R]** `patches/RabbidsHollywood/game.ini:33-35`). The NAS
folder name ends in a space (`Rabbids Hollywood `), which Wine rejects with
`Directory name invalid (0x10B)` — hence a local symlink view **[M]**
`packaging/games-status.md:233`, `:306`.

Two routes exist, and they are **not** the same thing:

| | Route A — TeknoParrot (fallback only) | Route B — `aagames` / unity-tcp (default) |
|---|---|---|
| Trigger | `play-halo.sh --profile RabbidsHollywood --teknoparrot` | `play-halo.sh --profile RabbidsHollywood` |
| Loader | our copy of `OpenParrotLoader64.exe` | none — `Game.exe` runs directly under GE-Proton |
| Core | closed `TeknoParrot64.dll` | none |
| In-game I/O | TeknoParrot core + JVS/I-O board emulation | BepInEx plugin in the game, TCP 127.0.0.1:33610 |
| Status | parked: game runs with the core loaded but never opens the JVS pipe, dies after 45–70 s **[M]** `packaging/games-status.md:237`, `:203` | default since 2026-10-09 **[M]** `packaging/decisions.md:274-288` |

The default route is Route B. `play-halo.sh` diverts the profile before any TeknoParrot code is
touched **[M]** `docs/loskoppelen-per-game.md:44-46`, `docs/research/loskoppelen-metingen.md:94-107`:

```bash
if [[ $USE_TP -eq 0 && $NATIVE -eq 0 && "$ROUTE" != tp-shm ]] && grep -qE "^[[:space:]]{4}\"$PROFILE\":[[:space:]]*\{" \
     "$ROOT/packaging/aagames-games.json"; then
  "$ROOT/scripts/play-unity.sh" "${UARGS[@]}"
```

**What the game does on its own, without any of them.**
- With TeknoParrot's loader + core but **no** dongle answer: reaches the title, logs
  `**** DONGLE NOT FOUND`, waits ~30 s, then `UnityEngine.Application.Quit` — ≈57 s total **[M]**
  `docs/matrix-layer.md:99-119`, `:211`.
- Without the board/plugin patches: `ArcadeManager.CheckIOBoard` reports "IO BOARD ERROR" and
  `ArcadeManager.GunFatalError` reports "Gun not found" and the game closes itself **[M]**
  `docs/decisions.md:276`, `:338`, `:350`.
- Truly bare (`Game.exe` with no `winhttp.dll`/BepInEx at all) is **not** measured **[I]**. Note the
  NAS folder is not bare: it ships a TeknoParrot BepInEx (doorstop `winhttp.dll`,
  `doorstop_config.ini`, `UnityPlugin_BepInEx_RHA.dll`). The aagames view deliberately excludes all of
  it **[R]** `scripts/aagames-setup.py:54-55`.

---

## 2. What already exists — ours vs TeknoParrot's

### Route B has zero TeknoParrot components in the start path **[M]**
`docs/loskoppelen-per-game.md:50-54`: *"Al los van TeknoParrot: 2 games … daar zit geen enkel
TeknoParrot-onderdeel meer in het startpad"*. Verified independently in the source **[R]**:

| Part | Whose | Where | Note |
|---|---|---|---|
| Process/loader | **ours** (i.e. Proton) | `scripts/play-unity.sh:106-113` | `proton run Game.exe -popupwindow`; no injection, no loader exe |
| Doorstop / BepInEx | **ours**, from upstream BepInEx | `scripts/aagames-setup.py:167-196` | BepInEx 5.4.21.0 + `winhttp.dll` + `doorstop_config.ini` from `~/.local/share/ravengun/bepinex/`, `WINEDLLOVERRIDES=winhttp=n,b` |
| In-game plugin | **ours** (`ravengun`) | `plugins/unity/src/rabbids/Rabbids.cs` | Route 2, default since `docs/decisions.md:329-346` |
| Wire protocol + client | **ours**, clean-room | `src/ravengun/games/aagames.{hpp,cpp}`, `src/ravengund/aagames_service.cpp` | 38-byte input for Rabbids, 61-byte framed output |
| I/O bridge dialect | **ours** | `src/ravengun/iobridge/unity_tcp.*` | thin wrapper over `AaClient`; pull dialect at 120 Hz |
| Daemon | **ours** | `ravengund` + `ravengunctl aagames` | TCP **client**; the plugin is the server |
| Launch scripts | **ours** | `play-unity.sh`, `aagames-setup.py` | |
| Game list / icons | **TeknoParrot's** | `UserProfiles/RabbidsHollywood.xml` | still the source for the Games list and icons **[M]** `docs/decisions.md:287` |
| GE-Proton binary | **TeknoParrot's path, no TP code** | `play-unity.sh:19` → `~/.local/share/TeknoParrotUI/proton/GE-Proton11-7-x86_64` | "alleen een opslagplek; geen TP-code" **[M]** `docs/research/eigen-runtime-inventaris.md:139` |
| `game.ini` metadata | **TeknoParrot's naming** | `[Game] Type=TeknoParrot`, `EmulationProfile=WartranTroopers`, `[Loader] PostStartMs=3000`, `[Wine] DllOverrides` | only meaningful for the `--teknoparrot` fallback **[R]** `game.ini:10-20` |

**Conclusions on ownership.** On Route B, *nothing* is TeknoParrot's except a directory name, a unit
name, and the profile used to draw the Games list. The loader is Proton; the injection is the BepInEx
doorstop (upstream BepInEx, ours to deploy); the I/O dialect is ours; the daemon is ours.

The one part on Route B that is **neither ours nor TeknoParrot's** is the *fallback* plugin: the
DemulShooter `UnityPlugin_BepInEx_RHA.dll` (assembly `UnityPlugin_BepInEx_RHA`, guid
`argonlefou.mouse.rha`, "RHA Mouse Plugin 1.0.0"), a third-party binary with **no licence** that is
deliberately not in the repo and only copied into a local view from a user-downloaded release
**[M]** `docs/decisions.md:279`, `docs/matrix-layer.md:244-252`, `docs/procedures.md:127`.

### The per-game patch content, and its dongle problem
`patches/RabbidsHollywood/` exists in the authoritative `ravengun-patches` tree with `game.ini`,
`README.md`, and `dropin/{dropin.ini, matrix64.dll (25,614 B), matrix/matrix.ini}` **[R]**. Its whole
subject is the game's own Matrix/SBK LPT dongle check — the one thing my brief excludes. State, for
the record only: `SBK.Matrix.MatrixManager.Awake` requires `Dongle_Count(85) > 0` and
`Dongle_ReadSerNr(..., 85) > 0`; with our DLL in `Game_Data/Plugins` the game survived 92/132/150 s
vs ≈57 s without **[M]** `docs/matrix-layer.md:92-119`, `:209-216`, and
`patches/RabbidsHollywood/README.md:55-94`.

---

## 3. What remains for our own loader/patch

Split into **loader** / **I/O dialect** / **packaging**. None of it is dongle design work; where a
step is blocked by a dongle decision it is marked **[OUT OF SCOPE]**.

### Loader
1. **Make `play-unity.sh` consume the drop-in engine.** Today `play-unity.sh:102` hardcodes
   `WINEDLLOVERRIDES=winhttp=n,b` and never reads `patches/<Game>/dropin/dropin.ini`; the drop-in
   machinery (`dropin_specs`, `dropin_plan`, `dropin_materialiseer`, `merge_env`) lives only in
   `scripts/launch-direct.py:348-542`, `:1115`, `:1520-1531` **[R]**. Consequence: on the Unity route
   `Env = RAVENGUN_MATRIX_DIR=matrix`, `RAVENGUN_MATRIX_DLL=1` and `DllOverride = native` are
   **never applied**, and `dropin.ini` is inert. Extract the drop-in mechanism into something both
   start paths call (the loader stays dongle-agnostic: it copies files and sets environment
   variables, `dropin.ini:8-11` **[R]**).
2. **Materialise `Game_Data` locally in the aagames view.** `aagames-setup.py:128` links
   `Game_Data` as a real directory but symlinks each child, so `Game_Data/Plugins` ends up a symlink to
   the NAS **[R]**. Any drop-in whose `To = Game_Data/Plugins` would therefore write to the NAS, which
   must never happen. `launch-direct.py` already solves this (`_maak_map_lokaal`); the aagames view does
   not. **[OUT OF SCOPE]** insofar as the only current consumer is `matrix64.dll`.
3. **Retire the TeknoParrot-shaped plumbing** (cosmetic but part of "no TeknoParrot component"):
   `play-unity.sh:19` (Proton under `~/.local/share/TeknoParrotUI/proton/`, already flagged for a move
   in `docs/research/eigen-runtime-inventaris.md:139`, `:219`) and `play-unity.sh:23`
   (`UNIT=ravengun-tpui`). Keep the shared unit name if the Games list and `--stop` depend on it, or
   rename deliberately.
4. **Decide the fate of the `--teknoparrot` fallback** for this profile. It is the only path that ever
   pulls in `OpenParrotLoader64.exe` + closed `TeknoParrot64.dll` **[M]** `docs/research/loskoppelen-metingen.md:60`.

### I/O dialect
5. **Nothing to build.** `aagames.*` is clean-room and complete: `to_pixels` calibration, 38-byte
   Rabbids input packet (`Axis_X f32[4]`, `Axis_Y f32[4]`, `EnableInputsHack u8`, `HideCrosshairs u8`,
   `Trigger u8[4]`), 61-byte framed output (`Ammo`, `Credits`, `Damaged`, `Life`, `Recoil`,
   `StartLamp` — note `StartLamp` is last for Rabbids), 120 Hz send, 1 s reconnect, recoil → rumble
   (`game_rumble_mask`) **[R]** `src/ravengun/games/aagames.hpp:8-19`, `:76-88`;
   `packaging/aagames-games.json:25-42`.
6. **Optional, deliberately deferred:** a protocol version handshake. `docs/decisions.md:336` records
   the choice to keep the daemon byte-compatible with the DemulShooter plugin so both plugins work.
7. **[OUT OF SCOPE]** The game-side plugin contract (`plugins/unity/src/rabbids/Rabbids.cs`) must also
   answer the Matrix calls. Verified present in the newest snapshot **[R]**:
   `MatrixInit_Prefix` → 0, `MatrixFind_Prefix` → 1, `MatrixCount_Prefix(short Port)` → 2,
   `MatrixSerNr_Prefix` → `0x10F2C`. An older snapshot has `MatrixCount_Prefix` → **1** and **no**
   `Dongle_ReadSerNr` patch, which is exactly the configuration measured at 57 s **[M]**
   `docs/matrix-layer.md:182-188`, `:212`. Do not ship the old one.

### Packaging
8. **Keep the JSON entry as the single source of truth** — `port`, `type: rabbids_hollywood`,
   `plugin`, `plugin_dir`, `language: EN`, 1920×1080, `y_origin: bottom`, `send_hz: 120`,
   `nas_dir` with its trailing space **[R]** `packaging/aagames-games.json:25-42`.
9. **Guarantee the patch tree is complete before committing.** The authoritative
   `ravengun-patches/patches/RabbidsHollywood/` has `game.ini` (2,093 B) and
   `dropin/matrix64.dll` (25,614 B); the copy found under the loader working directory is missing both
   **[R]**. `README.md:3` promises `game.ini`, so a commit out of the incomplete copy would ship a
   README pointing at a file that is not there.
10. **Refresh the stale per-game status text.** `game.ini:26-28` still says `Status = parked` /
    "stopt na ongeveer een minuut", and `findings/RabbidsHollywood.md:7` still says the own plugin dies
    after 50–70 s, while the 2026-10-11 measurements show >150 s (drop-in) and >200 s (plugin patch)
    **[M]** `docs/matrix-layer.md:213-216`, `patches/RabbidsHollywood/README.md:64-67`. Also update
    `packaging/games-status.md:271-272` and `docs/compatibility.md:48`.
11. **Reconcile the plugin default with the docs.** `packaging/aagames-games.json` says
    `"plugin": "ravengun"` **[R]** and `docs/architecture.md:92` makes route 2 the default, but
    `packaging/games-status.md:271` still instructs `plugin: "demulshooter"` for this game, and the
    live Rabbids measurement at `packaging/games-status.md:309` was taken with the **DemulShooter**
    plugin loaded. Pick one and make the three files agree.
12. **Account for the *other* view.** Route B uses
    `~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood`; the Matrix/dongle work was
    measured in `~/.local/share/ravengun/runtime/views/RabbidsHollywood` via `launch-direct.py`
    **[M]** `patches/RabbidsHollywood/README.md:64-70`, `docs/research/loskoppelen-metingen.md:150-162`.
    Two views exist for one game; only the `views-aagames` one belongs to Route B.

---

## 4. The exact next measurement

**Question it settles:** does the *TeknoParrot-free* Route B, with **our own** plugin, get past the
game's ~57 s dongle deadline and behave — i.e. is the "it works live" claim true for the route we
actually ship? Every existing longevity measurement (57 s, 92 s, 132 s, 150 s, >200 s) was taken with
`scripts/launch-direct.py RabbidsHollywood --run`, i.e. the **TeknoParrot-loader route in
`views/RabbidsHollywood`** **[M]** `docs/matrix-layer.md:206`, `patches/RabbidsHollywood/README.md:64-66`.
The only Route-B longevity data is §5 below and predates the plugin fix.

**Commands**

```bash
# 0. clean slate — nothing of TeknoParrot may be running
pgrep -af 'TeknoParrot|OpenParrotLoader|pipehelper' || echo "geen TeknoParrot-proces"

# 1. start Route B with OUR plugin (the default), from a terminal outside the unit
RAVENGUN_UNITY_PLUGIN=ravengun scripts/play-halo.sh --profile RabbidsHollywood

# 2. prove no TeknoParrot component is in the game's cgroup
cg=$(systemctl --user show -p ControlGroup --value ravengun-tpui.service)
for p in $(cat "/sys/fs/cgroup$cg/cgroup.procs"); do cat "/proc/$p/comm"; done | sort -u

# 3. daemon side
ravengunctl status
ravengunctl status --json            # -> .aagames.packets_per_s, .output_frames, per player ammo/life

# 4. coin, start
ravengunctl key 5; ravengunctl key 5; ravengunctl key 5; ravengunctl key 5
ravengunctl key 1

# 5. crosshair + shot + rumble, still without restarting the daemon
ravengun-aagames-probe --game RabbidsHollywood --source fixed --u 0.3 --v 0.3 --trigger --seconds 20
ravengun-aagames-probe --game RabbidsHollywood --source ipc --seconds 30

# 6. the actual test: leave it running and check at 60 s / 120 s / 180 s
sleep 180; systemctl --user is-active ravengun-tpui.service

# 7. stop
scripts/play-halo.sh --stop
```

**What counts as success** (all of these):

1. **No TeknoParrot process anywhere** — step 0 prints nothing, and step 2 lists only wine/Proton
   processes and `Game.exe`; no `TeknoParrotUi`, no `OpenParrotLoader64.exe`, no `pipehelper.exe`.
2. **Our plugin loaded, not DemulShooter** — `~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/BepInEx/LogOutput.log`
   contains `Loading [RavenGun` , `TCP-server luistert op 127.0.0.1:33610`, `Daemon verbonden.`,
   `Invoer van de daemon actief.`, and **no** `DemulShooter`
   (expected strings: `docs/procedures.md:147`).
3. **The plugin answers the dongle** — the same log shows `Matrix: Dongle_Count(85) -> 2` **and**
   `Matrix: Dongle_ReadSerNr -> 69420`. If only `-> 1` appears, or `ReadSerNr` is missing, the old
   `Rabbids.cs` is in the view and the run is invalid.
4. **Daemon connected at the configured rate** — `ravengunctl status` shows the "Unity-game" line as
   connected at ≈120 packets/s, and `stage 3 --json` gives `aagames.connected = true` with
   `packets_per_s ≈ 120` and rising `output_frames` (the claim under test, `packaging/games-status.md:103`).
5. **Crosshair follows aim** — with `--source fixed --u 0.3 --v 0.3` the in-game crosshair sits at
   **576/324 px of 1920×1080** (Y origin bottom; `docs/matrix-layer.md` neighbours and
   `packaging/games-status.md:311`).
6. **Coins and start** — four `key 5` produce credits 4 and `key 1` starts; `StartLamp` for P1 goes on
   and credits drop to 0 (`packaging/games-status.md:312`).
7. **Shots count and the Move rumbles** — the probe's `recoil` counter rises and
   `rumble-pulsen` increments; on the real Move each game shot is felt.
8. **The game is still alive at 180 s** — `is-active` is `active`. This is the decisive number: it
   crosses the ≈57 s dongle deadline and the 70 s ceiling previously observed on this very view with
   the own plugin.

**If it fails at ~57–70 s**, the failure is the dongle answer and belongs to whoever owns the Matrix
decision **[OUT OF SCOPE]** — capture, for them:
`views-aagames/RabbidsHollywood/BepInEx/LogOutput.log` (the `Matrix:` lines) plus
`ravengun-aagames-probe`'s exit JSON. Also note `docs/matrix-layer.md:92-94` records one unrelated
loader failure (`Failed to Load DLL via RemoteThread! Error 2b`) that killed a start — so a single
failed run is not conclusive.

---

## 5. How much of the "it works live" claim I could verify

The claim is verbatim in the project, at **`packaging/games-status.md:103`**:

> "Unity-route (Tomb Raider, Rabbids) draait nu als dialect unity-tcp van de brug: Rabbids Hollywood
> live gecontroleerd (verbonden, 120 pakketten/s, vizier, munten en start, schoten tellen mee voor de
> trilling)."

Verdict: **the claim is genuine and traceable, but it is narrower than it reads.** Four caveats,
all measured from the files:

1. **It was not achieved with our plugin.** The supporting detail in the same document,
   `packaging/games-status.md:308-313`, says "*Live gemeten (zelfde methode)*" and then
   `:309`: "`BepInEx/LogOutput.log`: BepInEx 5.4.21 onder Unity 2017.2, **Loading
   [RabbidsHollywood_BepInEx_DemulShooter_Plugin 17.0.0.0]**". The 120 packets/s, crosshair, coins,
   start and recoil-rumble for Rabbids were observed through the **third-party DemulShooter** plugin.
   This is corroborated by `findings/RabbidsHollywood.md:6,25,28` ("met de BepInEx-plugin van
   DemulShooter als terugval"; "Tot dan: `plugin: "demulshooter"`"). It directly contradicts
   `packaging/games-status.md:262`, which asserts that both games' logs show `[RavenGun ...]` and no
   DemulShooter.
2. **The same document records that our own plugin did not hold.** `:271-272`: with our plugin the game
   "komt tot het kiezen van de wereld" then quits itself after 50–70 s; four runs died inside 70 s,
   while the DemulShooter plugin kept it alive 80 s and 229 s in two runs, cause unexplained.
   `:272` states explicitly that crosshair, shooting and rumble *in a running level* were **not**
   assessed with our plugin. `findings/RabbidsHollywood.md:33` repeats that.
3. **The daemon side was never tested live.** `docs/architecture.md:76`: "de protocolkant, de klant en
   de probe zijn live op echte hardware getest (beide games), de daemonkant (`AagamesService`) alleen
   met nep-apparaten en een nep-plugin". The measurements were taken with
   `ravengun-aagames-probe` run **against a not-yet-restarted daemon** (`packaging/games-status.md:247`),
   i.e. bypassing `AagamesService` itself.
4. **Our own plugin is now the default, and it is the *untested* configuration.** Since
   `packaging/aagames-games.json` says `"plugin": "ravengun"` **[R]**, the configuration the docs
   declare "live gecontroleerd" is the one they also describe as dying inside 70 s. Meanwhile the
   measurements that show the game surviving (>150 s, >200 s) were taken on the **TeknoParrot-loader
   route** (`launch-direct.py`, `views/RabbidsHollywood`) **[M]**
   `patches/RabbidsHollywood/README.md:64-67`. No run has demonstrated the shipped configuration —
   Route B, `views-aagames`, our plugin — surviving the dongle deadline.

**Score: partially verified.** Verified in the files: the protocol implementation is real, clean-room
and live-tested; the TCP client, the 38-byte Rabbids packet, the `StartLamp` field ordering, the coin/
start keys, the 120 packets/s rate and the recoil→rumble path are all recorded with concrete numbers.
Not verified: that this works on the route that actually ships, with our own plugin, for longer than a
minute. That is precisely what §4 measures.

---

## 6. Files consulted

`packaging/aagames-games.json` · `packaging/games-status.md` · `packaging/decisions.md` (`docs/decisions.md`) ·
`docs/architecture.md` · `docs/loskoppelen-per-game.md` · `docs/matrix-layer.md` · `docs/procedures.md` ·
`docs/compatibility.md` · `docs/todo.md` · `docs/per-game-stand.md` ·
`docs/research/demulshooter-integratie.md` · `docs/research/loskoppelen-metingen.md` ·
`docs/research/zonder-teknoparrot.md` · `docs/research/eigen-runtime-inventaris.md` ·
`docs/research/teknoparrot-io.md` · `docs/research/findings-digest.md` ·
`findings/RabbidsHollywood.md` · `patches/RabbidsHollywood/{README.md, game.ini, dropin/dropin.ini,
dropin/matrix/matrix.ini}` · `scripts/play-unity.sh` · `scripts/aagames-setup.py` · `scripts/matrix.py` ·
`scripts/launch-direct.py` · `src/ravengun/games/aagames.hpp` · `src/ravengund/aagames_service.cpp` ·
`src/tools/aagames_probe/aagames_probe.cpp` · `plugins/unity/src/rabbids/Rabbids.cs` (two snapshots) ·
`matrix/src/*`.

### Limitation
The Deck paths the assignment names (`~/projects/ravengun`, `~/projects/ravengun-patches`, the NAS
mount) are not reachable from this Windows bench; the survey above was done against the workspace
mirrors. Where two mirrors disagree, I state both and treat
`ravengun-patches` + the newest `matrix_work` mirror as authoritative. No file on the NAS was written,
and no game was started.

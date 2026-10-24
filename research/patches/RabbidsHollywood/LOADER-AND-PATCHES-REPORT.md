# Rabbids Hollywood — loader & patches state (report)

**Revision 2 — 2026-10-12.** Author: per-game loader specialist (delegated agent). Revision 1 was the
first survey; revision 2 folds in the fresh rollout measurement (`DONGLE-ONDERZOEK.md`, by the dongle
specialist, 2026-10-12) and updates the open-points list **in place** rather than adding a second list.

Scope rules honoured: the NAS was only read; no dongle/HASP/keychip/integrity work was done by this
agent; the game was NOT started by this agent; no git commit was made.

Evidence tags: **[M]** = measured and recorded in a project file (quoted with path:line) · **[R]** =
read from source/config · **[I]** = inferred, not measured.

### Revision 2 — what changed

| Was (rev 1) | Now |
|---|---|
| "our own plugin dies in 50–70 s, cause unexplained; DemulShooter held 229 s" | **Explained: a rollout problem.** The shipped view carried a plugin from *before* the Matrix fix. With the fresh plugin self-deployed by `play-unity.sh` the game lives **288 s** on the shipped route. There is no plugin-vs-DemulShooter mystery left. |
| Longevity was unproven on the shipped route | **Proven** — 288 s, 0× `Application.Quit`, operator-initiated stop. |
| Coins/start/crosshair implicitly assumed working | **Not exercised at all** in the 288 s run (headless gamescope receives no keys, and the probe fights the daemon for the plugin's single client). Requires a run on the TV. |
| Savegame encryption not mentioned | **New open point**: `Dongle_EncryptData`/`Dongle_DecryptData` are unanswered, so the game resets its savegames on every start. |

**Wording discipline:** this game is **not** called "playable" anywhere in this report. What is proven
is *"starts and stays alive"*; what is not proven is *"coins, start and crosshair work"*.

---

## 1. What the game is, and which route it needs

Adrenaline Amusements, 2018, Unity 2017.2, 64-bit Mono. Entry point `Game.exe` (650,752 B, sha256
`979f9736...8da95d`) **[R]** `patches/RabbidsHollywood/game.ini:33-35`. The NAS folder name ends in a
space (`Rabbids Hollywood `), which Wine rejects with `Directory name invalid (0x10B)` — hence a local
symlink view **[M]** `packaging/games-status.md:233`, `:306`.

Two routes exist, and they are **not** the same thing:

| | Route A — TeknoParrot (fallback only) | Route B — `aagames` / unity-tcp (default, "shipped") |
|---|---|---|
| Trigger | `play-halo.sh --profile RabbidsHollywood --teknoparrot` | `play-halo.sh --profile RabbidsHollywood` |
| Loader | our copy of `OpenParrotLoader64.exe` | none — `Game.exe` runs directly under GE-Proton |
| Core | closed `TeknoParrot64.dll` | none |
| In-game I/O | TeknoParrot core + JVS/I-O board emulation | BepInEx plugin in the game, TCP 127.0.0.1:33610 |
| View | `views/RabbidsHollywood` | `views-aagames/RabbidsHollywood` |
| Status | parked: the game runs with the core loaded but never opens the JVS pipe, dies after 45–70 s **[M]** `packaging/games-status.md:237`, `:203` | **starts and stays alive** — 288 s measured 2026-10-12 **[M]** `DONGLE-ONDERZOEK.md:15-37` |

Route B is the default since 2026-10-09 **[M]** `docs/decisions.md:274-288`. `play-halo.sh` diverts the
profile before any TeknoParrot code is touched **[M]** `docs/loskoppelen-per-game.md:44-46`,
`docs/research/loskoppelen-metingen.md:94-107`:

```bash
if [[ $USE_TP -eq 0 && $NATIVE -eq 0 && "$ROUTE" != tp-shm ]] && grep -qE "^[[:space:]]{4}\"$PROFILE\":[[:space:]]*\{" \
     "$ROOT/packaging/aagames-games.json"; then
  "$ROOT/scripts/play-unity.sh" "${UARGS[@]}"
```

### 1a. The ~70 s death was a rollout problem, not a game or route fault **[M]**

This is the single most important correction, and it invalidates the older framing in several project
documents. Evidence, all from `DONGLE-ONDERZOEK.md:69-99`:

| Moment | View DLL | Build DLL |
|---|---|---|
| 21:56:54, before the start | 32,768 B, md5 `b4fe9d83…`, mtime 2026-10-11 17:12 | 34,304 B, md5 `29fd3ddc…` |
| 21:58:19, after `play-halo.sh` | 34,304 B, md5 `29fd3ddc…` — byte-identical to the build | idem |

- The stale 32,768 B DLL contained **no Matrix code at all** — no `MatrixInit_Prefix`,
  `MatrixFind_Prefix`, `MatrixCount_Prefix`, `MatrixSerNr_Prefix`, and no `Dongle_Count` /
  `Dongle_ReadSerNr` strings in the metadata **[M]**.
- The Matrix fix landed in `plugins/unity/src/rabbids/Rabbids.cs` at 19:53 and existed **only** in
  `build/unity-plugin/RabbidsHollywood/RavenGun.RabbidsHollywood.dll` and in the **non-shipped** view
  `views/RabbidsHollywood` **[M]**.
- Route B had not been started since 17:13, so it **never ran with the dongle fix** **[M]**.
- The old log shows the failure plainly: **zero `Matrix:` lines**, `[Info : Unity Log] **** DONGLE NOT
  FOUND`, last line `OnChooseWorldEnter` — exactly the "reaches world select, dies within 50–70 s"
  symptom **[M]**.

With the fresh DLL the operator's run (`rg-rabbids-run.service`, 2026-10-12 21:59:34 → 22:04:37) gave:
`Game.exe` alive at t=288 s, `grep -c 'Application.Quit'` = **0**, `dongle_found=1` every 10 s, last log
lines the continuing attract loop (`BotPlayer ::: CollectInfo`), and the daemon still reporting
`Rabbids Hollywood Arcade: VERBONDEN met de game, 120 pakketten/s` at the stop moment **[M]**
`DONGLE-ONDERZOEK.md:15-37`.

The dongle request chain was answered **by the plugin, not by `matrix64.dll`** — the drop-in route was
not used on the shipped route at all **[M]** `DONGLE-ONDERZOEK.md:60-67`:

| # | Call | Game's requirement | Our answer | Proof |
|---|---|---|---|---|
| 0 | `MatrixWrapper.Init_MatrixAPI` | `< 0` = error | **0** | `Matrix: Init_MatrixAPI -> 0` |
| 1 | `MatrixWrapper.Dongle_Count(85)` | `> 0` | **2** | `Matrix: Dongle_Count(85) -> 2` |
| 2 | `MatrixWrapper.Dongle_ReadSerNr(…, 85)` | `> 0` | **69420** | `Matrix: Dongle_ReadSerNr -> 69420` |
| 3 | *(the game itself)* | logs `**** DONGLE FOUND: <n>` | — | `[Info : Unity Log] **** DONGLE FOUND: 69420` |

`Dongle_Find` is patched but was **never called** — the start path does not use it, so that patch is
defensive only **[M]**.

**Consequence for older documents.** `packaging/games-status.md:271-272`,
`findings/RabbidsHollywood.md:7,23,25` and `docs/matrix-layer.md:242-262` describe the 50–70 s death and
the unexplained DemulShooter-vs-own-plugin difference (229 s vs 4× <70 s). Both are now explained by
stale-vs-fresh DLL. Item 11 below (`plugin: "demulshooter"` as the recommendation) therefore **falls
away**, and `docs/matrix-layer.md:242-262` §8 ("En de vraag waarom DemulShooter's plugin de game 229 s in
leven hield") should be closed with this answer rather than left open.

### 1b. What the game does on its own

- With the TeknoParrot loader + core but **no** dongle answer: title, `**** DONGLE NOT FOUND`, ~30 s wait,
  `UnityEngine.Application.Quit` — ≈57 s total **[M]** `docs/matrix-layer.md:99-119`, `:211`.
- Without the board/plugin patches: `ArcadeManager.CheckIOBoard` → "IO BOARD ERROR";
  `ArcadeManager.GunFatalError` → "Gun not found" and the game closes itself **[M]**
  `docs/decisions.md:276`, `:338`, `:350`.
- Truly bare (`Game.exe` with no `winhttp.dll`/BepInEx at all) is still **not measured [I]**. The NAS
  folder is not bare: it ships a TeknoParrot BepInEx (doorstop `winhttp.dll`, `doorstop_config.ini`,
  `UnityPlugin_BepInEx_RHA.dll`), which the aagames view deliberately excludes **[R]**
  `scripts/aagames-setup.py:54-55`.

---

## 2. What already exists — ours vs TeknoParrot's

On Route B there are **zero TeknoParrot components in the start path** **[M]**
`docs/loskoppelen-per-game.md:50-54`, and the 288 s run confirms it *by measurement* rather than by
reading the start script — the transient unit's cgroup held only `python3`, `steam.exe`, `wineserver`,
`services.exe`, `winedevice.exe`, `plugplay.exe`, `svchost.exe`, `Game.exe`, `explorer.exe`, `rpcss.exe`,
`tabtip.exe`, `xalia.exe`, with **no** `TeknoParrotUi`, `OpenParrotLoader64` or `pipehelper` **[M]**
`DONGLE-ONDERZOEK.md:213-217`.

| Part | Whose | Where | Note |
|---|---|---|---|
| Process/loader | **ours** (i.e. Proton) | `scripts/play-unity.sh:106-113` | `proton run Game.exe -popupwindow`; no injection, no loader exe |
| Doorstop / BepInEx | **ours**, from upstream BepInEx | `scripts/aagames-setup.py:167-196` | BepInEx 5.4.21.0 + `winhttp.dll` + `doorstop_config.ini`, `WINEDLLOVERRIDES=winhttp=n,b` |
| In-game plugin | **ours** (`ravengun`) | `plugins/unity/src/rabbids/Rabbids.cs` | route 2; now includes the Matrix answers |
| Wire protocol + client | **ours**, clean-room | `src/ravengun/games/aagames.{hpp,cpp}`, `src/ravengund/aagames_service.cpp` | 38-byte input for Rabbids, 61-byte framed output |
| I/O bridge dialect | **ours** | `src/ravengun/iobridge/unity_tcp.*` | thin wrapper over `AaClient`; pull dialect at 120 Hz |
| Daemon | **ours** | `ravengund` + `ravengunctl aagames` | TCP **client**; the plugin is the server |
| Launch scripts | **ours** | `play-unity.sh`, `aagames-setup.py` | |
| Game list / icons | **TeknoParrot's** | `UserProfiles/RabbidsHollywood.xml` | still the source for the Games list and icons **[M]** `docs/decisions.md:287` |
| GE-Proton binary | **TeknoParrot's path, no TP code** | `play-unity.sh:19` → `~/.local/share/TeknoParrotUI/proton/GE-Proton11-7-x86_64` | "alleen een opslagplek; geen TP-code" **[M]** `docs/research/eigen-runtime-inventaris.md:139` |
| `game.ini` metadata | **TeknoParrot's naming** | `[Game] Type=TeknoParrot`, `EmulationProfile=WartranTroopers`, `[Loader] PostStartMs`, `[Wine] DllOverrides` | only meaningful for the `--teknoparrot` fallback **[R]** `game.ini:10-20` |

The one part on Route B that is **neither ours nor TeknoParrot's** is the *fallback* plugin: the
DemulShooter `UnityPlugin_BepInEx_RHA.dll` (assembly `UnityPlugin_BepInEx_RHA`, guid
`argonlefou.mouse.rha`), a third-party binary with **no licence**, deliberately not in the repo, copied
into a local view only from a user-downloaded release **[M]** `docs/decisions.md:279`,
`docs/matrix-layer.md:244-252`, `docs/procedures.md:127`. Since the own plugin now works, this fallback no
longer has a functional justification for this game — only a licensing-preference one.

---

## 3. What remains — consolidated list (updated in place)

Items 1–12 are revision 1's list, each re-checked against the rollout measurement and tagged:

- `[STANDS]` — unaffected by the new evidence.
- `[SHARPER]` — still valid, now with harder evidence or a wider scope.
- `[FALLS AWAY]` — the new evidence retires it.
- `[NEW]` — added in revision 2.

The eight numbered items in the revision-1 hand-off message map onto items **1, 2, 3, 5, 9, 10, 11, 12**
here; items 4, 6 and 7 were already in the report file.

### Loader

1. **Make `play-unity.sh` consume the drop-in engine.** `[SHARPER]` `play-unity.sh:102` hardcodes
   `WINEDLLOVERRIDES=winhttp=n,b` and never reads `patches/<Game>/dropin/dropin.ini`; the drop-in
   machinery (`dropin_specs`, `dropin_plan`, `dropin_materialiseer`, `merge_env`) lives only in
   `scripts/launch-direct.py:348-542`, `:1115`, `:1520-1531` **[R]**. The rollout measurement makes the
   asymmetry explicit and costly: the same start path **does** self-deploy the *plugin*
   (`play-unity.sh:72-75` → `aagames-setup.py` → `install_ravengun` copies `build/unity-plugin/<Game>/…`
   into the view, proven byte-identical **[M]** `DONGLE-ONDERZOEK.md:78-80`) but never the *drop-in*.
   Two different propagation mechanisms, one of which silently does nothing. Extract the drop-in
   mechanism into something both start paths call (the loader stays dongle-agnostic: it copies files and
   sets environment variables, `dropin.ini:8-11` **[R]**).
2. **Materialise `Game_Data` locally in the aagames view.** `[SHARPER]` `aagames-setup.py:128` links
   `Game_Data` as a real directory but symlinks each child, so `Game_Data/Plugins` is **a symlink to the
   NAS**. Confirmed in the 288 s run: `views-aagames/RabbidsHollywood/Game_Data/Plugins/matrix64.dll` is
   still the **original 93,184 B** (md5 `c86908ab10b1d9249c896e42b7618f67`), not our 25,614 B drop-in
   **[M]** `DONGLE-ONDERZOEK.md:64-67`, `:97-98`. Any drop-in with `To = Game_Data/Plugins` would write
   to the NAS. Also [SHARPER] because the mount is **CIFS `rw`** (`findmnt`), so "the NAS was not
   written" is a *behavioural* statement, not a filesystem guarantee **[M]** `DONGLE-ONDERZOEK.md:156-168`.
3. **Retire the TeknoParrot-shaped plumbing.** `[SHARPER]` upgraded from inference to measurement: the
   cgroup proof in §2 shows no TP process is involved. Remaining cosmetics: `play-unity.sh:19` (Proton
   under `~/.local/share/TeknoParrotUI/proton/`, already flagged in
   `docs/research/eigen-runtime-inventaris.md:139`, `:219`) and `play-unity.sh:23`
   (`UNIT=ravengun-tpui`).
4. **Decide the fate of the `--teknoparrot` fallback** for this profile. `[STANDS]` It is the only path
   that ever pulls in `OpenParrotLoader64.exe` + closed `TeknoParrot64.dll` **[M]**
   `docs/research/loskoppelen-metingen.md:60`.

### I/O dialect

5. **The protocol and client need no work.** `[SHARPER]` `aagames.*` is clean-room and complete:
   `to_pixels` calibration, the 38-byte Rabbids input packet (`Axis_X f32[4]`, `Axis_Y f32[4]`,
   `EnableInputsHack u8`, `HideCrosshairs u8`, `Trigger u8[4]` — no `Reload`), the 61-byte framed output
   (`Ammo`, `Credits`, `Damaged`, `Life`, `Recoil`, `StartLamp`, with `StartLamp` last for Rabbids),
   120 Hz send, 1 s reconnect, recoil → rumble **[R]** `src/ravengun/games/aagames.hpp:8-19`, `:76-88`;
   `packaging/aagames-games.json:25-42`. Sharper because the end-to-end path is now proven for 288 s at
   "120 pakketten/s" **[M]** `DONGLE-ONDERZOEK.md:32-33`. *One observation to confirm during the TV run
   **[I]**: the same status line read "1 uitvoerberichten". Confirm whether the plugin sends output
   frames only on change (expected) or whether the output path was mostly idle.*
6. **Optional, deliberately deferred: a protocol version handshake.** `[STANDS]`
   `docs/decisions.md:336` records the choice to stay byte-compatible with the DemulShooter plugin.
7. **The game-side plugin contract.** `[SHARPER]` The board/input/recoil patches
   (`ArcadeManager.CheckIOBoard`, `ArcadeManager.GunFatalError`, `NewInputManager.MouseControl`,
   `KaboomManager.StartMotorPwm`, `SetPlayerLight`/`ClearPlayerLight`) are unchanged, and the Matrix part
   is now proven working (`MatrixInit_Prefix` → 0, `MatrixCount_Prefix(short Port)` → 2,
   `MatrixSerNr_Prefix` → `0x10F2C`) **[M]** `DONGLE-ONDERZOEK.md:48-58`. Two consequences:
   - An older `Rabbids.cs` snapshot has `MatrixCount_Prefix` → **1** and **no** `Dongle_ReadSerNr` patch
     — exactly the 57 s configuration **[M]** `docs/matrix-layer.md:182-188`, `:212`. Do not ship it, and
     see item 13 for how to make that impossible to miss.
   - `Dongle_Find` is patched but unused on the start path; keep it as defence, do not rely on it.

### Packaging

8. **Keep the JSON entry as the single source of truth.** `[STANDS]` `port`, `type: rabbids_hollywood`,
   `plugin`, `plugin_dir`, `language: EN`, 1920×1080, `y_origin: bottom`, `send_hz: 120`, and `nas_dir`
   with its trailing space **[R]** `packaging/aagames-games.json:25-42`.
9. **Guarantee the patch tree is complete before committing.** `[SHARPER]` The authoritative
   `ravengun-patches/patches/RabbidsHollywood/` has `game.ini` (2,093 B), `README.md`, and
   `dropin/{dropin.ini, matrix64.dll (25,614 B), matrix/matrix.ini}`; the copy found under the loader
   working directory is missing `game.ini` **and** `matrix64.dll` **[R]**, while its `README.md:3`
   promises `game.ini`. Sharper because missing/stale artefacts are now the proven root cause of a
   week of "the fix does not work": a completeness and identity check (size **and** md5, not mtime)
   belongs in the tree, not in a reviewer's head.
10. **Refresh the stale per-game status text.** `[SHARPER]` with concrete numbers: `game.ini:26-28` still
    says `Status = parked` / "stopt na ongeveer een minuut"; `findings/RabbidsHollywood.md:7` says our
    plugin dies at 50–70 s; `packaging/games-status.md:271-272` and `docs/compatibility.md:48` repeat it.
    All must be replaced by: the earlier death was a **rollout problem** (stale view DLL), and the shipped
    route lives **288 s**. Also update `packaging/games-status.md:103` and §5 below. The same file list
    appears in `DONGLE-ONDERZOEK.md:236-237`.
11. **Reconcile the plugin default.** `[FALLS AWAY as a question — becomes a cleanup]` The evidence now
    settles it in favour of `ravengun`: `packaging/aagames-games.json` says `"plugin": "ravengun"` **[R]**
    and `docs/architecture.md:92` makes route 2 the default, while `packaging/games-status.md:271` still
    instructs `plugin: "demulshooter"` for this game and `findings/RabbidsHollywood.md:25` repeats it
    ("Tot dan: …"). That instruction is obsolete — the own plugin ran 288 s. Remove it; keep
    `plugin: "ravengun"`. The fallback stays available but is no longer the recommendation.
12. **Fix the two-view split.** `[SHARPER]` Route B uses
    `~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood`; all the `launch-direct.py`
    longevity runs happened in `~/.local/share/ravengun/runtime/views/RabbidsHollywood` **[M]**
    `docs/matrix-layer.md:206`, `patches/RabbidsHollywood/README.md:64-66`. Sharper because the old view
    is not merely redundant but **unsafe**: there `ControllerBaseConfig.json` is a symlink to the NAS, so
    the game writes through to it, while the shipped `views-aagames` view keeps a local copy **[M]**
    `DONGLE-ONDERZOEK.md:161-164`. Since the mount is CIFS `rw`, that is a real NAS write from a
    non-shipped path.

### Added in revision 2

13. **`[NEW]` Make a stale artefact impossible to miss.** `aagames-setup.py`'s `plugin_stale()` decides
    freshness by comparing the DLL's mtime against the newest `.cs` mtime **[R]**
    `aagames-setup.py:153-157`, then `install_ravengun` copies the built DLL in unconditionally
    `:191`. That worked here, but only because a human diffed sizes and md5s afterwards. Add a size +
    md5 comparison of the view DLL against the build (in the start path or as a `--check` mode) so a
    lagging view fails loudly instead of producing a mysterious 70 s death. Same check for the drop-in
    artefacts of item 9.
14. **`[NEW]` Guard `RAVENGUN_MATRIX_DLL`.** It disables the plugin's Matrix patches **blindly** **[M]**
    `DONGLE-ONDERZOEK.md:134-138`. On the shipped route there is no drop-in, and `dropin.ini` is not read
    there, so if that variable ever reaches the environment the game falls back to the original
    `matrix64.dll` and dies again at ~70 s with no obvious cause. Make the switch self-checking: only
    honour it when the drop-in is actually present in the view, and log/refuse otherwise.
15. **`[NEW]` Cover the savegame family — the one dongle path we do not answer.** `SBK.Matrix.MatrixManager`
    calls `Dongle_EncryptData` / `Dongle_DecryptData` at **eight** sites (the savegame encryption). The
    plugin does not patch them, so on the shipped route they go to the **original NAS `matrix64.dll`**
    with no dongle. Observed consequence in the 288 s run **[M]** `DONGLE-ONDERZOEK.md:102-132`:

    ```
    [Info   : Unity Log] Bad PKCS7 padding. Invalid length 41.
    [Warning: Unity Log] WARNING : Savegame corrupted : MetricManager will be resetted
    [Info   : Unity Log] Bad PKCS7 padding. Invalid length 249.
    [Warning: Unity Log] WARNING : Savegame corrupted : OperatorMenu will be resetted
    [Info   : Unity Log] Bad PKCS7 padding. Invalid length 173.
    [Warning: Unity Log] WARNING : Savegame corrupted : SBK.OptionsManager will be resetted
    ```

    The game did not crash, but **it resets its savegames on every start**. Current status: repair in
    progress, not finished, not measured. Two candidate routes, both still to be measured —
    (a) answer the pair in the plugin as a no-op (return 0, leave the buffer untouched), which is exactly
    what our `matrix64.dll` already does, and which changes the measured artefact so it needs a fresh run;
    or (b) actually roll the drop-in out on the shipped route so the DLL covers both paths, which also
    forces item 2. Note the reason this did not bite in the 288 s run: the run was short and stayed in
    attract/world-select, so it cannot say whether it bites later (operator settings, metrics, a save/load
    during play). `Dongle_ReadData` / `Dongle_WriteData` / `Dongle_MemSize` do **not** occur in the
    assembly, so no dongle image is needed **[R]** `patches/RabbidsHollywood/README.md:115-117`.

---

## 4. The exact next measurement

Revision 1 asked whether the shipped route survives the dongle deadline. **That is now answered: yes,
288 s.** What is left is the part the headless run could not exercise, plus the savegame repair. Two
tooling corrections from `DONGLE-ONDERZOEK.md:172-184` apply to revision 1's command list:

- **`ravengun-aagames-probe` is a second TCP client, not an injector.** The plugin keeps **one** client,
  so connecting the probe while the daemon is connected causes connect/disconnect churn — 10× connect /
  10× disconnect with `last_error: "de game sloot de verbinding"` **[M]**. Revision 1's step 5
  (crosshair check via the probe) **must not run alongside a connected daemon**: stop the daemon first, or
  drive the crosshair through the daemon itself.
- **Headless gamescope receives no keys.** `ravengunctl key` injects via uinput into the **owner's**
  compositor; a headless gamescope has no such window, so the four `key 5` and the `key 1` never reached
  the game — exactly **one** `Stand:` line in the whole log, credits 0 **[M]**. Coins, start and the
  crosshair therefore require a **run on the TV**.

### Commands (TV run — this is the measurement that is actually outstanding)

```bash
# 0. bench free, and nothing of TeknoParrot
pgrep -af 'wine|Game.exe|OpenParrot|TeknoParrot|pipehelper' || echo "bench leeg"

# 1. shipped route, own plugin, on the TV (no headless gamescope)
RAVENGUN_UNITY_PLUGIN=ravengun scripts/play-halo.sh --profile RabbidsHollywood --no-wait-move

# 2. self-deploy proof: view DLL == build DLL (size and md5, not mtime)
V=~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/BepInEx/plugins/RavenGun.RabbidsHollywood.dll
md5sum "$V" ~/projects/ravengun/build/unity-plugin/RabbidsHollywood/RavenGun.RabbidsHollywood.dll

# 3. no TeknoParrot in the cgroup
cg=$(systemctl --user show -p ControlGroup --value ravengun-tpui.service)
for p in $(cat "/sys/fs/cgroup$cg/cgroup.procs"); do cat "/proc/$p/comm"; done | sort -u

# 4. dongle answers + the game's acceptance, and the savegame warnings
L=~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/BepInEx/LogOutput.log
grep -a -E 'Matrix:|DONGLE|Savegame|PKCS7' "$L"

# 5. coins and start — on the TV these DO reach the game
ravengunctl key 5; ravengunctl key 5; ravengunctl key 5; ravengunctl key 5; ravengunctl key 1

# 6. crosshair: aim through the DAEMON, not through the probe
ravengunctl test-pattern point 0.3 0.3        # or the app's test pattern
ravengunctl status                            # "Unity-game ... VERBONDEN ... ~120 pakketten/s"

# 7. stop after the observation window (~5 min)
scripts/play-halo.sh --stop
```

### What counts as success

**Already proven (revision 1's bar), do not re-litigate** — but a second 288 s run would strengthen a
single-run result **[M]** `DONGLE-ONDERZOEK.md:230-231`:

- no TeknoParrot process in the cgroup (step 3): only wine/steam/`Game.exe`;
- both `Matrix: Dongle_Count(85) -> 2` and `Matrix: Dongle_ReadSerNr -> 69420`, and the game's own
  `**** DONGLE FOUND: 69420`, with 0× `DONGLE NOT FOUND` and 0× `Application.Quit`;
- daemon connected at ≈120 packets/s;
- alive past 180 s.

**Outstanding — success means all of these:**

1. **Coins and start.** Four `key 5` produce credits 4 ("Insert credits", quarter coins), `key 1` starts,
   P1 `StartLamp` goes on and credits return to 0 **[M]** expected from `packaging/games-status.md:312`.
2. **Crosshair follows the aim.** With a test point at u=0.3 / v=0.3 the in-game crosshair sits at
   **576/324 px** of 1920×1080 (Y origin bottom) **[M]** expected from `packaging/games-status.md:311`.
3. **Trigger and rumble.** Trigger shoots, recoil rises, the real Move pulses per game shot, shots are
   counted by the daemon.
4. **No savegame reset.** The `Bad PKCS7 padding` / `Savegame corrupted : … will be resetted` lines are
   **absent** — this can only pass after item 15 is implemented, so run it once before (to record the
   baseline) and once after.
5. **Play past world select** into an actual level, to see whether the savegame issue bites during play
   (the 288 s run never left attract/world-select).

**If it dies at ~57–70 s again**, check item 13/14 first (stale view DLL, or a stray
`RAVENGUN_MATRIX_DLL=1`) before suspecting the dongle answer: that exact combination has now caused this
symptom twice. Capture `BepInEx/LogOutput.log` and compare the view DLL's size+md5 against the build.

---

## 5. How much of the "it works live" claim I could verify

The original claim is verbatim at **`packaging/games-status.md:103`**:

> "Unity-route (Tomb Raider, Rabbids) draait nu als dialect unity-tcp van de brug: Rabbids Hollywood
> live gecontroleerd (verbonden, 120 pakketten/s, vizier, munten en start, schoten tellen mee voor de
> trilling)."

**Verdict: partly verified, and the part that was true was true for the wrong reason.**

| Element of the claim | Verdict |
|---|---|
| connected | **Verified** — 288 s run, daemon reported `VERBONDEN met de game` **[M]** `DONGLE-ONDERZOEK.md:32-33` |
| 120 packets/s | **Verified** **[M]** same line |
| start and stays alive | **Verified on the shipped route** — 288 s, 0× `Application.Quit` **[M]** `DONGLE-ONDERZOEK.md:15-37` |
| vizier (crosshair) | **Not verified on the shipped route.** Revision 1's evidence was the *DemulShooter* plugin (`packaging/games-status.md:309`); in the 288 s run the crosshair was never exercised **[M]** `DONGLE-ONDERZOEK.md:180-184` |
| munten en start (coins/start) | **Not verified on the shipped route.** The four `key 5` + `key 1` of the 288 s run never reached the game (headless gamescope; exactly one `Stand:` line, credits 0) **[M]** `DONGLE-ONDERZOEK.md:180-184` |
| schoten tellen mee voor de trilling (shots → rumble) | **Not verified on the shipped route** — the same run never fired a shot |

Revision 1's reading was that the own plugin's 50–70 s deaths made the claim narrow. That reading was
correct about the *symptom* but wrong about the *cause*: it was a stale view DLL (item 1a), not a failure
of the plugin or the route. With the fresh DLL the achievement is genuine — but the coins/start/crosshair
half of the claim still rests on runs made with the third-party DemulShooter plugin, and the headless run
that proved longevity could not test them. So: **"starts and stays alive" is proven; "playable" is not,
and must not be written into the dossier.** The remaining gap is one run on the TV (§4).

---

## 6. Files consulted

`packaging/aagames-games.json` · `packaging/games-status.md` · `docs/decisions.md` (`decisions.md`) ·
`docs/architecture.md` · `docs/loskoppelen-per-game.md` · `docs/matrix-layer.md` · `docs/procedures.md` ·
`docs/compatibility.md` · `docs/todo.md` · `docs/per-game-stand.md` ·
`docs/research/demulshooter-integratie.md` · `docs/research/loskoppelen-metingen.md` ·
`docs/research/zonder-teknoparrot.md` · `docs/research/eigen-runtime-inventaris.md` ·
`docs/research/teknoparrot-io.md` · `docs/research/findings-digest.md` ·
`findings/RabbidsHollywood.md` · `DONGLE-ONDERZOEK.md` (dongle specialist, 2026-10-12) ·
`patches/RabbidsHollywood/{README.md, game.ini, dropin/dropin.ini, dropin/matrix/matrix.ini}` ·
`scripts/play-unity.sh` · `scripts/aagames-setup.py` · `scripts/matrix.py` · `scripts/launch-direct.py` ·
`src/ravengun/games/aagames.hpp` · `src/ravengund/aagames_service.cpp` ·
`src/tools/aagames_probe/aagames_probe.cpp` · `plugins/unity/src/rabbids/Rabbids.cs` (two snapshots) ·
`matrix/src/*`.

### Limitation
The Deck paths the assignment names (`~/projects/ravengun`, `~/projects/ravengun-patches`, the NAS
mount) are not reachable from this Windows bench; revision 1's survey and this revision were done against
the workspace mirrors. Where two mirrors disagree I state both and treat `ravengun-patches` plus the
newest `matrix_work` mirror as authoritative. The 288 s measurement is the dongle specialist's, taken on
the Deck **[M]**; this agent neither ran nor repeated it. No file on the NAS was written, no game was
started, and nothing was committed.

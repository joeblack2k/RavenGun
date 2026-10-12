# House of the Dead: Scarlet Dawn — per-game loader dossier

**Game:** House of the Dead: Scarlet Dawn (SEGA ALLS UX, 2018, Unreal Engine 4.18, x64)
**Profile:** `GameProfiles/HOTDSD.xml`, `EmulatorProfile ALLSHOTDSD`, `EmulatorType TeknoParrot`, revision 21
**Game folder:** `Z:\Teknoparrot\House of the Dead Scarlet Dawn` on this Windows session
(= `\\192.168.2.3\emulation\Teknoparrot\House of the Dead Scarlet Dawn`, and on the Deck
`/home/deck/mnt/spidernas_emulation/Teknoparrot/House of the Dead Scarlet Dawn`)
**Round:** RavenGun per-game loader round — HOTD Scarlet Dawn, 2026-10-11
**Constraints honoured:** NAS strictly read-only (nothing written, moved or deleted there); the
game was **not** started; no keychip/dongle/HASP/integrity work (see §9); no git commits.

Every claim below is tagged **[M]** measured in this round or in the round-12 research,
**[M-proj]** measured by an earlier round of this project (cited), or **[I]** inferred.

---

## 0. How this round measured, and what it adds

The earlier AM Daemon research (round 12) was done on the Deck with `strings`, `objdump`, `xxd` and
Python. This round could work **directly from Windows**, because the NAS is mounted here:
`Z:` → `\\192.168.2.3\emulation`.

New in this round (all [M], all read-only):

| # | What was measured | Why it matters |
|---|---|---|
| 1 | Full recursive inventory of the game folder (2,081 files, 13,095,511,155 B, 0 empty) and of every `.exe`/`.dll`/`.bat`/`.ini` in it | confirms the folder is complete and shows **no `Shell.exe`/`SystemLauncher.exe` anywhere** — the SEGA `TFM` shell is not part of this game |
| 2 | PE headers, section tables, import tables, DOS stub and Rich header of **both** the working exe and its `_org` twin, and of the daemon and its `.original` twin | settles what "patched" means for this game (§1.3) |
| 3 | Byte-identity check of the game's daemon against `loaders/SegaShell/bin/amdaemon.exe` and `hwvalue.exe` | proves the repo's "SegaShell" is a *copy* of the game's own daemon, **not** a TeknoParrot component |
| 4 | ASCII + UTF-16LE string probe of the game exe and the daemon on daemon-related names | shows **the game is the IPC client and the daemon is the IPC server** (§2.2) |
| 5 | Path/registry probe inside the daemon | shows the drive letters are **data**, not literals (§2.3) |

Scripts and raw output are in this folder:

* `probes/pe_compare.py` → `measurements/01-pe-compare.txt`
* `probes/string_probe.py` → `measurements/02-string-probe.txt`
* `probes/ipc_probe.py` → `measurements/03-ipc-probe.txt`
* `probes/mount_probe2.py` → `measurements/04-mount-probe.txt`

Reproduce with the bundled interpreter:

```powershell
& "$env:USERPROFILE\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe" probes\pe_compare.py
```

> **Note on placement.** The existing per-game folder for this title in this repo is
> `patches/HOTDSD/` (`game.ini` + `README.md`). I followed the instruction for this round and put the
> dossier in `patches/HouseOfTheDeadScarletDawn/`. If the long name is not wanted, the two folders are
> easy to merge; nothing in `patches/HOTDSD/` was changed.

---

## 1. What this game is, and what route it needs

### 1.1 The shape of the arcade title

| | |
|---|---|
| Engine | Unreal Engine 4.18, x64, Shipping build **[M-proj]** |
| Project codename | `Hodzero` (`Hodzero-Win64-Shipping.exe` — the `-Win64-Shipping.exe` suffix is the standard UE4 naming convention) **[M]** |
| Platform | SEGA ALLS UX — `pxGetHwInfo.ini` says `platform-id = ACA0` **[M]** |
| Game id | `SDET` (`99_StartApp.bat`: `set GAME_ID=SDET`) **[M]** |
| Content | one 7.8 GB UE4 pak + 123 CRIWARE `.usm` videos **[M]** |
| Programs | **two**: `amdaemon.exe` (SEGA AM Daemon) and the game exe; that is what `HasTwoExecutables` / `LaunchSecondExecutableFirst` encode **[M]** |
| Input | two lightguns; aim + triggers + `start` / `gun_select` / `gun_special` per player **[M]** |
| Network | ALL.Net is **not** needed to play **[M-proj]** |

The game is, in other words, a normal UE4 arcade title wrapped in SEGA's platform layer. The UE4 part
is unremarkable; the platform layer is the whole problem, and it is exactly two things: **a second
program that must run first**, and **a small set of absolute paths the second program needs**.

### 1.2 The cabinet's own startup chain — read in full this round [M]

The chain is `game.bat` → `App/Startup/AppStartup.bat` → `99_StartApp.bat` → `App/Hodzero/StartApp.bat`.
All files are CP932 (Shift-JIS); the comments are Japanese, the keys are ASCII.

```
game.bat                     LAUNCH_ON_IMAGE=1 ; IMAGE_ROOT_DIR defaults to X:\
                             pushd "%IMAGE_ROOT_DIR%\App\Startup" ; call AppStartup.bat

App/Startup/AppStartup.bat   call 05_SetVideoPort.bat      reg add HKLM\SYSTEM\SEGA\SystemProperty\amVideo\PortSetting port_1..port_5
                             call 10_SetResolution.bat     ..\Tools\SetResolution.exe 1920 1080 0
                             call 20_SetAudioVolume.bat    ..\Tools\SetAudioVolume.exe 85 75
                             call 99_StartApp.bat

App/Startup/99_StartApp.bat  LAUNCH_MODE=target ; GAME_ID=SDET ; APP_DIR=Y:\SDET
                             APP_LOG_DIR=%APP_DIR%\current_log ; ABAAS_LOG_DIR=%APP_DIR%\log
                             if not exist Y:\SDET  ->  rmdir /S /Q "Y:\" ; mkdir "Y:\SDET"
                             pushd ..\Hodzero ; call StartApp.bat target

App/Hodzero/StartApp.bat     DETAIL_LAUNCH_MODE=cabinet  (no Dev\AsMiniCabinet, no Dev\AsTarget in this copy)
                             DAEMON_CONFIG_PATH=WindowsNoEditor\AMDaemon\config.json
                             DAEMON_WAIT_SECONDS=5
                             start /min "" amdaemon.exe -c <config.json> -l "<APP_LOG_DIR>\<date>\amdaemon.log"
                             timeout 5
                             start /wait "" Hodzero-Win64-Shipping.exe -launch="cabinet" \
                                    -ABSLOG=... -UserDir=... -NotInstalled -UNATTENDED
                             timeout /t 1
                             taskkill /im amdaemon.exe
```

Three things in that chain matter for us:

* **Order and wait are explicit and short:** daemon, **5 seconds**, game, 1 second, `taskkill` the
  daemon. `DAEMON_WAIT_SECONDS=5` is a file fact, not a guess.
* **`05`/`10`/`20` are cabinet-boot jobs**, not game jobs: they write the SEGA video-port registry
  values, set the desktop resolution and set the ALLSX/SB audio volumes. **TeknoParrot never runs
  them** — and the game is playable anyway **[M-proj]**. So they are not on the critical path; they are
  polish we can adopt later per-machine.
* **`StartApp.bat` has a `pc` mode that skips the daemon entirely.** Measured in the file:

  ```bat
  :CheckDaemon
  if "%LAUNCH_MODE%"=="pc" goto StartApp
  ```

  `LAUNCH_MODE` is `pc` when `StartApp.bat` is called **with no argument** and neither of the two dev
  marker files exists (they do not exist in this copy). The same file then launches the game with
  `-launch="pc"`. This is the **only documented no-daemon path** in the whole tree, and it lines up
  with the single string `DebugWithoutAMDaemon` that exists in the game exe **[M]**. It is *not*
  proven that the game is playable in that mode — but it is the cheapest remaining static question
  (§6, measurement B).

### 1.3 What "the exe is patched" actually means — measured, and it changes the story

The folder holds two pairs of binaries:

| Pair | Working (A) | Twin (B) |
|---|---|---|
| game | `Hodzero-Win64-Shipping.exe` — 54,128,128 B, sha256 `0fdb745d…` | `Hodzero-Win64-Shipping_org.exe` — 51,470,336 B, sha256 `38fde9f4…` |
| daemon | `amdaemon.exe` — 8,272,384 B, sha256 `b4b0484e…` | `amdaemon.exe.original` — 4,567,040 B, sha256 `a483facc…` |

Measured PE facts (`measurements/01-pe-compare.txt`):

| | game A | game B (`_org`) | daemon A | daemon B (`.original`) |
|---|---|---|---|---|
| PE32+ x64 | yes | yes | yes | yes |
| PE timestamp | 2018-08-21T10:10:19Z | **same** | 2018-04-19T14:13:45Z | **same** |
| image base | 0x140000000 | **same** | 0x140000000 | **same** |
| DOS stub (0x40) | — | **identical** | — | **identical** |
| entry point | 0x01c2929c | 0x1b5c | 0x2268a4 | 0x1b5c |
| file alignment | 0x200 | 0x1000 | 0x200 | 0x1000 |
| sections | 9, normal sizes, `.idata` | 9, **every raw size 0x1000**, `.reloc` | 6, normal, `.idata` | 6, **every raw size 0x1000**, `.reloc` |
| import directory | **50 DLLs** | **1 (`KeRnEl32.dLl`), 0x28 bytes** | **23 DLLs** | **1 (`KeRnEl32.dLl), 0x28 bytes** |
| overlay after last section | 0 bytes | 0 bytes | 0 bytes | 0 bytes |
| TeknoParrot strings | **0** (ASCII + UTF-16) **[M-proj]** | n/a (packed) | **0** | n/a |

Read together:

* **A and B are the same build** — identical timestamp, image base, DOS stub, section names and PE
  layout skeleton.
* **B is not a runnable plain image.** Every section is a single 0x1000 page at 0x1000 file alignment,
  the whole import table is one `LoadLibraryA`, the entry point sits at 0x1b5c inside a stub
  (round 12 measured that stub's first bytes as `pop rax` followed by a push of all registers), and the
  real payload sits in a high-entropy block between the section table and the first raw section
  (30,239,680 B for the game, 4,541,640 B for the daemon) **[M-proj + M]**.
* **A is a plain, fully-imported PE with no overlay.** The game imports 50 DLLs (including
  `d3d11`, `dxgi`, `xinput1_3`, `winhttp`, `wininet`, `ws2_32`, `setupapi`, `cfgmgr32`); the daemon
  imports 23 (including `hid.dll`, `setupapi`, `cfgmgr32`, `winhttp`, `ws2_32`, `wlanapi`, `crypt32`,
  `bcrypt`).

> **Conclusion [M, with one honest limit].** The delta between A and B is **packed → unpacked**, not
> "3.7 MB / 2.66 MB of TeknoParrot runtime squeezed into the files". There is no TeknoParrot code in A:
> no string, no section, no extra import. What I cannot prove by reading is *how* A was produced (a
> protector that unpacks at load, or a scene-provided unpacked image). What I can state is that **B is
> not usable as a drop-in replacement** and that **A needs no on-disk patch at all**.
>
> This directly contradicts `findings/antwoorden_scarlet_dawn.md` §1 from the TeknoParrot team ("er is
> 3,7 MB en 2,66 MB aan code bijgezet — dat is een volledige runtime die in de bestanden is geschoven").
> Their *conclusion* — "TeknoParrot is not required, and the drive letters are what it supplies" —
> survives measurement; their *mechanism* does not.
>
> Practical consequence, and it is the good news of this round: **a per-game patch for this game is not
> a byte patch.** There is nothing to `Find`/`Replace` in either exe. Whatever our loader does for this
> game, it does at run time.

### 1.4 The route the game needs

```
1.  amdaemon.exe                         <- FIRST, from App/Hodzero/WindowsNoEditor/AMDaemon/
    cabinet chain args:  -c WindowsNoEditor\AMDaemon\config.json  -l <log>
    TeknoParrot args:    NONE  (ExtraParameters is empty)  -> still reaches "Process state: Advertise" [M-proj]
2.  wait DAEMON_WAIT_SECONDS = 5         (StartApp.bat) [M]
3.  Hodzero-Win64-Shipping.exe           <- THEN
    cabinet chain args:  -launch="cabinet" -ABSLOG=… -UserDir=… -NotInstalled -UNATTENDED
    TeknoParrot args:    NONE  -> playable [M-proj]
4.  when the game exits: taskkill amdaemon.exe
```

Both processes must be started by the **same loader with the same core**, because the core's drive-letter
redirection and input channel are per-process and both processes need them **[M-proj]**.

The batch chain's arguments are **not** required: TeknoParrot passes none and the game runs **[M-proj]**.
The 5-second wait is what the vendor's own chain uses and is cheap insurance.

---

## 2. The AM Daemon: what it provides, and what the project already knows

### 2.1 What `amdaemon.exe` is

`amdaemon.exe`, 8,272,384 B, PE32+ x64 console subsystem, image base 0x140000000, entry 0x2268a4,
linker 11.0, build `amdaemonExe Ver.2437 Build:Apr 19 2018 23:13:37`, built from
`D:\jenkins_work\workspace\all_build\apps\amdaemon\src\…` and `…\libs\libamw\src\…` **[M]**.
It is a **console** program (subsystem 3) — worth remembering for the measurement in §6.

What it provides (round-12 research, `docs/research/amdaemon-mechanisme.md`, all [M-proj] and
re-confirmed by my string probe):

| Service | Evidence |
|---|---|
| **The AM services for the game over shared memory** — not a port, not a named pipe | `amdaemon::ipc::` (369 names): `SharedMemory`, `DaemonSharedPart`, `SharedDataList`, `makeDaemonShared`; one `Client…Request`/`Server…Response` pair per service: `aime`, `allnet`, `apm`, `backup`, `boardio`, `can`, `credit`, `emoney`, `error`, `input`, `jvs`, `network`, `output`, `process`, `sequence`, `abaas` |
| **A request/response server per service** | `amdaemon::server::` (189–378 names), e.g. `JvsController::loadInput`, `CreditController::loadData`, `SequenceController::beginPlay`, `BackupRequestExecutor::save`, `RequestExecutor<…>::doAccept` |
| **Publishing the mount points** | imports `RegSetValueExW` **and** `RegCreateKeyExW`; `amsSystemPropertySetMountAmfs`, `amsSystemPropertySetMountAppdata`, `amsSystemPropertyMountInit` |
| **Reading the SEGA boot's hardware properties** | `System\SEGA\SystemProperty\{mount,amVideo,keychip,Static,Master,Install,installer,sgworker,wirelessNetwork}`, `System\Sega\SystemProperty\sgsetdisplaysetting\CurrentSetting` **[M]** |
| **The keychip/dongle check** | `am::dongle::Manager`, `amDongleOpen`, `amDongleSetupKeychip`, `amDongleGetGameId/ModelType/Region`, device path `\??\FddDriver`, errors `Keychip Not Found`, `Keychip Access Failed`, `Retry Keychip Access.` (24 `Keychip` + 101 `Dongle` strings) **[M]** |
| **The USB-I/O board and the AiMe reader** | `hid.dll`/`setupapi.dll`/`cfgmgr32.dll` imports; `HidD_*`, `SetupDiEnumDeviceInterfaces`, `DeviceIoControl`, serial API; firmware in `AMDaemon/aime_firm/` |
| **The ALL.Net client outwards** (`amMaster`, `alpb*` = ALL.Net Payment/Billing) | `amMasterInit/Update/GetResult`; `alpb*` 67 strings; imports `winhttp`, `ws2_32`, `crypt32`, `bcrypt`, `wlanapi` |
| **Clock and system** | `SetSystemTime`, `SetLocalTime`, `SetTimeZoneInformation`, `GlobalMemoryStatusEx` |

And what it does **not** do: it never checks the `pxGetHwInfo.ini` hardware requirements (`pxGetHw`
appears **0** times). That is `pxGetHwinfo.exe`'s job, and **nothing in the startup chain runs that
program** **[M-proj]** — so the hardware gate is inert in this copy.

### 2.2 The daemon↔game channel is an IPC arena, and the *game* is the client — measured this round

Round 12 knew the daemon's server side. What this round adds is the **other end**:

| String | in the game exe | in the daemon |
|---|---|---|
| `amdaemon::ipc::` | **506** | 738 |
| `amdaemon::server::` | 0 | 378 |
| `ClientRequest` / `ServerResponse` | 131 / 76 | 55 / 56 |
| `DaemonShared` / `DaemonSharedPart` / `makeDaemonShared` | 6 / 2 / 2 | 6 / 2 / 2 |
| `amdaemon::ctrl::` client helpers | present (`RequestExecuter<amdaemon::ipc::aime::ClientRequest, …>`, `RequestableCtrlBase<…>::executeBlockingRequest<…>`) | — |
| the 13 lifecycle mutex formats (`ALIVEHANDLEMUTEX`, `LIBMUTEX`, `DBMUTEX`, …) | **0** | 1 each |

**[M]** So: the game statically links the AM Library's **client** layer (`amdaemon::ctrl::*`) and talks
to the daemon through a shared-memory arena whose types are `DaemonShared` / `DaemonSharedPart`, using
**blocking** request/response calls. The daemon holds the server side and the lifecycle mutexes.

This is the single most important structural fact for our plan: **there is no port and no named pipe to
reverse-engineer.** Everything the game wants from the platform layer goes through that arena, and the
only way to keep the game happy is to keep **SEGA's own daemon** running next to it.

### 2.3 Where the drive letters come from — measured this round, and it is *data*, not literals

`measurements/04-mount-probe.txt`:

```
load SystemProperty\mount\AMFS.
open SystemProperty\mount.
System\SEGA\SystemProperty\mount
amfs_share$
C:\mount\Option\
C:\mount\Option
x:\gametest
s:\sgsegaboot.exe
s:\sgsegaboot.exe -s test
L:\tfps-res\
Error: get amfs drive letter failed. error code is %d.
Error : amsSystemPropertyGetMountAmfs error = %d
```

* The daemon reads its mount values from **`HKLM\SYSTEM\SEGA\SystemProperty\mount`** (value `AMFS`,
  plus an app-data mount) and falls back to `amfs_share$` / `C:\mount\Option\`.
* **Neither the daemon nor the game contains a literal `X:\`, `Y:\` or `D:\`.** [M] The letters are
  **data**. The TeknoParrot team's line "het spel zoekt zijn bestanden op `X:`, `Y:` en `D:`" is not
  supported by the binaries; what the binaries show is a daemon that resolves mount **names** to
  letters/paths, and a game that never mentions a letter at all.
* Which means: the `AMDaemon/TeknoParrot/{AMFS,D,Y}` tree that TeknoParrot creates next to the daemon
  exe is a rewrite target for **whatever the daemon asks for** — which is why the folder sits next to
  `amdaemon.exe` and not next to the game **[M]**.

### 2.4 What the project already has on this layer

| Asset | Where | State |
|---|---|---|
| The mechanism, with evidence | `staging/docs/research/amdaemon-mechanisme.md` (392 lines) | complete, [M-proj] |
| The two-program start, as a recipe | `staging/docs/research/amdaemon-twee-proces-start.md` (210 lines) | complete, [M-proj] |
| `minime` analysed and **ruled out** as a daemon replacement | `staging/docs/research/amdaemon-minime.md` | complete, [M-proj] |
| The layer summary + "is this a folder or loader work?" | `staging/docs/amdaemon-layer.md` | complete, [M-proj] |
| Read-only advisor tool | `re/_pgg/loader-scripts/amdaemon.py` (and `rgwork/out/amdaemon.py`) | prints the status, blockers and the start plan |
| A copy of the daemon + `hwvalue.exe` + SEGA's `Shell.exe` | `loaders/SegaShell/` | **research copy** — I measured that `bin/amdaemon.exe` and `bin/hwvalue.exe` are **byte-identical** to the game's own files (sha256 `b4b0484e…` and `ceaa2c27…`) **[M]** |
| `minime` source | `loaders/SegaTools/bin/minime/` | an ALL.Net/AiMe **server** (Node/TypeScript); knows `SDDF`/`SDBT`/`SBZV`, **not** `SDET` |

**What does not exist, and what this round shows does not need to exist: an AM Daemon replacement.**
`ravengund` is the lightgun/HID/IO-bridge daemon for the Deck (`camera.cpp`, `hid_thread.cpp`,
`iobridge_service.cpp`, `web_server.cpp`) — it has nothing to do with AM Daemon. There is no
`amdaemon` substitute in the tree, and, see §3, TeknoParrot does not ship one either.

---

## 3. Ours versus TeknoParrot's — the honest split, today

### 3.1 What TeknoParrot actually contributes at run time

| Component | What it does for this game | Evidence |
|---|---|---|
| **`TeknoParrotUi.exe` / `TeknoParrotUi.dll`** | Reads `HOTDSD.xml`, starts **two** processes in the right order (`ExecutableName2` first because `LaunchSecondExecutableFirst=true`, with `ExtraParameters` empty so **no arguments**), reads RawInput and writes the input channels, drops `teknoparrot.ini` next to **each** launched exe | `GameProfiles/HOTDSD.xml` **[M]**; the two 449-byte `teknoparrot.ini` files next to the daemon **and** next to the game exe **[M]** |
| **`OpenParrotLoader64.exe`** | The injector. Under Wine the working route is its **default entry-point-trampoline** ("Resolved target kernel32.dll procedure … Have fun :)"), **not** `TP_REMOTETHREAD` (that fails under Wine with "Failed to find kernel32.dll in target process … Failed to Load DLL via RemoteThread! (Error 2b)") | **[M-proj]**, `findings/HOTDScarletDawn.md` |
| **`TeknoParrot64.dll`** (the closed core) | The drive-letter redirection (TEAM STATEMENT), the input channel, FreePlay, gun axis handling. **Packed** (section `.teknoGo`); 0 readable strings about ALLS/amdaemon/AMFS/keychip in 84,538 extracted strings — its HOTDSD behaviour is **not statically readable** | `docs/research/amdaemon-mechanisme.md` §6 **[M-proj]** |
| **`amdaemon.exe`** | **Nothing.** TeknoParrot does not ship, patch or replace it: the profile launches the game's own `AMDaemon\amdaemon.exe` (byte-identical to the file in the game folder) | `GameSetup/HOTDSD.xml` `GameExecutableLocation2` **[M]** |
| **`pipehelper.exe`** | Under Wine, creates the real named pipe + named file mapping in the prefix and mirrors them over TCP to the UI and to `/dev/shm` | `staging/docs/research/teknoparrot-io.md` **[M-proj]** |
| **`HOTDSDPipe`** (the `ALLSHOTDSD` input writer) | Writes into `TeknoParrot_JvsState` (64 B): offset **8** = `Control`, bit 0 = P1 `Up` (= trigger), bit 1 = P2 `Up`; offsets **12/16/20/24** = `AnalogBytes[0,2,4,6]`. **Nothing for start, test, service or coin.** | `re/pipes_il/per/HOTDSDPipe.Transmit.il` **[M]** |

### 3.2 What is ours today

| Layer | Ours | State |
|---|---|---|
| **Profile / per-game data** | `patches/<Game>/game.ini` + `README.md`; `staging/ravengun-patches/patches/HOTDSD/` already exists with both | working; HOTDSD entry says `Mode = tpui` because the direct route is refused |
| **Launcher** | `re/_pgg/loader-scripts/launch-direct.py` (~1,700 lines). Reads the profile + `game.ini`, has a patch motor, `JvsPipeServer`, `/dev/shm` mirroring, calls `pipehelper.exe`, honours `[Env] RG_UNSET_REMOTETHREAD`, refuses Proton, enforces Wine-prefix wrapper rules | working — **but line 966 refuses two-exe profiles**: `if truthy(p.get("HasTwoExecutables","false")): raise Unsupported("profielen met twee exe's worden niet direct gestart")` → rc=4 **[M]** |
| **Loader / injector** | the same script drives either TeknoParrot's loader or an own OpenParrot build: `[Loader] Build = ravengun` | own core wired **only** for `EmulatorType OpenParrot` ("een eigen kern bestaat alleen voor EmulatorType OpenParrot (dit is %s)") **[M]** |
| **Own core** | `cores/`, `memory/`, `matrix/`, `Native/`, `OpenParrotLoader` | exists for the OpenParrot family; HOTDSD is `EmulatorType TeknoParrot` |
| **Patch motor** | In `launch-direct.py`: byte patches against **the loader or the core only**, with `ExeSha256` version checking, and an explicit refusal to patch anything on the NAS ("het doel %s ligt op de NAS; er wordt nooit een bestand op de NAS gepatcht") | working, and for HOTDSD **there is nothing to patch** (§1.3) |
| **Daemon** | **nothing** — and, see §2.4, nothing needed: SEGA's binary is in the game folder | n/a |
| **Network layer** | nothing (and not needed: measured that the game plays with no ALL.Net server) | n/a |
| **I/O layer** | `ravengund` (Move cameras, HID, IO bridge, web UI) + `JvsPipeServer` + tp-shm dialects (`packaging/tp-shm-profiles.json`) | working for the games it has dialects for; `HOTDSDPipe`'s dialect carries **only** trigger + axes |
| **AM Daemon knowledge** | the three round-12 research docs + `amdaemon.py` | complete |

### 3.3 The one-line comparison

**TeknoParrot's irreplaceable contribution for this game is not the daemon and not a patch — it is the
in-memory layer that (a) turns a drive letter into a folder next to the exe and (b) feeds the input
that `HOTDSDPipe` does not carry.** Everything else is either SEGA's own binary (the daemon) or already
ours (loader, patch motor, per-game data, gun input).

---

## 4. The concrete blockers

| # | Blocker | Kind | Evidence |
|---|---|---|---|
| **B1** | **Our loader refuses two-executable profiles.** No order, no wait, no "kill the daemon when the game ends", and both processes must get the same core. | ours, small and generic — every ALLS title has this shape | `launch-direct.py` line 966–967, rc = 4 **[M]** |
| **B2** | **The drive-letter/mount layer is missing on our side.** The daemon resolves `SystemProperty\mount` (AMFS + an app-data mount) to absolute paths; TeknoParrot answers that with a per-process rewrite into `.\TeknoParrot\<LETTER>\`. We have nothing equivalent. | ours, new, bounded (a small letter→folder map) | `AMDaemon/TeknoParrot/{AMFS,D,Y}` + `D/SDET/appfile.dat` written by the daemon **[M]**; `<LETTER>` rewrite is a **team statement**, not something round 12 could confirm (the core is packed) — **[I]** |
| **B3** | **The input channel for start / test / service / coin is unknown.** Our recorded direct-launch trial reached the attract screen but "start (key 1) gave nothing". `HOTDSDPipe` provably carries only triggers + axes, and `/dev/shm/TeknoParrot_JvsState` only ever changed for the trigger. | ours, **the real unknown** | `findings/HOTDScarletDawn.md` §Invoer **[M-proj]**; `HOTDSDPipe.Transmit.il` **[M]** |
| **B4** | **The closed core is required today, and it requires TeknoParrot activation.** `launch-direct.py` needs TP's activation (seed / registry) for `TeknoParrot64.dll`, and an own core is only wired for `EmulatorType OpenParrot`. | ours, structural | `raise ActivationMissing("de activering staat niet (actueel) in %s; start eenmaal via TeknoParrotUi")`, `"een eigen kern bestaat alleen voor EmulatorType OpenParrot"` **[M]** |
| **B5** | **The daemon's keychip/dongle layer.** `amDongle*`, `\??\FddDriver`, `Keychip Not Found`, and `amsSystemPropertyGetPlatformIdOnDongle()` — the *platform id itself* can come from the dongle. | **rule: document and stop** (§9) | strings **[M]** |
| **B6** | **The `_org` / `.original` files are packed shells**, so "put the originals back" is a protector/unpacking job. | **rule: document and stop** — and unnecessary, because the working exes are plain PEs | §1.3 **[M]** |

**B3 is where the interesting evidence points.** `AMDaemon/config.json` declares the cabinet switches
as **USB-I/O** inputs, not as JVS or as a generic input area:

```
input.system:   bakuon(14) eye0(10) proximity_cabinet(10) emoney_left(13) emoney_right(12)
                emoney_center(11) cabinet_kind(11) test(-1,0) service(6)          [usb_node 0]
input.players:  start(7)  gun_select(4)  gun_special(1)   per player 0 and 1
usb_io.node[0].required_function: coin_input(slot 2), switch_input(2 players, 8 buttons),
                                  general_output(slot 20), unique_io(command 65, param [6])
```

**[M]** So the switches the game reads for `start` / `test` / `service` sit on the **daemon's emulated
USB-I/O board** — which is exactly the thing that `HOTDSDPipe` does *not* write. The coherent reading
**[I]** is: the core's 64-byte `TeknoParrotPipe` is what TeknoParrotUi sends to the core, and the core
turns it into USB-I/O board state that the **daemon** then serves to the game over its normal
`amdaemon::ipc::` arena. That would explain both observations at once (attract works, start does not)
and why the daemon keeps running the whole time. It also means B3 is *daemon-side input*, not *game-side
input* — which is a very different, and much smaller, piece of work than a game-side hook.

That reading is a hypothesis, and §6 is designed to test the half of it that can be tested without
starting the game.

---

## 5. The loader/patch plan — running it with no TeknoParrot component at all

Target: **our loader + our core, SEGA's daemon, nothing from TeknoParrot.** Numbered, in dependency order.

1. **Teach the loader the two-executable shape (B1).** Remove the refusal at
   `launch-direct.py:966` and implement the recipe from `amdaemon-twee-proces-start.md`:
   * if `HasTwoExecutables` and `LaunchSecondExecutableFirst`: start `Executable2`, wait
     `DaemonWait` seconds, start `Executable`;
   * new `game.ini` key `[Launch] DaemonWait = 5` (default 5, value from `DAEMON_WAIT_SECONDS`);
   * start **both** through the same loader with the same core;
   * when the first-executable process exits, kill `Executable2` (`taskkill /im amdaemon.exe`
     equivalent), as `StartApp.bat` does;
   * keep the existing rule that nothing on the NAS is patched.
   *Keep the trial honest:* the earlier trial proved the *launch* works; make the new code path a
   first-class, tested feature rather than a reverted experiment.

2. **Build the mount/letter shim (B2).** In the core (or a tiny preload), intercept
   `CreateFileA`/`CreateFileW` in **both** processes and rewrite any absolute path whose root is a
   drive letter `L:` into `<dir of the exe>\TeknoParrot\L\<rest>` — byte-for-byte the behaviour the
   `AMDaemon/TeknoParrot/` tree demonstrates. Seed the letter list from the daemon measurement (§6) and
   store it per game: `[Mounts] Amfs = AMFS`, `AppData = D`, `AppDir = Y`.
   This is the only genuinely new loader-side subsystem in the whole plan.

3. **Start the daemon the way the cabinet does, not the way TeknoParrot does.** Pass
   `-c <AMDaemon>\config.json -l <log>` (TeknoParrot passes nothing and it still reaches
   `Advertise` **[M-proj]**, so both work; the config is the one that describes the real cabinet —
   `usb_io`, `credit`, `video`, `error.auto_reset`). Keep `taskkill` on game exit.
   Do **not** run `05_SetVideoPort.bat` / `10_SetResolution.bat` / `20_SetAudioVolume.bat` in the first
   iteration: the playable route does not, and their absence is not the reason anything fails.

4. **Close B3 by extending the input channel.** After §6 tells us which side owns the switches,
   either
   * (a) extend our shm/pipe dialect so `start` / `test` / `service` / `coin` reach the core, and let
     the core serve them to the daemon as USB-I/O switch state — if the §3 hypothesis holds this is the
     smaller path; or
   * (b) add a game-side hook for those four signals.
   Do not guess here: this is the step that decides whether the route is days or weeks.

5. **FreePlay.** The profile carries `FreePlay=1` and TeknoParrot implements it in-core. Ours must do
   the equivalent — either free play in the core, or enough credit through the daemon (`config.json`
   `credit.max_credit = 24`, `game_cost` with `1`s for two modes) plus a coin signal from our input
   layer. Small, but do not skip it: without it a start costs a coin the player cannot insert.

6. **Per-game data.** `patches/HouseOfTheDeadScarletDawn/game.ini`: two exe's, `DaemonWait = 5`,
   the mount letter map from step 2, `[Launch] Mode = direct`. Add a `dropin/dropin.ini` **only if** a
   file actually has to be placed in the view — at the moment nothing does. There is deliberately **no**
   `dongle.json` (`docs/geen-dongle-in-de-loader.md`).

7. **Explicitly out of scope, and why.**
   * keychip / dongle emulation, `\??\FddDriver` — rule (§9);
   * unpacking `amdaemon.exe.original` / `Hodzero-Win64-Shipping_org.exe` — rule, and unnecessary
     (§1.3);
   * `pxGetHwinfo.exe` / `pxGetHwInfo.ini` — nothing in the chain runs it, so there is nothing to
     satisfy **[M-proj]**;
   * `minime` as "the open amdaemon" — it is an ALL.Net/AiMe **server**, does not know `SDET`, and is
     not needed to start **[M-proj]**. Repeating it as an option (as `patches/HOTDSD/README.md`
     currently does) sends the next reader down the wrong road;
   * the SEGA `TFM` shell (`Shell.exe`, `SystemLauncher.exe`) — **not part of this game**: there is no
     `Shell.exe` anywhere in the folder (I listed all 8 exes and 24 dlls) **[M]**.

---

## 6. The exact next measurement

**Question it settles:** can SEGA's own `amdaemon.exe` run **without any TeknoParrot component**, and
exactly which absolute paths does it need? This decides whether B2 is a small letter shim (plan step 2)
or a daemon rewrite — the single biggest fork in the plan. It starts **the daemon only, never the
game**, so the bench and the owner's screen stay free.

**Preparation — build a view so the NAS is never written to:**

```sh
V="$HOME/.local/share/ravengun/runtime/views/HOTDSD"
mkdir -p "$V"
rsync -a --delete \
  "/home/deck/mnt/spidernas_emulation/Teknoparrot/House of the Dead Scarlet Dawn/App/Hodzero/WindowsNoEditor/AMDaemon/" \
  "$V/AMDaemon/"
```

**The measurement itself:**

```sh
cd "$V/AMDaemon"
wine reg query 'HKLM\System\SEGA\SystemProperty\mount' 2>&1 | tee "$V/mount-before.txt"

timeout 90 env WINEDEBUG=+file \
  WINEPREFIX="$HOME/.local/share/ravengun/prefixes/shared/wine" \
  wine amdaemon.exe -c config.json -l "$V/amdaemon.log" \
  > "$V/amdaemon-filetrace.log" 2>&1
echo "exit=$?"

# what it wanted on disk, deduplicated:
grep -oE '[A-Za-z]:\\\\[^"]*' "$V/amdaemon-filetrace.log" | sort -u > "$V/paths.txt"
wc -l "$V/paths.txt" ; cat "$V/paths.txt"

# how far it got:
grep -aiE 'amdaemonExe|Ver\.|state|Advertise|Keychip|Dongle|Error|Fail' "$V/amdaemon.log" | tail -60

# and whether it published the mounts itself:
wine reg query 'HKLM\System\SEGA\SystemProperty\mount' 2>&1 | tee "$V/mount-after.txt"
```

`amdaemon.exe` is a **console** binary (measured: subsystem 3), so it should not take the screen; if the
owner's machine has `xvfb-run` available, prepend `xvfb-run -a -s "-screen 0 1280x720x24"` to the `wine`
line as a belt-and-braces variant. Do **not** run the `05`/`10`/`20` batch steps in the same session.

**What counts as success — all four:**

1. `amdaemon.log` prints the `amdaemonExe Ver.2437 Build:…` banner and reaches a state line containing
   **`Advertise`** (or at minimum gets past initialisation and is still running at the end of the
   timeout, instead of exiting in the first seconds).
2. `paths.txt` is **non-empty and enumerates the mount paths** — in particular which letter/path the
   daemon resolves for `AMFS` and for the app-data mount, and where it wants `appfile.dat` /
   `sysfile.dat`. This is the input for plan step 2 and replaces guesswork with a list.
3. The log contains `Keychip Not Found` and/or `Retry Keychip Access.` **and the daemon keeps going** —
   i.e. the keychip layer is tolerated, not fatal. (If it *is* fatal, that is a §9 stop, and the finding
   must be written down rather than worked around.)
4. `mount-before.txt` vs `mount-after.txt` shows whether the daemon **created** `SystemProperty\mount`
   itself (it imports `RegSetValueExW`/`RegCreateKeyExW`). This closes open question #2 of round 12 —
   "where does the first mount value come from?" — with data instead of a candidate list.

**Bonus measurement B — free, static, no bench, can be done today on Windows.**
Resolve the single `DebugWithoutAMDaemon` string in the game exe: find its file offset, convert to an
RVA, then scan `.text` for a RIP-relative `lea` whose displacement targets it, and inspect the
surrounding code for an ini/CVar read. That answers whether the game has a real no-daemon mode — the
thing the `pc` branch of `StartApp.bat` hints at. Cheap, and it could shrink the dependency on the
daemon to "startup only".

---

## 7. Honest effort estimate

| Piece | Effort | Confidence |
|---|---|---|
| B1 two-executable launch in our loader (order, wait, same core, cleanup) | **0.5–1 day** | high — the mechanics are already proven by the recorded trial and the recipe is written down |
| B2 mount/letter shim (CreateFileA/W rewrite in both processes, per-game letter map) | **2–4 days** | medium — the shape is clear, the letter list is the input to §6, and the Windows-level hook is the only fiddly part |
| Step 3 daemon invocation + logging + view plumbing | **0.5 day** | high |
| B3 input for `start` / `test` / `service` / `coin` | **3–10 days** | **low** — depends entirely on where §6's follow-up lands; if the switches are daemon-side USB-I/O this is the small end, if they are game-side it is the large end |
| B4 own core / dropping the activation dependency for `EmulatorType TeknoParrot` | **1–2 days** to wire, more if the core must also absorb FreePlay and gun axis | medium |
| FreePlay equivalence | **0.5 day** | medium |
| Per-game data + README + verification pass | **0.5 day** | high |

**Two honest headlines:**

* **Milestone 1 — "starts without TeknoParrotUi, still using TeknoParrot's loader + core":**
  **~1–2 days.** B1 plus a small `game.ini`. The trial already reached the attract screen this way; what
  is missing is start/test input and making the path first-class.
* **Milestone 2 — "no TeknoParrot component at all, fully playable":**
  **~2 weeks of focused work**, and it is gated on B3. If B3 turns out to be game-side, add a week. If it
  turns out to be daemon-side USB-I/O, the estimate holds at the low end. The one thing that is now
  certain is that the *daemon* is not part of the cost: we run SEGA's own binary, exactly as TeknoParrot
  does.

---

## 8. Measured versus inferred — the honest ledger

**Measured in this round [M]**
* Folder completeness: 2,081 files, 13,095,511,155 B, **0 empty files**; only 8 `.exe` and 24 `.dll`,
  and **no `Shell.exe` / `SystemLauncher.exe`** anywhere.
* Every line of the startup chain quoted in §1.2, including the `pc` branch that skips the daemon.
* The full PE comparison in §1.3 (headers, sections, imports, DOS stub, timestamps, overlays).
* `loaders/SegaShell/bin/amdaemon.exe` and `bin/hwvalue.exe` are **byte-identical** to the game's copies.
* The game exe contains the **client** half of `amdaemon::ipc` (`amdaemon::ipc::` ×506, `ClientRequest`
  ×131, `DaemonShared`/`DaemonSharedPart`/`makeDaemonShared`, `amdaemon::ctrl::…RequestExecuter<…>`),
  and **none** of the daemon's 13 lifecycle mutex formats.
* No `X:\` / `Y:\` / `D:\` literal in the game exe **or** the daemon; the daemon reads
  `SystemProperty\mount` (values `AMFS` + app-data) with `amfs_share$` / `C:\mount\Option\` fallbacks.
* `HOTDSDPipe.Transmit` writes exactly `Control` at offset 8 (bits 0 and 1) and `AnalogBytes[0,2,4,6]`
  at offsets 12/16/20/24.
* The two 449-byte `teknoparrot.ini` files, one next to the daemon and one next to the game exe.
* `launch-direct.py` refuses two-exe profiles at line 966; the patch motor never patches the NAS.

**Measured by an earlier round [M-proj], reused and cited**
* The game is **playable** through TeknoParrotUi on the Deck under plain Wine: attract "FREE PLAY",
  start (key 1), Mode Select, item selection, ~1.5 min load, first stage, trigger and sub-trigger,
  crosshair measured at corners and centre. `amdaemon.log` shows `amdaemonExe Ver.2437` and
  `Process state: Advertise`. The daemon uses about one core.
* The daemon runs with **no arguments** under TeknoParrot and still reaches `Advertise`.
* The working injection route under Wine is the loader's entry-point trampoline; `TP_REMOTETHREAD` fails.
* The direct two-exe trial reached the attract screen **without input**; the change was reverted.
* `amdaemon.exe` and the game exe contain **zero** TeknoParrot strings (ASCII + UTF-16).
* `TeknoParrot64.dll` is packed (`.teknoGo`) and yields no readable ALLS/amdaemon/AMFS/keychip strings.
* `pxGetHwinfo.exe` is never started by anything in the chain.

**Inferred, and marked as such [I]**
* That the working exes were produced by *unpacking* the `_org`/`.original` images. What is measured is
  that B is a one-page-stub packed image with a single `LoadLibraryA` import and a large high-entropy
  payload, and that A is a plain fully-imported PE of the same build with no TeknoParrot content. The
  direction of the transformation is an inference.
* That TeknoParrot's core rewrites drive letters via a `CreateFileA` hook into `.\TeknoParrot\<LETTER>\`.
  This is the **TeknoParrot team's statement**, and it fits the folder placement, but the core is packed
  and round 12 could not confirm it. My measurement confirms the *targets* exist and that the daemon
  writes into them; it does not confirm the mechanism.
* That `start` / `test` / `service` / `coin` reach the game via the **daemon's** emulated USB-I/O board
  rather than through a game-side hook (§4, B3). This is the hypothesis that §6's follow-up is meant to
  attack; it is consistent with `config.json` and with the recorded failure, but it is not measured.
* That `TeknoParrot\Y\` exists because the daemon or core resolved an app-directory mount to `Y:`.
  Which value produced it is not determined.
* That `-launch="cabinet"` / `-ABSLOG` / `-UserDir` / `-NotInstalled` / `-UNATTENDED` are unnecessary.
  Measured is that TeknoParrot passes no arguments at all and the game is playable; the conclusion that
  the arguments are optional is an inference from that.

**Open, and left open on purpose**
* `DebugWithoutAMDaemon` — one string in the game exe, unexplained. See §6 measurement B.
* Whether the `pc` mode of `StartApp.bat` yields a genuinely playable no-daemon route.
* The origin of the `Y:` mount.
* Whether the game would refuse to continue if the keychip were *required* rather than tolerated.

---

## 9. Keychip, dongle and integrity — documented, then stopped

Per the rules for this round, these were **not** worked on. They are recorded precisely so that whoever
picks this up does not re-open them by accident.

**What exists [M]**
* Inside `amdaemon.exe`: `am::dongle::Manager::setup/loadInfo`, `amDongleInit`, `amDongleOpen`,
  `amDongleIsAvailable`, `amDongleSetupKeychip`, `amDongleGetGameId/ModelType/Region/NetworkAddress/SystemFlag`,
  `amDongleReadNvram`, `amDongleEncrypt/Decrypt`, `amDongleBillingGetKeychipId`,
  `amDongleBillingGetPlayCount`, `amyDongleDriverStart/End`, `amyDriverSyncronizeDeviceIoControl`;
  device path `\??\FddDriver`; errors `Keychip Not Found`, `Keychip Access Failed`,
  `Error : Invalid Keychip ID : %s`, `Retry Keychip Access.`, `Error : keychip mode is wrong.`
* The **platform id and format type can come from the dongle**:
  `amsSystemPropertyGetPlatformIdOnDongle()`, `amsSystemPropertyGetFormatTypeOnDongle()` **[M]**.
* The ALL.Net billing layer `alpb*` (`alpbGetKeychipSerialID`, `ALPB_DONGLE::BillingGetKeychipId`).
* `loaders/SegaShell/config/Dongle - 261009.log` records `DONGLE : MainThreadDongleScan: failed stage 1`.
* Independently, `findings/WAT-ELK-SPEL-NODIG-HEEFT.md` (TeknoParrot team) lists Scarlet Dawn among the
  games **without** a HASP file — this game's "dongle" dependency is the AM Daemon, not a USB key.

**What is measured about the consequence [M-proj]**
* The game is **playable with no keychip present**, under TeknoParrot, with SEGA's unmodified daemon:
  `error.auto_reset = true` in `config.json`, `Retry Keychip Access.` in the binary, and the daemon
  reaches `Process state: Advertise`.
* Therefore the keychip is **not on the critical path as long as we keep running SEGA's own daemon.**

**The two consequences for the plan — and the place where the rule bites**

1. **Keep SEGA's daemon; do not write a substitute.** A daemon substitute would have to reimplement the
   keychip layer to satisfy `amsSystemPropertyGetPlatformIdOnDongle`, which is exactly the work the
   rules exclude. Running the vendor binary avoids the question entirely.
2. **If a future measurement shows the game refusing to continue because the keychip response is
   invalid** — rather than merely logging and retrying — then that is the stop condition. Write down the
   exact log line and the state it stops in, and stop. Do not patch the daemon, do not emulate
   `\??\FddDriver`, do not forge a keychip id.

Separately, and also excluded: **the `_org` / `.original` files are packed shells**, so "put the
originals back" is anti-tamper/unpacking work (§1.3). It is also unnecessary: the working exes are plain
PEs and there is nothing to byte-patch in them.

---

## 10. What changes for the parent, in one paragraph

Round 12 concluded that Scarlet Dawn needs *a daemon we have to build* ("er is geen daemon" →
engine-werk). The bytes say otherwise, and this changes the plan's centre of gravity: SEGA's daemon is
already in the game folder, is **byte-identical** to the copy in `loaders/SegaShell/`, and TeknoParrot
does not patch or replace it — it merely launches it. What TeknoParrot actually supplies for this title
is a **drive-letter redirection into `.\TeknoParrot\<LETTER>\`** and **the input that `HOTDSDPipe` does
not carry**. The first is a small, well-bounded, generic piece of loader work (and every ALLS title will
need it); the second is the real unknown. Also worth correcting in the docs before it costs someone a
week: the "+3.7 MB / +2.66 MB of TeknoParrot runtime added to the exes" claim is contradicted by the
PEs — the twins are packed shells, the working files are plain PEs of the same build carrying **zero**
TeknoParrot content, so there is no byte patch to reproduce for this game. With that, Milestone 1
(starting via our own loader with TP's loader/core, ~1–2 days) is close, and Milestone 2 (no TeknoParrot
component at all) is roughly two weeks, gated on the input channel.

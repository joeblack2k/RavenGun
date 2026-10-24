# House of the Dead: Scarlet Dawn — per-game loader dossier

**Game:** House of the Dead: Scarlet Dawn (SEGA ALLS UX, 2018, Unreal Engine 4.18, x64)
**Profile:** `GameProfiles/HOTDSD.xml`, `EmulatorProfile ALLSHOTDSD`, `EmulatorType TeknoParrot`, revision 21
**Game folder:** `Z:\Teknoparrot\House of the Dead Scarlet Dawn` on this Windows session
(= `\\192.168.2.3\emulation\Teknoparrot\House of the Dead Scarlet Dawn`, and on the Deck
`/home/deck/mnt/spidernas_emulation/Teknoparrot/House of the Dead Scarlet Dawn`)
**Round:** RavenGun per-game loader round — HOTD Scarlet Dawn, 2026-10-11
**Constraints honoured:** NAS strictly read-only (nothing written, moved or deleted there); the
game was **not** started; no git commits. The keychip/dongle/authenticity layer **is** in scope for this
report — the boundary is about *who* does that work, not *whether* — so §9 measures it and §10 designs it.

> **Provenance note — verified, not a mirror.** The measurements in this round were taken through
> the SMB share that this Windows session has as `Z:` (`\\192.168.2.3\emulation`). That is the **same
> CIFS share the Deck mounts**: `findmnt` on the Deck reports
> `//192.168.2.3/emulation cifs /home/deck/mnt/spidernas_emulation`. It was confirmed by
> re-measuring on the Deck itself — folder totals **2,081 files / 13,095,511,155 B / 0 empty** and
> five complete sha256 values all match:
> `Hodzero-Win64-Shipping.exe` `0fdb745d…5ac0`, `…_org.exe` `38fde9f4…cdf`,
> `amdaemon.exe` `b4b0484e…3415`, `amdaemon.exe.original` `a483facc…4cf1`,
> `hwvalue.exe` `ceaa2c27…7c83`. **This is not a stale mirror**, so the results stand.
> (Verification script: `re\_hotdsd-verify.sh`, run on the Deck with
> `powershell -File re\rc.ps1 -Target deck -Cmd "bash /home/deck/_hotdsd-verify.sh"`.)

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
| 6 | ASCII sweep of the daemon for the whole dongle/keychip/billing surface, and the complete string table of `hwvalue.exe` | recovers the **entire `amDongle*` ABI**, the ten functions libam itself leaves unimplemented, the registry value names, and `hwvalue`'s own English keychip help text (§9) |
| 7 | UTF-16LE sweep of the daemon and `hwvalue.exe` for driver/adapter names | finds `\??\FddDriver`, `\\.\sghwreset`, `\\.\sghwmonitor`, `\\.\HCD%d`, the `amyDongleDriver*` bracket functions, the AiMe adapter DLL names, and `hwvalue`'s usage/enum block (§9.1, §9.4, §9.5) |
| 8 | Provenance check against the Deck itself (totals + five sha256) | proves the SMB share I read is the Deck's own CIFS mount, **not a stale mirror** |

Scripts and raw output are in this folder:

* `probes/pe_compare.py` → `measurements/01-pe-compare.txt`
* `probes/string_probe.py` → `measurements/02-string-probe.txt`
* `probes/ipc_probe.py` → `measurements/03-ipc-probe.txt`
* `probes/mount_probe2.py` → `measurements/04-mount-probe.txt`
* `probes/keychip_probe.py` → `measurements/05-keychip-probe.txt`
* `probes/wide_probe.py` → `measurements/06-wide-probe.txt`
* `probes/hwvalue_dump.py` → `measurements/07-hwvalue-wide.txt`

Deck-side verification used in this round: `re\_hotdsd-verify.sh` in the loader repo, run with
`powershell -NoProfile -File re\rc.ps1 -Target deck -Cmd "bash /home/deck/_hotdsd-verify.sh"`.

Reproduce with the bundled interpreter:

```powershell
& "$env:USERPROFILE\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe" probes\pe_compare.py
```

> **Note on placement.** The existing per-game folder for this title in this repo is
> `patches/HOTDSD/` (`game.ini` + `README.md`). I followed the instruction for this round and put the
> dossier in `patches/HOTDSD/`. If the long name is not wanted, the two folders are
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

6. **Per-game data.** `patches/HOTDSD/game.ini`: two exe's, `DaemonWait = 5`,
   the mount letter map from step 2, `[Launch] Mode = direct`. Add a `dropin/dropin.ini` **only if** a
   file actually has to be placed in the view — at the moment nothing does.

7. **The keychip layer — in scope, designed in §10.** Add it to the **same hook table in the same
   process** as step 2: `CreateFileW("\\.\FddDriver")` returns a synthetic handle, and
   `DeviceIoControl` answers from a per-game `sega-keychip` descriptor. Start with the cheap registry
   experiment (§10.3 step 1, §6 measurement C): if `HKLM\SYSTEM\SEGA\SYSTEMPROPERTY\keychip` with
   `keychipId`/`platformId`/`formatType` satisfies `amsSystemPropertyGet*OnDongle`, the device layer only
   has to cover NVRAM and billing. Values come from the open `minime` implementation, not from invention.
   Note the ten `amDongle*` functions libam itself answers with *"not implemented"* — do not emulate them.
   A `dongle.json` is correct here and is per-game data, not loader code.

8. **Explicitly not needed** (measured, not merely forbidden).
   * unpacking `amdaemon.exe.original` / `Hodzero-Win64-Shipping_org.exe` — the working exes are plain
     PEs of the same build, and packed code cannot be hooked anyway (§1.3);
   * `pxGetHwinfo.exe` / `pxGetHwInfo.ini` — nothing in the chain runs it, so there is nothing to
     satisfy **[M-proj]**; only relevant if we switch to running the vendor chain (§9.7);
   * `minime` as "the open amdaemon" — it is an ALL.Net/AiMe **server**, does not know `SDET`, and is
     not needed to start **[M-proj]**. Repeating it as an option (as `patches/HOTDSD/README.md`
     currently does) sends the next reader down the wrong road. Its **values** and its **PKI** are
     useful; its process is not;
   * the SEGA `TFM` shell (`Shell.exe`, `SystemLauncher.exe`) — **not part of this game**: there is no
     `Shell.exe` anywhere in the folder (I listed all 8 exes and 24 dlls) **[M]**;
   * the `amsIcf*` image-integrity path — no `.icf` file exists in the folder, so it is idle (§9.6).

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

### Measurement C — the keychip layer, and it piggybacks on the same run

**Question it settles:** does the daemon take its `keychipId` / `platformId` / `formatType` from the
**registry** (cheap) or does it insist on the **`\\.\FddDriver` device** (expensive)? And if it insists
on the device, what is the ioctl protocol?

**Step C1 — the 0.5–1 day experiment.** In the same view prefix, seed the registry key the daemon names
itself (§9.3), then repeat the §6 daemon run and compare:

```sh
V="$HOME/.local/share/ravengun/runtime/views/HOTDSD"
W="$HOME/.local/share/ravengun/prefixes/shared/wine"
export WINEPREFIX="$W"
wine reg add 'HKLM\System\Sega\SystemProperty\keychip' /v keychipId   /t REG_SZ /d 'A0000000000000000000' /f
wine reg add 'HKLM\System\Sega\SystemProperty\keychip' /v platformId  /t REG_SZ /d 'ACA0' /f
wine reg add 'HKLM\System\Sega\SystemProperty\keychip' /v formatType  /t REG_SZ /d '0'    /f
wine reg query 'HKLM\System\Sega\SystemProperty\keychip'
# then re-run the daemon exactly as in the main measurement and diff the log
```

*Success:* the `Keychip Not Found` / `Retry Keychip Access.` lines **change shape** (fewer retries, a
different state, or the identity questions stop being asked). That would mean the identity half of §10
collapses into a registry write and the device shim only has to cover NVRAM and billing.
*Failure:* the log is byte-identical → the daemon really does go to the device, and C2 is required.

**Step C2 — capture the ioctl protocol (daemon only, no game).** Wine's relay tracer shows the imported
call with its parameters, so the ioctl codes and buffer sizes fall out as data — no disassembler and no
code:

```sh
cd "$V/AMDaemon"
timeout 90 env WINEDEBUG=+relay,+file WINEPREFIX="$W" \
  wine amdaemon.exe -c config.json -l "$V/amdaemon-keychip.log" \
  > "$V/amdaemon-relay.log" 2>&1
grep -a 'DeviceIoControl' "$V/amdaemon-relay.log" | head -100 > "$V/fdd-ioctls.txt"
grep -aoE 'code=0x[0-9a-f]+' "$V/amdaemon-relay.log" | sort | uniq -c | sort -rn > "$V/ioctl-codes.txt"
grep -a 'FddDriver\|CreateFile' "$V/amdaemon-relay.log" | head -40
```

*Success:* `fdd-ioctls.txt` is non-empty and `ioctl-codes.txt` lists the distinct ioctl codes — that is
the whole keychip protocol, and it goes straight into the `ioctls[]` array of the `sega-keychip`
descriptor (§10.2). `+relay` is very verbose, so keep the timeout short.
*If `+relay` produces nothing for `DeviceIoControl`:* the daemon may reach the device through
`ntdll`/`NtDeviceIoControlFile` instead; grep the relay log for `NtDeviceIoControlFile` and, failing
that, fall back to implementing the logging hook in our own core during plan step 7 — at which point the
measurement and the feature are the same code.

**What must NOT happen, and what to write down if it does:** if either run shows the daemon **aborting**
rather than logging `Keychip Not Found` and retrying, stop and record the exact line and state. That is
the one condition under which the keychip becomes a hard blocker instead of completeness work (§11.1).

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

**Three honest headlines:**

* **Milestone 1 — "starts without TeknoParrotUi, still using TeknoParrot's loader + core":**
  **~1–2 days.** B1 plus a small `game.ini`. The trial already reached the attract screen this way; what
  is missing is start/test input and making the path first-class.
* **Milestone 2 — "no TeknoParrot component at all, fully playable":**
  **~2 weeks of focused work**, and it is gated on B3. If B3 turns out to be game-side, add a week. If it
  turns out to be daemon-side USB-I/O, the estimate holds at the low end. The one thing that is now
  certain is that the *daemon* is not part of the cost: we run SEGA's own binary, exactly as TeknoParrot
  does.
* **Milestone 3 — "keychip layer complete" (§10):** **≈ 4–6 days**, and it can run in parallel with
  Milestone 2 because it lands in the same hook table and the same process as plan step 2. It is not on
  the critical path to playable: the game is measured playable with no keychip at all. Its first day is
  measurement C, which may remove most of the rest.

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
* **The keychip surface (§9):** the complete `amDongle*` client API in file order, including the **ten
  functions libam answers with `Function "…" is not implemented.`**; `\??\FddDriver` opened via the
  `\\.\%s` pattern; `amyDongleDriverStart/End`, `…StartNvram/EndNvram`, `…StartGetLog/EndGetLog`,
  `amyDriverSyncronizeDeviceIoControl`; errors `Keychip Not Found`, `Keychip Access Failed`,
  `ERROR: invalid KeychipID(%s)`, `Retry Keychip Access.`, `Error : keychip mode is wrong.`,
  `Disconnect dongle.`, `Error: setup timeout. state %d.`, `Error: DeviceIoControl failed. %08x`.
* **The registry contract:** `keychipId`, `platformId`, `formatType` under
  `System\Sega\SystemProperty\keychip`; `IDENTIFY_NAME`, `STATE`, `LAST_RESULT_CODE`, `PID`, `VID` under
  `…\sgworker`; `SYSTEM\SEGA\SYSTEMPROPERTY` as the root.
* **`hwvalue.exe`'s own help text**, quoted in §9.4: keychip model type `1 = Server / 2 = Satellite /
  3 = Live` in range `1–98`; region as bit flags `1 = JPN / 4 = EXP / 8 = CHN` with an optional AND
  mask; a dip-switch value; output names `modeltype`, `region`, `dipsw`.
* **The AiMe adapters are absent from the game folder** (`aime_rw_adapterMD.dll`, `thincatcapclient.dll`,
  `indicdll.dll` are all named by the daemon and none of them is among the folder's 24 DLLs) — the
  shipped state is "no card reader".
* **No `.icf` file in the folder**, so the `amsIcf*` image-container/key path is idle.
* `AMDaemon/config.json` has **no `allnet*` section**; the daemon validates `allnet_auth.enable` against
  `network.*` and refuses inconsistent combinations, but the shipped config does not trip that.
* Provenance: the SMB share I read equals the Deck's CIFS mount (totals and five sha256 values matched on
  both sides).

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

## 9. The SEGA keychip, the AiMe reader and ALL.Net — measured surface

**This layer is in scope** (subagents own keychip/dongle/authenticity work; see the ownership note at
the end of this section). Everything below is measured from the game's own binaries in this round
(`measurements/05-keychip-probe.txt`, `06-wide-probe.txt`, `07-hwvalue-wide.txt`).

### 9.1 The device, and why one hook covers the whole keychip

| Piece | Evidence |
|---|---|
| Device path | `\??\FddDriver`, opened through the `\\.\%s` pattern as `\\.\FddDriver` **[M]** |
| Driver client layer | module `amyDongle`, functions `amyDongleDriverStart`, `amyDongleDriverEnd`, `amyDongleDriverStartNvram`, `amyDongleDriverEndNvram`, `amyDongleDriverStartGetLog`, `amyDongleDriverEndGetLog`, and the funnel `amyDriverSyncronizeDeviceIoControl` **[M]** |
| The actual I/O call | `DeviceIoControl`; errors `Error: DeviceIoControl failed. %08x`, `Error: DeviceControl failed. %08x`, `Error: Maybe library bug.` **[M]** |
| Other SEGA devices in the same client | `\\.\sghwreset`, `\\.\sghwmonitor`, `\\.\HCD%d` (USB host controllers, for the AiMe reader), `sgmanageio` **[M]** |
| Reference client, small | **`hwvalue.exe` (202,240 B) links the same `amDongle*`/`amPlatform*` client** and is a command-line keychip reporter with an English usage text — it is a second, much smaller sample of the same ABI **[M]** |

Two consequences:

* **`amy*` is not a hook point.** Those functions are statically linked inside `amdaemon.exe`, not
  exported. But every one of them funnels into `DeviceIoControl` on the `\\.\FddDriver` handle, so
  **`CreateFileW` + `DeviceIoControl` interception in the daemon process covers the entire keychip
  surface** — one clean boundary, and it is a boundary our core already has to work at for the drive
  letters (§4, B2). Same process, same hook table.
* **There is no kernel driver to write.** The `FddDriver` kernel driver is a SEGA-shipped component that
  does not exist under Wine; we do not need it, because we answer the ioctls in user mode.

### 9.2 The complete `amDongle*` contract, and the 10 functions that are *not implemented*

Reading the `amdaemon.exe` string table in file order gives the whole client API, and — importantly —
libam itself answers **"Function … is not implemented"** for ten of them **[M]**:

| Group | Functions | Notes |
|---|---|---|
| Lifecycle | `amDongleInit`, `amDongleOpen`, `amDongleUpdate`, `amDongleIsAvailable(Internal)`, `amDongleExit` | `Error: setup timeout. state %d.` shows `amDongleSetupKeychip` is a **state machine with a timeout** |
| Identity | `amDongleGetModelType`, `amDongleGetRegion`, `amDongleGetPlatformId`, `amDongleGetFormatType`, `amDongleGetGameId`, `amDongleGetSystemFlag`, `amDongleGetNetworkAddress` | these are the values the game's platform layer is built on |
| Crypto | `amDongleSetIv`, `amDongleEncrypt`, `amDongleDecrypt` | AES through the keychip |
| NVRAM | `amDongleReadNvramInternal`, `amDongleWriteNvramInternal`, `amyDongleGetGeometryNvram` | the daemon **validates** the request itself: `Nvram(%hhu) is out of range. Valid range %hhu - %u`, `Adress(%u) is out of range. Valid range 0 - %u`, `BufferSize(%u) is out of range. Valid range 1 - %u` |
| Billing / ALL.Net | `amDongleGetBillingMode`, `amDongleBillingGetKeychipId`, `…GetPlayCount`, `…AddPlayCount`, `…GetPlaylimit`, `…UpdatePlaylimit`, `…GetNearfull`, `…UpdateNearfull`, `…GetSignaturePubKey`, `…GetCaCertification`, `…TdRestore`, `…PutTraceData`, `…GetTraceData`, `…TdGetLogNum`, `…TdGetFreeLogNum`, `…TdLogicalErase`, `…TdEraseUsedSector`, `…TdEraseAll` | `BillingUpdatePlaylimit pKeychipId(pPlaylimit or pSign is NULL)` — play limits are **signed** |
| **Not implemented** (we do **not** have to emulate these) | `amDongleSetAuthConfig`, `amDongleGetAuthCondition`, `amDongleSetAuthCondition`, `amDongleGetSeed`, `amDongleGetVersion`, `amDongleGetDvdFlag`, `amDongleReadEeprom`, `amDongleWriteEeprom`, `amDongleRequestSsdHostProof`, `amDongleBillingGetMainId`/`SetMainId`, `amDongleBillingTdReserveErasableSector` | each prints `Function "…" is not implemented.` **[M]** |
| Errors | `Keychip Not Found`, `Keychip Access Failed`, `ERROR: invalid KeychipID(%s)`, `Error : Invalid Keychip ID : %s`, `Retry Keychip Access.`, `Error : keychip mode is wrong.`, `Disconnect dongle.` | the daemon logs and retries; it does not abort |

That last column is a **measured scope reduction of the keychip work**: roughly a fifth of the ABI is
dead code inside libam, so a keychip implementation only has to answer the identity, NVRAM, crypto and
billing groups.

### 9.3 The registry contract — the cheap path, and possibly the whole answer

`hwvalue.exe` names its own value keys, which is exactly the contract the SEGA boot writes **[M]**:

```
HKLM\SYSTEM\SEGA\SYSTEMPROPERTY
  System\Sega\SystemProperty\keychip    value names: keychipId, platformId, formatType
  System\Sega\SystemProperty\sgworker   value names: IDENTIFY_NAME, STATE, LAST_RESULT_CODE, PID, VID
  System\SEGA\SystemProperty\mount      value names: AMFS, <app data>
  System\Sega\SystemProperty\amVideo    value names: port_1 .. port_5   (written by 05_SetVideoPort.bat)
```

and the reading functions are `amsSystemPropertyGetKeychipId`, `amsSystemPropertyGetPlatformIdOnDongle`,
`amsSystemPropertyGetFormatTypeOnDongle`, `amsSystemPropertyGetNetworkAddressIpv6` **[M]**.

**Why this matters:** the daemon's own "who am I" questions have a *registry* answer as well as a
*device* answer. If `HKLM\SYSTEM\SEGA\SYSTEMPROPERTY\keychip` already holds `keychipId` / `platformId` /
`formatType`, the expensive half of the keychip layer may not be needed at all for a start. Whether
`amsSystemPropertyGet*OnDongle` prefers the registry or insists on the device is **the single most
valuable thing left to measure** (§6, measurement C).

### 9.4 The documented keychip values — `hwvalue.exe`'s own help text

`hwvalue.exe` prints an English usage block. Quoted verbatim from its wide-string table **[M]**:

```
Usage: hwvalue <target> [<mask>]

    Return the keychip model type. [1-98]
        1 : Server
        2 : Satellite
        3 : Live

    Return the region code. [1,4,8]
        1 : JPN
        4 : EXP
        8 : CHN

    Return the dip-switch application value. [0-...]
        4 : Terminal

    <mask> (optional)
        If the integer value is set, … Exclude the error value (… returns the result of the AND operation.)

output value names:  modeltype   region   dipsw
```

Errors it can report: `Cannot initialize the keychip module.`, `Cannot setup the keychip module.`,
`Cannot read the keychip model type.`, `Cannot read the keychip region.`, `Cannot get the dip-switch
backup record.`, `Cannot get the default dip-switch backup data.`, `Cannot read the dip-switch backup
data.`, `Cannot initialize the system file module.`, `Cannot initialize the backup module.`

Three useful facts fall out of it:

* The **region is a bit field** (`1/4/8`, with an AND mask) — i.e. JPN/EXP/CHN are flags, not an enum.
  This is the SEGA-ALLS flavour of the project's existing `region-pcb` dongle type.
* The **keychip model type range is 1–98** with `1 = Server`, `2 = Satellite`, `3 = Live` known.
* The keychip also carries a **dip-switch** value and a dip-switch **backup record** on the AMFS/system
  file — so `sysfile.dat` (24,576 B, present in `AMDaemon/TeknoParrot/AMFS/`) is part of this surface.

### 9.5 The AiMe card/IC reader

| Piece | Status |
|---|---|
| Reader adapters the daemon loads by name | `aime_rw_adapterMD.dll`, `thincatcapclient.dll` (with config key `ThincaTcapClientPath`), `indicdll.dll` **[M]** |
| **Are they present?** | **No.** The game folder contains 24 DLLs and none of them is any of these (full inventory, §0) **[M]** |
| Reader firmware | `AMDaemon/aime_firm/`: `TN32MSEC003S_V12.hex`, `837_15084_1_01.hex`, `837-15286-P_LPC1112_NFC_RW_LED_BD_0x92.bin`, `update_15396_6728_93/94.bin` **[M]** |
| Transport | `hid.dll` + `setupapi` + `cfgmgr32` + `DeviceIoControl`, `\\.\HCD%d`, and the NFC mutexes `%u:LPNTNFCMUTEX:%x` / `%u:NFCBufMutex:%x`; the `NFC R/W` device name **[M]** |
| Server side | the AiMeDB protocol on TCP 22345 (`minime/src/aimedb/`), whose decoder reads a `keychipId` field from the message body **[M-proj]** |
| Config gate | the daemon validates `"emoney.enable" and "aime.enable" are inconsistent`, `"emoney.aime_unit" is out of "aime.unit" range`, `aime.unit[` **[M]** |

**Measured conclusion:** the SHIPPED state of this copy is *no card reader*. The adapter DLLs are absent
and the daemon is playable in that state, so the AiMe layer is not a start blocker. It only becomes work
if we want card reads to actually function — and then the cheapest route is to implement the adapter
DLL's surface (the daemon loads it by name, so a drop-in replacement named `aime_rw_adapterMD.dll` in
the daemon's search path is the natural per-game artifact) rather than to emulate hardware.

### 9.6 ALL.Net, billing and the signature surface

* The outbound ALL.Net call is one format string **[M]**:
  `keychipid=%s&functype=%u&gameid=%s&gamever=%s&boardid=%s&tenpoip=%s&libalibver=%s&datamax=%s&billingtype=%d&protocolver=%s&operatingfix=%d`
* Billing semantics are known from the open implementation **[M-proj]**, `minime/src/billing.ts`:
  `playlimit = 1024`, `nearfull = 0x00010200`, commented *"Keychip odometer limit. Game will lock out
  if the odometer reaches …"*. That is the concrete constraint a keychip answer has to respect.
* `minime/initpki` generates a CA, a server certificate and a billing key, and writes the CA as DER with
  the note *"Convert PEM cert to DER form for emulated keychip. DER must fit in 1024 bytes so it must be
  small."* **[M-proj]** — a ready-made certificate for exactly the `amDongleBillingGetCaCertification`
  call, with a stated size limit.
* Signature/authenticity surface **[M]**: `amDongleBillingGetSignaturePubKey`,
  `amDongleBillingGetCaCertification`, `alpbExPutVerifySysTimeLog`, `ALPB_DONGLE::checkDongleFuncResult`,
  `WinTrust.dll` referenced from the daemon's `.rdata`, and (from `hwvalue.exe`)
  `BillingUpdatePlaylimit pKeychipId(pPlaylimit or pSign is NULL)` — **the play limit is signed**.
* An **image-integrity** surface also exists but this deployment does not exercise it: `amsIcf*`,
  `Error: invalid ICF.`, `Error: Locked ICF`, `Error: Get ICF Key error=%d`, `tmpIcf.icf`,
  `updateDliFromIcf`. The game folder contains **no `.icf` file** (full extension inventory, §0), so the
  ALLS image-container/key path is idle here. Worth knowing before someone goes looking for a check that
  is not being made.
* Config-level gate: the daemon validates `"allnet_auth.enable"` against `network.enable`,
  `network.use_gateway` and `network.use_location_router`, and refuses an inconsistent combination
  **[M]**. `AMDaemon/config.json` contains **no `allnet*` section at all** (read in full, §2.4) — so
  ALL.Net auth runs on defaults. Measured: the game plays offline, so the default is tolerant
  **[M-proj]**.

### 9.7 What the hardware gate would require, if it were ever live

`pxGetHwinfo.exe` (701,440 B) is the program that enforces `pxGetHwInfo.ini`: `platform-id = ACA0`,
`mem-size = 16384`, `appdata-size = 17000`, `graphics-board-lane-width = 16`, `sub-storage = 0` **[M]**.
`hwvalue.exe` links the enforcement primitives — `amPlatformGetMemorySize`,
`amPlatformGetGraphicInfo`, `amPlatformGetSoundInfo`, `amPlatformGetDevInfo`,
`amPlatformIsAvailableSubStorage`, `amPlatformGetCheckInterfaceHandle`,
`amPlatformGetGpuPstate`, `amPlatformNvapiInit` **[M]**. Round 12 measured that **nothing in the
startup chain runs `pxGetHwinfo.exe`**, so the gate is inert in this copy `[M-proj]` — which is why we
do not have to satisfy it. It only becomes live if we choose to run the vendor chain (`game.bat`) rather
than launching the two exes ourselves.

### 9.8 Ownership note

Per the corrected rule for this round: the boundary around dongle/HASP/keychip/authenticity/checksum
work is about **who**, not **whether**. The reader-agent hands this layer over; **subagents (this
report) are the ones who investigate and design it.** §10 is that design, and §6 measurement C is the
one measurement it still needs. What remains genuinely out of scope is only what is *unnecessary*, not
what is *forbidden*: the packed `_org`/`.original` originals (§1.3) stay untouched because restoring
them is not needed — the working exes are plain PEs.

---

## 10. The keychip / dongle design for Scarlet Dawn

Goal: make the daemon's keychip layer answer, **inside the daemon process, with no kernel driver and no
hardware**, and express it in the existing `dongles/` model so it stays per-game data rather than code.

### 10.1 Where it lives

```
             amdaemon.exe
                  |  statically linked libam + libamw (amyDongle, amDongle*)
                  |  CreateFileW("\\\\.\\FddDriver")
                  |  DeviceIoControl(h, code, in, inLen, out, outLen, ...)
                  v
    +---------------------------------------------------+
    |  our core, injected into amdaemon.exe             |   <-- same injection as the
    |   hook CreateFileW  -> synthetic FddDriver handle  |       drive-letter shim (B2),
    |   hook DeviceIoControl -> answer from dongle.json  |       same process, same table
    +---------------------------------------------------+
                  ^
                  |  dongles/HOTDSD/dongle.json  (type: sega-keychip)
```

One hook table per process gives us both the mount letters (§4, B2) and the keychip. That is the reason
this design is cheap relative to its apparent size.

### 10.2 A new dongle type: `sega-keychip`

The existing `dongles/schema.json` has `hasp-hl`, `region-pcb` and `sega-scan`. `sega-scan` is a
stage-based framework whose contents were unknown. For ALLS the real contract is *the ioctl stream plus
a fixed identity record*, so add a fourth type — and keep `sega-scan` for the Ring `Shell.exe` path,
which is a different thing (§2.4).

```json
{
  "game": "House of the Dead: Scarlet Dawn",
  "type": "sega-keychip",
  "device": "\\\\.\\FddDriver",

  "identity": {
    "modeltype": 1,          // 1 Server, 2 Satellite, 3 Live   [1-98]
    "region": 1,             // bit flags: 1 JPN, 4 EXP, 8 CHN
    "platformId": "ACA0",    // ALLS UX (pxGetHwInfo.ini)
    "formatType": 0,
    "gameId": "SDET",        // 99_StartApp.bat
    "systemFlag": 0,
    "networkAddress": "10.0.0.2"
  },

  "billing": {
    "mode": 0,
    "keychipId": "A0000000000000000000",
    "playlimit": 1024,       // minime: billing.ts
    "nearfull": 66048,       // 0x00010200
    "signaturePubKey": "pki/billing.pub",
    "caCertification": "pki/ca.crt"
  },

  "nvram": { "geometry": [ { "index": 0, "size": 4096 } ],
             "files": { "0": "nvram/0.bin" } },

  "crypto": { "iv": "…", "keySource": "keychip" },

  "ioctls": [ { "code": "0x0022xxxx", "in": "keychip/ioctl-in.bin", "out": "keychip/ioctl-out.bin" } ],

  "notes": "…"
}
```

The `ioctls` array is the only part we cannot fill from static reading, because the ioctl codes are
immediate constants in the code rather than strings. §6 measurement C captures them.

### 10.3 The build order — cheapest question first

**Step 0 — use `hwvalue.exe` as the unit test.** It is 202 KB, it prints `modeltype` / `region` / `dipsw`
as text, and it links the same client as the daemon. That makes the whole keychip layer testable without
touching `amdaemon.exe` at all:

```sh
V="$HOME/.local/share/ravengun/runtime/views/HOTDSD"; W="$HOME/.local/share/ravengun/prefixes/shared/wine"
cd "$V/AMDaemon"
for t in modeltype region dipsw; do
  echo "--- hwvalue $t"; timeout 30 env WINEPREFIX="$W" wine hwvalue.exe "$t" 2>&1 | head -20
done
```

*Before* the shim this should say `Cannot read the keychip model type.` / `Cannot read the keychip
region.` — that is the baseline. *After* the shim it should print the numbers, and it is also the
cheapest way to learn the correct region/modeltype for an ALLS UX cabinet, instead of guessing.

1. **Registry-first (hours, not days).** Write `keychipId`, `platformId`, `formatType` into
   `HKLM\SYSTEM\SEGA\SYSTEMPROPERTY\keychip` in the prefix and try a daemon-only run (§6). If the
   `amsSystemPropertyGet*OnDongle` calls are satisfied from the registry, the device layer never has to
   answer the identity questions and the design shrinks to a shim.
2. **The device shim.** `CreateFileW` → synthetic handle; `DeviceIoControl` → desk-check against
   `dongles/…/dongle.json`, log every call, and answer from the captured `ioctls` table with the
   identity/billing/NVRAM values above. `SetIv`/`Encrypt`/`Decrypt` are AES through the keychip; start
   by logging and returning the identity/geometry answers, which is what `amDongleSetupKeychip` needs to
   leave its timeout loop.
3. **Values from the open implementation.** `minime/src/billing.ts` gives `playlimit`/`nearfull` and the
   lockout rule; `minime/initpki` gives a CA certificate built to fit 1024 bytes DER, which is exactly
   the `amDongleBillingGetCaCertification` shape. Reuse both rather than inventing values.
4. **Signature surface.** Answer `GetSignaturePubKey` / `GetCaCertification` with the minime PKI, and
   treat `BillingUpdatePlaylimit(pPlaylimit or pSign is NULL)` as "the limit comes with a signature" —
   so `UpdatePlaylimit` needs a signed payload, not a bare integer.
5. **AiMe, only if cards are wanted.** Ship a replacement `aime_rw_adapterMD.dll` in the daemon's
   search path (the daemon loads it by name, and the shipped copy is absent, §9.5) rather than emulating
   the reader hardware. Out of scope until card reads are an actual requirement.
6. **Do not touch**: the `_org`/`.original` packed originals, and the `pxGetHwinfo.exe` gate (inert —
   nothing runs it, §9.7).

### 10.4 Files in this folder that go with the design

| File | What it is |
|---|---|
| `dongle.sega-keychip.draft.json` | The draft descriptor, valid JSON, ready to become `ravengun/dongles/HOTDSD/dongle.json`. Every unmeasured value is marked `TODO` with the function that reads it, so nothing is silently invented. |
| `sega-keychip.schema-fragment.json` | The addition to `dongles/schema.json`: the `sega-keychip` enum entry, the property block and the `allOf` rule, plus a 5-step adoption list. `sega-scan` stays as-is for the Ring path. |

Both are proposals, not loader code; the loader repo's `dongles/` folder was **not** modified.

### 10.5 Effort for the keychip layer

| Step | Effort | Note |
|---|---|---|
| Step 0: baseline + values via the `hwvalue.exe` harness | **0.5 day** | gives the correct region/modeltype instead of a guess, and gives the acceptance test for everything below |
| Registry-first experiment + reading `amsSystemPropertyGet*` behaviour | **0.5–1 day** | might close the whole identity question |
| `\\.\FddDriver` shim + ioctl capture + triage | **1.5–2 days** | capture is scripted; triage is the variable |
| Identity + NVRAM geometry answers | **1 day** | values are all known (§9.4) |
| Billing values + minime PKI wiring | **1–2 days** | reuses `billing.ts`/`initpki` |
| AiMe adapter DLL (only if cards are wanted) | **2–4 days** | separate deliverable, not needed to play |
| **Total for the keychip layer proper** | **≈ 4–6 days** | on top of, not instead of, the loader work in §5 |

**And the honest caveat that has not changed:** the game is **measured playable with no keychip at all**
under TeknoParrot, with SEGA's unmodified daemon. So this layer is **completeness and robustness work,
not a start blocker**. It becomes load-bearing in exactly three situations: if a longer session trips
the billing/odometer path; if we ever run the vendor chain so that `pxGetHwinfo.exe` goes live; or if a
future measurement shows the game refusing to continue instead of logging `Keychip Not Found` and
retrying. That last case is the one to watch for, and it is what §6 measurement C is designed to
surface.

---

## 11. What changes for the parent, in one paragraph

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

### 11.1 And the keychip layer, now that it is ours

The layer turned out to be far better documented than expected, because **SEGA shipped a small
command-line keychip reporter next to the daemon**: `hwvalue.exe` links the same `amDongle*`/`amPlatform*`
client as `amdaemon.exe` and prints an English usage block that names the values — keychip model type
`1 = Server / 2 = Satellite / 3 = Live` in range `1–98`, region as bit flags `1 = JPN / 4 = EXP /
8 = CHN` with an AND mask, and a dip-switch value, with output names `modeltype`, `region`, `dipsw`
(§9.4). The transport is `CreateFileW("\\.\FddDriver")` + `DeviceIoControl`, with `amyDongleDriverStart/
End` and `amyDriverSyncronizeDeviceIoControl` as the in-process funnel (§9.1). **`amy*` is statically
linked, so it is not a hook point — but every call funnels into `DeviceIoControl`, so one hook in the
daemon process covers the whole keychip, in the same hook table we already need for the drive letters.**
There is no kernel driver to write.

Three things make this cheaper than the `sega-scan` framework in `dongles/` assumed:

* **A fifth of the ABI is dead code.** Ten `amDongle*` functions answer `Function "…" is not
  implemented.` from inside libam — auth config/condition, seed, version, dvd flag, eeprom, ssd host
  proof, billing main id, reserve-erasable-sector (§9.2). Measured scope reduction.
* **The values already exist.** `playlimit = 1024` and `nearfull = 0x00010200` come from the open
  `minime/src/billing.ts`, and `minime/initpki` builds a CA certificate to a stated 1024-byte DER budget
  for exactly the `amDongleBillingGetCaCertification` call (§9.6). Nothing has to be invented.
* **There may be a registry-only path.** The daemon's own value keys are `keychipId`, `platformId` and
  `formatType` under `HKLM\SYSTEM\SEGA\SYSTEMPROPERTY\keychip` (§9.3). If writing those satisfies
  `amsSystemPropertyGet*OnDongle`, the device layer does not have to answer the identity questions at
  all. That is a 0.5–1 day experiment and it is measurement C below.

The design is a new per-game dongle type **`sega-keychip`** in the existing `dongles/` model — identity
(modeltype/region/platformId/formatType/gameId/systemFlag/networkAddress), billing (keychipId,
playlimit, nearfull, signature pubkey, CA cert), NVRAM geometry and files, crypto IV, plus a captured
`ioctls` table — answered by our core's `DeviceIoControl` hook in `amdaemon.exe`. Effort **≈ 4–6 days**,
and it is **completeness work, not a start blocker**: the game is measured playable with no keychip at
all under TeknoParrot with SEGA's unmodified daemon. The AiMe reader is likewise already in its shipped
"absent" state (no `aime_rw_adapterMD.dll` anywhere in the folder, §9.5), and the `amsIcf*` image-integrity
path is idle (no `.icf` file in the folder, §9.6). The one thing to watch for is a run where the game
**refuses to continue** instead of logging `Keychip Not Found` and retrying — measurement C is built to
surface exactly that.

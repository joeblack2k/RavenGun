# Namco and ALLS games without the TeknoParrot core: Point Blank X, Mario Kart DX, Time Crisis 5, HOTD Scarlet Dawn

*Status 2026-10-17 (second round: TC5 and HOTDSD now have a candidate). The owner's goal: every game starts without the closed `TeknoParrot(64).dll` and without any file from
`~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64`. This document covers the four games that still load the closed core on the direct (Wine) route.
**None of this was measured on the TV**: nothing was started (no game, no Wine); everything was read statically
(objdump, strings, decompilation of the Unity assemblies, the open sources of TeknoParrotUI and OpenParrot) and built in the mingw container.
Candidate configurations are in `docs/research/namco/<Game>.game.ini` and, with their drop-ins, in `patches/<Game>/game.kandidaat.ini` and `patches/<Game>/dropin.kandidaat.d/`. **Second round (TC5 and HOTDSD): the complete candidate (`game.ini`, `dropin/`, `data/`) is in `docs/research/namco/patches/<Game>/`; check without TeknoParrot with `tools/dev/namco-candidates-check.sh`.** The bundled
`patches/<Game>/game.ini` and `dropin/` are unchanged, **except Mario Kart DX** (owner's decision 2026-10-17: default without TeknoParrot,
the closed core via the Online play option); the others stay on the closed core until the TV shows otherwise.**

## Overview

| Game | What the core did | Our replacement | Status |
|---|---|---|---|
| Point Blank X | route the USIO layer (`Plugins/bnusio.dll`: board via WinUSB, gun via COM) to shared memory; drive letters F:/G: | **our own `bnusio.dll`** (`tools/rg-usio`, drop-in in place of the real file) + our core (generic, drive-letter redirection patch 0004) | **ready to test** |
| Mario Kart DX | JVS: `COM3` -> pipe `TeknoParrot_JVS`; answer ALL.Net/AMAuthd (COM server) and point it at the TP community server; drive letters; `teknoparrot.ini` | JVS-over-COM3 in `rg-mfcam.dll` + our core, **by default** (offline). The "Online play" option in Properties puts `[Loader] Core = teknoparrot` back (online, requires TeknoParrot) | **offline by default; crash after ~15 s is ShareK (not IAuth), candidate: own ShareK object in `rg-mfcam.dll`, see "Mario Kart DX: the crash after ~15 s"** (`docs/research/namco/patches/MarioKartDX/`) |
| Time Crisis 5 | WinUSB/SetupAPI emulation of the USIO board in the game process, the HASP layer, aim/trigger | **our own `winusb.dll`** (`tools/rg-winusb`, drop-in next to the exe) + our core + HASP drop-in (already existed) | **candidate ready to test** (aim/trigger, pedal bits and HASP guessed) |
| HOTD Scarlet Dawn | platform data for `amdaemon.exe` (registry), drive letters AMFS/D/Y, USB-IO board (HID) from the control pipe, two-exe start | `platform.reg` + our core + **`rg-allsio.dll`** (`tools/rg-allsio`, HID emulation in the daemon, preload) | **candidate ready to measure** (product-string tail and replies guessed; drive letters open) |

## What an empty Wine start of these games lacks (the common foundation)

* Our OpenParrot core on the generic route (`[Loader] Core = ravengun`, patch 0003) loads every game without per-game hooks. What it does do for
  every game (patch 0004): `CreateFileA/W` and `CreateDirectoryA/W` on `<L>:\` or `<L>:/` become `.\TeknoParrot\<L>\...` (cwd-relative,
  never `C:` and `Z:`). That covers the closed core's drive-letter layer for Point Blank X (`F:`/`G:` for `nbamsavdat.dll` and
  `Setting.ini` `MAIN_PATH`/`BACKUP_PATH`) and Mario Kart (`G:/Contents`; the `./F...` folders are already in the exe).
* Per-game hooks belong in the game's drop-in, not in the core (`docs/dropins.md`). The loader loads **one** preload DLL per Windows game
  (`TP_PRELOAD_DLL`); extra fixes therefore go into the same DLL (like the pHResult fix in `rg-mfcam.dll`).

---

## Point Blank X (EmulationProfile `PointBlankX`, 32-bit Unity, OpenGL)

**Wat het spel van het I/O-bord wil.** `Assembly-CSharp-firstpass.dll` heeft de klasse `BNUsio` met 48 `[DllImport("bnusio")]`-functies
(36 `usio_*`, 12 `gun_*`); `BNUsioController` (Assembly-CSharp.dll) is de enige gebruiker. Het echte `Plugins/bnusio.dll` (21 KB, MSVC 2013)
opent het USIO-bord via SetupAPI + WinUSB en het vuurwapen via een COM-poort (`Setting.ini`: `GUNPORT 2`); zijn exports zijn cdecl en
ongedecoreerd (gemeten: `ret` zonder argument). Wat het spel leest: `usio_GetSwIn()` (start 1P = `0x02000000`, 2P = `0x01000000`, test `0x80`,
service `0x4000`, omhoog `0x2000`, omlaag `0x1000`, enter `0x200`), `usio_GetCoin(0)` + `usio_DecCoin`, `gun_GetPositionX/Y(1|2)` (tienbits
waarde `<< 6`, zo doet het echte bestand het), `gun_IsTriggered`, `gun_IsInRange`, firmwareversie (moet 120..126 zijn). Zonder bord valt het
spel terug op muis/toetsenbord (`Setting.controller`).

**Wat de kern deed.** De gesloten kern haakt die laag in het spelproces en vult hem uit het gedeelde geheugen `TeknoParrot_JvsState`
(TeknoParrotUi `PointBlankPipe`: offset 8 bitveld, offsets 12/16/20/24 de assen van P1/P2). De afbeelding bit -> schakelaar is gesloten
code; wij hebben haar afgeleid uit wat het spel zelf eist (hierboven).

**Vervanging.** `tools/rg-usio/rg_usio.cpp` = `bnusio.dll` met dezelfde 48 exports (`objdump`: namenlijst identiek aan het echte bestand),
gevoed door een eigen pollende draad op het gedeelde geheugen. De logica staat in `tools/rg-usio/usio_model.h` en is getoetst
(`tests/test_usio_model.cpp`, 544 controles): munt/service = rand omhoog -> teller +1 (zoals de muntgever pulst), `DecCoin` niet onder 0,
assen 0..255 -> tienbits `<< 6` (max `0xFFC0`), y omgekeerd (Unity telt y van onderen; de terugval van het spel gebruikt
`Input.mousePosition / Screen`; `RG_USIO_FLIP_Y=0` zet dat uit). Bouwen: `tools/dev/build-usio.sh` (mingw i686 in de gepinde container,
statisch). Plan met de kandidaat: `rg-direct --plan` is `ok`; `core_source: ravengun`; `dropin_files` = `bnusio.dll` in
`PBX100-2-NA-MPR0-A63_Data/Plugins/` (de map wordt lokaal gemaakt, de NAS blijft ongemoeid).

**Niet gedekt / onzeker.** De richting van y is afgeleid, niet gemeten. `gun_IsInRange` is altijd waar (de daemon meldt geen "buiten
beeld"). De ticketautomaat (`TicketIO`, COM) is een ander subsysteem en bleef zoals op de gesloten kern. Terugslag (`gun_StrikeOnce`) doet niets.

**Test op de tv.** (1) kandidaat activeren: `patches/PointBlankX/game.kandidaat.ini` over `game.ini` en `dropin.kandidaat.d/` naar `dropin/`
(of in de gebruikerslaag `~/.local/share/ravengun/patches/PointBlankX/`). (2) dubbelklik. (3) `Plugins/rg-usio.log` in de view: eerste regel
"rg-usio (bnusio.dll) geladen", dan "gedeeld geheugen TeknoParrot_JvsState geopend" en "eerste aanroep: usio_Initialize ... gun_GetPositionX".
Geen log = het spel las het echte bestand. (4) munt (toets 5) -> credit; start; richt op linksboven: het vizier hoort linksboven te staan,
anders `[Env] RG_USIO_FLIP_Y = 0`. (5) 5 minuten spelen. Bewijs van het laden: `/proc/<pid>/maps` van het spelproces bevat
`Plugins/bnusio.dll` met de sha256 uit `dropin.kandidaat.d/`, en bevat **geen** `TeknoParrot.dll`.

---

## Mario Kart Arcade GP DX 1.18 (EmulationProfile `NamcoMkdx`, 32-bit, D3D10/11)

**What the core did (measured on the exe, not on the core).**

1. **JVS.** `CreateFileA("COM3", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)` at `0x8ac1f2`, then
   `GetCommState`/`SetCommState` (115200 8N1, `0x8ac2ab`), `SetCommTimeouts`, `SetupComm`, `PurgeComm`, `GetCommModemStatus` (sense line).
   The core turns COM3 into `\\.\pipe\TeknoParrot_JVS` (same behavior as `amJvs.cpp` in the OpenParrot source, there for `hookPort = "COM3"`).
2. **ALL.Net.** `CoCreateInstance(CLSID {4603BB03-058D-43D9-B96F-639BE908C1ED}, NULL, CLSCTX_LOCAL_SERVER, IID {045A5150-D2B3-4590-A38B-
   C1158678E1AC})` at `0x4010c0`: that is the COM server **`AMCUS/AMAuthd.exe`** (both GUIDs are in `AMAuthd.exe`; the IID is in the
   proxy/stub file `AMCUS/iauthdll.dll`, which declares 32 methods = IUnknown + **29 own methods**). The game reads `auth_state`,
   `daemon_state`, `daemon_error`, `last_auth_error`, the PowerOn fields (`uri`, `host`, `place_id`, `country`, `region0`, `timezone`) and
   the game's IP from it; if `CoCreateInstance` fails, the game sets the flag byte of the wrapper object to 0 and uses fixed values
   (among others state 8 and -1, `0x461fc5`): that is an intended path, not a crash. `AMAuthd` registers its own class
   (`CoRegisterClassObject`) and knows `authType` = `ALL.NET` | `OFFLINE` | `NBLINE` in `AMConfig.ini`.
3. **The community server.** `Network/ServerAddress`/`ServerPort`/`PlayerId`/`Gateway` exist only in `teknoparrot.ini`. In a
   memory image of the closed core is TeknoParrot's online protocol: signed sessions (`TPAUTH`, `TPSK1`, `TPENC1`,
   HMAC, "the server refused this cabinet", `[Network] OnlineID / OnlineSecret` from the teknoparrot.com account). That is a
   proprietary, authenticated protocol of an external service.
4. Other: drive letters (`G:/Contents`), 16 MiB stack (engine, `StackReserve`), `Polygon2D` ViewLink and d3dx10 pHResult (already ours).

**Replacement.**

* JVS: `tools/rg-mfcam` got a third task: with `RG_JVS_COMPORT=COM3` (in the drop-in) `rg-mfcam.dll` redirects, in the exe's import table,
  `CreateFileA` (COM3 -> pipe, waiting until the bridge has the pipe) and the seven comm calls (they succeed on that handle);
  `GetCommModemStatus` returns 0x10, after address assignment 0x30 (byte 0 of `TeknoParrot_JvsState`, mirrored by `rg-jvsbridge`).
  Decision logic in `tools/rg-mfcam/jvs_com.h`, tested in `tests/test_jvs_com.cpp`. Log: `rg-mfcam.log`.
* Core: `[Loader] Core = ravengun` (generic).
* **Online does not work without the closed core**, and we do not rebuild it: (a) the TPAUTH protocol is that of TeknoParrot's service and requires
  the owner's account; (b) our own `IAuth` (29 methods, output structures that the game reads field by field) cannot be made reliable without a trial with the
  real game; (c) (see also the crash section below: the first TV crash was not IAuth) `AMAuthd.exe` in `OFFLINE` mode would use the game's own code, but requires a
  second-exe start with COM registration (`iauthdll.dll` `DllRegisterServer`, `LocalServer32`) and an `AMConfig.ini` copy with
  `amdcfg-authType=OFFLINE`; whether the game accepts that offline auth has not been measured. The candidate therefore starts the game **without**
  `IAuth`: on the path the game itself knows (flag 0). Whether it then reaches the attract mode or waits at the network check after 2 minutes
  (`ALL.NetCheck_TIMEOUT`, after 7200 = `0x1c20` ticks) is the question for the TV.

### Mario Kart DX: the crash after ~15 s is ShareK, not IAuth (measured statically 2026-10-17)

**What the TV measurement (`WINEDEBUG=+seh`, `direct-MarioKartDX-20261017-151925.log`, lines 1970-1990) really says.** The log is binary
(`grep -a`). The line `*ERR* ComError Code = 80040154` comes from the catch handler at `0x47E8F8`, and that belongs to the function at
`0x47E7D0` (`StartIpcClient` in `..\AppSystem\Network\ShareK\ShareKManager.cpp`; source path at `0x9b2bfc`), not to the `AMAuthd` wrapper. The `CoCreateInstance` in that function
(`0x47E834`) requests **CLSID `{ccc44b2a-709a-33a8-a1b0-5dbe25b19c2c}`, IID `{c2116f7f-cf46-3c68-b187-9d0fe9f70ed0}`**, `CLSCTX` 0x17 (GUIDs at
`0x9b2bbc` / `0x9b2bac`). Those are name-based GUIDs (version 3), as .NET derives them for a `ComVisible` class without `[Guid]`; they appear
as bytes or text in no file of the game folder except the exe (scan of the whole folder, including `ipccl.dll`, `ShareK/*`, `AMCUS/*`). They are the COM class of **ShareK**: `init_w10.ps1` (the real startup chain, UTF-16) does
`RegAsm.exe .\ipccl.dll` (the .NET client) and then starts `ShareK\ShareK.exe` via `sharek.ps1` in a loop; ShareK is the content distribution between
cabinets (`NET_USE_SHAREK`, `NET_SHAREK_CHECK_MASTER_SLAVE_*` in `Data/System/Config/NetConfig.csv`). The registration is missing in our prefix.

| address | what |
|---|---|
| `0x47E7D0` (`StartIpcClient`, `ret` at `0x47E8E3`) | calls `0x47DF90` (Init, re-reads `NetConfig.csv`) and then **always** does `CoInitialize` + `CoCreateInstance` (`0x47E834`), even if Init just set the flag `M+0x394` to 0 |
| `0x47E269`, `0x47E29D` | Init: `_waccess_s(L"G:/Contents")` / `_mkdir("G:/Contents")` fails, logs `Zipダウンロードパス生成失敗.ShareKは強制的に未使用.` (line 1923 of the log) and flag `M+0x394 = 0` |
| `0x47E870` | `call [vtbl+0x1c]` (`StartIpcClient`, `[out] VARIANT_BOOL`), `<0` throws `_com_error` (throw info `0xA32708`) |
| `0x47E8F8` | catch: logs `ComError Code = %08lx` (`0x47E92C`), loads `M[0]` (the interface, still 0 here) and does `0x47E95E: mov ecx,[eax]` -> **access violation** (`call [vtbl+0x20]` follows) |
| `0x481031` | restart (`state == 2`): the same `CoCreateInstance` (`0x48107F`) with the same catch error (`0x48114E`) |

**IAuth (AMAuthd) is a second, separate question.** `CoCreateInstance(CLSID {4603BB03-...}, ctx 4 = LOCAL_SERVER, IID {045A5150-...})` is in
the wrapper constructor `0x401000` (`0x4010C0`) and **catches its own failure**: `hr < 0` -> logs (`0x4010CC`, via `0x6095C0`), `CoUninitialize`,
flag byte `[wrapper+0] = 0` (`0x4010D9`); no exception, no crash. The wrapper hangs off `ds:0xec29fc`; all 151 places that use it first test
`cmp BYTE [wrapper],0` and then use fixed values (`0x461910`: state 8, `0x461f71`: -1). The state machine at `0x461f1a..0x462033` counts
frames in `[esi+0x10]`; at `0x1c20` (7200, 2 minutes) it sets state 6 and only logs `* ALL.NetCheck_TIMEOUT:auth_state:%d daemon_state:%d
daemon_error:%d last_auth_error:%d last_lib_error:%d` (`0x9b08a8`). The game uses nine of the 29 methods (vtable offsets `0x24` (56 places),
`0x28`, `0x30`, `0x34`, `0x38` (PowerOn structure of 0x8a2 bytes: `uri`, `host`, ... at `0x461a47..0x461c9b`, offsets `+0x78 +0x179 +0x27a +0x37a +0x47a
+0x4fa +0x57a +0x59a +0x69a +0x79a`), `0x3c`, `0x4c`, `0x74`, `0x78`; no full signatures, only `this` + an output pointer is proven for
`0x24`/`0x28`/`0x38`). The complete IAuth object is therefore needed for a game that *follows* ALL.Net, not to start up.

**Chosen route (with evidence).** For this crash neither of the two IAuth routes is needed; the cause is ShareK. Chosen: **an own in-process ShareK object**
in `rg-mfcam.dll` (FOURTH TASK, `tools/rg-mfcam/sharek_fake.h` + `rg_mfcam.cpp`), because (1) the cause and the location are proven (table above),
(2) the interface can be read entirely from the call sites (36 used vtable slots in `0x47C000..0x483400`, all argument sizes: 8 bytes, six
methods 20 bytes), (3) an in-process object requires no second exe, COM registration or .NET (Mono) in the prefix, and (4) the behavior behind it is defined by the
game itself: status 2 (`0x47EEA5`, "ネットワーク未初期化により自動復旧待機") makes the tick at `0x47ECD0` skip the work every time
(`0x47EEE3`). `NET_USE_SHAREK = FALSE` in `NetConfig.csv` would also skip the call (`0x47E807` tests the flag), but a file in the view cannot be chosen per core (a
drop-in or `ViewCopies` also applies to Online play) and `[Patch] Kind = file` only touches the loader/core, not a game file. It is, however, the fallback if the object disappoints.

**The object** (`sharek_fake.h`): `CoCreateInstance` in the import table (`ole32.dll`, IAT `0x943870`) redirected; only for that one CLSID does an own object appear,
everything else (including IAuth) goes to the real function and is logged (first 16 calls). Slot 7 `StartIpcClient` -> `VARIANT_TRUE`; slot 33 (+0x84) restart query
-> 0; slot 35 (+0x8c) status -> 2; all other used slots `S_OK` without writing (the caller presets their output to 0: `mov [ebp-0x38],ebx`).
`Release` never counts below 1 (static object). Only on with `RG_SHAREK_FAKE=1` (dropin.ini `Env`) **and** without `TeknoParrot.dll` in the process.

**Tested (without the game).** `tests/test_sharek_fake.cpp` (176 checks: GUID bytes as in the exe, the slot table, 8/20 bytes, only slots 7/33/35 write).
In addition a throwaway program (`sk_probe.exe`, no game, throwaway prefix in the scratchpad, MinGW i686) that loads `rg-mfcam.dll` with `RG_SHAREK_FAKE=1`, asks for the object via
`CoCreateInstance` and calls every used slot as the game does (stdcall, 8 or 20 bytes) with an `esp` check: **0 failed**, IAuth goes to the
real call (`0x80040154`), `QueryInterface` refuses foreign IIDs. This proves the stack balance and the logic; not that the game gets further with it.

**Online play (Core = teknoparrot) is left alone.** (1) `game.ini` is unchanged relative to `patches/MarioKartDX/game.ini`; only `dropin.ini` got
`;RG_SHAREK_FAKE=1`; (2) the DLL does not enable the redirection if `TeknoParrot.dll` or `TeknoParrot64.dll` is in the process (same check as the
JVS redirection; log line `ShareK-object uit: de TeknoParrot-kern is geladen`); (3) `--plan` with `[Loader] Core = teknoparrot` in the user layer requests the core from the
TeknoParrot folder as before (`kern ontbreekt: /nonexistent/TeknoParrot/TeknoParrot.dll` with a folder that does not exist). Not measured: a real Online
play run with this DLL (the owner plays online; the TP core knows ShareK/IAuth's `CoCreateInstance` itself, how is unknown).

**AMAuthd itself as a second exe (route b) — why not, evidence.** `AMAuthd.exe` (280 KB) imports KERNEL32/USER32/ADVAPI32/ole32/WS2_32/IPHLPAPI (no
setupapi, no dongle DLL), but: `SetSystemTime` rights (`AdjustTokenPrivileges`), the ALL.Net client `alAbEx`, MUCHA memories (`muchaut_*`, daemon
`.\MuchaBin\muchacd.exe` on `tcp:0.0.0.0:8765`), and reads `WritableConfig.ini` (`serialID`, `netID`, `mode`) which `PreprocessAmAuth.exe` writes from
`HKLM\SOFTWARE\NBGI` (`ContentsPartitionPath`) and the disk serial number (`init_w10.ps1`: `Get-Disk ... SerialNumber`). `init_w10.ps1` also starts
`AMUpdater.exe -wait` (exit code 1 = wait 60 s). That is five separate components with write actions in `AMCUS/` (NAS: write layer) plus
`regsvr32 iauthdll.dll` in the prefix; none of it can be proven to work statically, and `rg-direct` has no COM registration (`[Patch] registry`
is not applied). Route (a) (own IAuth, same technique as above, nine methods) is the next step if the TV hangs at the
ALL.Net check after 2 minutes; the output structures (PowerOn) are known from `0x4619D0..0x461E49`, the values the game accepts as "OK" have not been measured.

**Not proven.** (i) That the game reaches the attract mode with the ShareK object: there may be a next blocker (the ALL.Net timeout, E05-xx).
(ii) The semantics of the remaining ShareK slots; they are `S_OK`/zero, which the tick (status 2) never reads. (iii) Creating `G:/Contents`: the
game logs `Zip...生成失敗` as long as `_mkdir("G:/Contents")` fails (drive letter G: in the prefix); with ShareK forced to "unused" that is harmless.
(iv) A full 5-minute run.

**Exact TV test recipe (for the coordinator; the bench does not belong to this session).**
1. Activate: `docs/research/namco/patches/MarioKartDX/dropin/{dropin.ini,rg-mfcam.dll}` to `~/projects/ravengun-patches/patches/MarioKartDX/dropin/`
   (the dll is not in git (`*.dll` is ignored) but is in the working tree; rebuild: `tools/dev/build-mfcam.sh --out docs/research/namco/patches/MarioKartDX/dropin`, reproducible, sha256 `3dca574ac7b1f6a97b53c1ec300c499308010c72f8f4de88271b2f7f9418b746`). Make sure the user layer `~/.local/share/ravengun/patches/MarioKartDX/game.ini` for this run
   does **not** have `Core = teknoparrot` (it does now, for the owner's online play; restore it afterwards) and `[Wine] Debug = +seh` for the game messages.
2. Check without TP: `RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan MarioKartDX --patches <folder>` (all patches + this `MarioKartDX`) gives `ok`, `core_source
   ravengun`, `RG_SHAREK_FAKE=1`, no path in the TeknoParrot folder.
3. Double-click in the app. In `rg-mfcam.log` (view): `importtabel: ole32.dll!CoCreateInstance omgezet (eigen ShareK-object ...)`, `ShareK: CoCreateInstance({CCC44B2A-709A-33A8-
   A1B0-5DBE25B19C2C}, ctx 0x17, {C2116F7F-...}) -> eigen object, hr 0x00000000`, `ShareK-object: slot 7 aangeroepen` (and 33, 35), and
   `CoCreateInstance({4603BB03-...}, ctx 0x4, {045A5150-...}) -> hr 0x80040154 (echte aanroep)` (the IAuth, intended path).
4. Game log: **no** `*ERR* ComError Code = 80040154` and no `c0000005` at `0x0047E95E`; but `Startup ShareK-Client.` and `ShareKステータス変化：ネットワーク未初期化...`.
5. Watch 5 minutes of attract/game, also past 2 minutes (`ALL.NetCheck_TIMEOUT` is then in the game log): what does the TV show (network error screen, E05-xx)?
   Proof of loading: `/proc/<pid>/maps` contains `rg-mfcam.dll` and **no** `TeknoParrot.dll`.
6. If it then fails on ALL.Net: first `rg-mfcam.log` (the line of the real `CoCreateInstance` of IAuth), then build route (a) for IAuth.

### Mario Kart DX: the stall dump of 2026-10-18 07:42 read (main loop runs; drive-letter fixup extended)

**What the dump says** (`rg-mfcam.log`, 20 s and 45 s, identical):

* **Main thread.** `EIP win32u.dll+0x11d38` (the syscall stub; Wine gives a thread suspended in a syscall this address), on the stack `0x83D7D6`, `0x77070A/0x77070F`.
  `0x83D7D6` is the return after `call [0x943600]` = **`PeekMessageW`** in the message loop at `0x83D630` (at `0x83D7D4`; the loop after that: `PeekMessageW(PM_REMOVE)`,
  `TranslateMessage`, `DispatchMessageW`, otherwise `call 0x8451E0`); `0x77070A` is the call of that loop from the main function (`0x77070F` = return).
  The main thread therefore is **not** waiting for a window, message or event: it runs the ordinary loop, and `0x8451E0` (the scheduler) apparently finds nothing to do
  (a second dump gave the same EIP and the same ESP: the loop is almost entirely PeekMessage).
* **Workers wait, nobody works.** 20 threads sleep in the worker pool (`Sleep(30)` at `0x8527FB`), four in `SleepEx` polling loops (`0x607C1D`, `0x8040EC` (10 s), `0x5F88C7`, `0x5F534A`),
  the input thread (`0x5F5310`, `SleepEx(8)`, reads the JVS input via `0x8AB990`) and audio threads (`mmdevapi`): that explains the sound stream, not the picture.
* **JVS was never opened.** `COM3` is hard-coded at `0x8AC1B0`, called via `0x8AB880` (only from `0x6022D0`, a state machine that sets `[esi+0x3c] = 2`: the IO check of the
  startup checks). No `JVS: CreateFileA(COM3)` in `rg-mfcam.log` of any run with our core: the startup checks do not reach that step. The input thread does run; `ds:0xec2470` (0 = JVS not open, 1 after opening at `0x8AB8F8`) was not measured.
* **The camera startup (`0x84BEC0`) does run** (it is in `0x770770`, after `StartIpcClient`), but `MFEnumDeviceSources` (`0x84BF5A`) is only called if the camera is on in the test menu.
  That test-menu file (`TestMode/testmode_*.bin`) is never read (see below): so the camera is off. That is the explanation for the missing camera line, not a blocker.
* **37.5 s after the init the game writes a second `bookkeeping_*.bin.tmp`** (00:02:09 in the run of 00:01:31; 07:43:25 in that of 07:42:48) and then nothing more. So exactly one thing happens at
  ~37.5 s (at ~190 loop ticks per second 7200 ticks = the `0x1c20` of `ALL.NetCheck_TIMEOUT`, but that line is not in the game log). Not investigated what it is.

**What this does not prove:** why the picture is black. There is no waiting thread that blocks something; the main loop runs and the scheduler has no work. The first scene (startup checks)
does not get going. Candidates, in order of evidence: (1) the settings/dongle state that is empty on every start (below, measured);
(2) the ALL.Net check without IAuth (state 6 would log an `*ERR*`: not seen);
(3) something in the TP core that presents the game differently (the TP core is closed, not investigable).

**Built: the drive-letter fixup extended (SIXTH TASK of `rg-mfcam.dll`, `drive_fix.h`).** The same rule as patch 0004 (`<L>:\` or `<L>:/` for L not C/Z to `.\TeknoParrot\<L>\rest`) for
the entry points that the core misses and that the game does use: `MSVCR100.dll!rename/_wrename/remove/_wremove/_waccess_s` and `KERNEL32.dll!FindFirstFileA/W/GetFileAttributesW`. On with
`RG_DRIVE_FIXUP=F` (now in `dropin.ini`); only letter F: `G:/Contents` stays as it was (the game turns ShareK off). Off as soon as `TeknoParrot.dll` is in the process (Online play left alone).
Tested: `tests/test_drive_fix.cpp` (24 checks) and a throwaway program under Wine (`rename("F:/system/x.bin.tmp","F:/system/x.bin")` with `msvcr100`, `FindFirstFileA/W`, `GetFileAttributesW`,
`_waccess_s`, `remove`: everything lands in `.\TeknoParrot\F\system\`, `G:` does not; 0 failed). Not tested: the game itself.

**Test recipe (for the coordinator).**
1. Activate the dll and `dropin.ini` from `docs/research/namco/patches/MarioKartDX/dropin/` (build: `tools/dev/build-mfcam.sh --out docs/research/namco/patches/MarioKartDX/dropin`); then `RG_STALL_DUMP=20,45,90`
   again after the `Env` line (or `--env`), `WINEDEBUG=-all,warn+seh`, `DXVK_HUD=fps` (or `frametimes`): a counter on the TV proves that the game calls `Present`.
2. First run: in `rg-mfcam.log` `schijfletter: rename(F:/system/... => ...)` and in `TeknoParrot/F/{system,TestMode,Bookkeeping,DongleData}` now `.bin` without `.tmp` (the old `.tmp` leftovers may stay or go).
3. Second run: the game lines `ShareKSaveErrorInfo/ContentsInfo*/... load failed` must be gone (the files are now found and loaded). Does the picture change? If not: the stall dump of that run.
4. Optional, only if the owner wants his old settings back (camera on, free play): `cp FTestMode/testmode_*.bin TeknoParrot/F/TestMode/` and likewise `FBookkeeping`, `FDongleData`, `Fsystem` to their
   `TeknoParrot/F/` folder (view data, not NAS). Not proven that the game picks the newest file first; therefore not automatic.
5. Black picture remains with `.bin` files and an fps counter: then the next measurement proposal is the TP baseline (the same `warn+seh` run with the TP core; the order of the `*INF*` lines after `Startup ShareK-Client.`
   and the first difference is the answer), or a second series of stall dumps (5, 10, 15, 30 s) to see whether the `0x8451E0` scheduler ever finds work.

### Mario Kart DX: the next step in the startup sequence is the Banapass card reader (static + 08:14 measurement)

**What the 08:14 measurement shows.** The drive-letter fixup works (45 `schijfletter:` lines, the FindFirstFile searches find the adopted TP files); about 31 s after the ShareK init
the game writes a `bookkeeping_*.bin.tmp`, renames it successfully (`rename ... = 0`) and cleans up old `bookkeeping*` leftovers. The main loop is the same (`PeekMessageW` in `0x83D630`), JVS stays closed.
The `system/*.bin` that the coordinator was looking for do not belong to this step: the startup writes only bookkeeping, so their absence says nothing.

**What determines the startup sequence (static).** The only code that opens `COM3` is `0x8AC1B0`, reached only via `0x8AB880` <- `0x6022D0` (state `[esi+0x3c] = 2` in the startup checks at `0x6027D0..0x604xxx`, with `[esi]` = the
system object `ds:0xEC2A08`). In the same family (`0x601360..0x603300`) sits the **Banapass card-reader state machine**: `0x601550` with twelve states (`[reader+0xF0]`, jump table `0x601970`), which calls `bngrw.dll`:
`BngRwInit` (`0x771C7D`, in the init), `BngRwAttach` (`0x601426`, `0x602D5B`, `0x603210`; reader 0 = `COM4`, reader 1 = `COM2`), `DevReset`, `SetLedPower`, `ReqLed`, `ReqBeep`, `ReqWaitTouch`, `ReqCancel`, `ReqSendUrlTo`, `IsCmdExec`.
Every request waits for a callback (cdecl: reader, result, user) that fills the result slot `{result, reader}` starting from `{-1, 2}`; `-301` (`0xFFFFFED3`) or `-400` (`0xFFFFFE70`) is an error that the game logs with code `0x43/0x45`
(`0x6095C0`) and sets the "reader present" flags `[0xEC2A08]+0x194/0x195` to 0. **This is a component that works differently under the TP core than under our core** (TP replaces it, we do not), and it sits right before the step we are missing: the real `bngrw.dll` (in the game folder; strings `SetCommState`, `SetCommMask`, `PurgeComm`) opens `COM4`/`COM2`, which do not exist under Wine; the closed TP core replaces the library (Banapass emulation, card button =
bit 0 of byte 8 of `TeknoParrot_JvsState`). Whether the game without a reader hangs or goes to an error screen after a time-out (~30 s: the second bookkeeping write) has not been measured. **Not proven** that this explains the black picture; it is, however,
the first missing emulation on the path to the IO check, but the order of the startup checks (card reader first, IO afterwards) was derived from the code and is not proven.

**Built: SEVENTH TASK of `rg-mfcam.dll` (`bngrw_fake.h`, `rg_mfcam.cpp`).** `RG_BNGRW=trace` turns the eleven `bngrw.dll` imports into logger wrappers around the real functions (arguments, outcome, and every callback the
library gives, with the result the game gets to see); `RG_BNGRW=fake` answers by itself: two readers that are present and have no card (Attach/DevReset/ReqLed/ReqBeep/ReqSendUrlTo succeed with callback result 0;
ReqWaitTouch stays open until ReqCancel, `IsCmdExec` = 1 meanwhile). Off with the TP core (`TeknoParrot.dll` loaded). Tested: `tests/test_bngrw_fake.cpp` (21 checks) and a throwaway program under Wine against a stub `bngrw.dll` with the
real export names (`_BngRwAttach@24` etc.): both modes, stack check after every call, 0 failed. Not tested: the game itself. The return value/result the game expects after `ReqCancel` and what it wants to do with a card
was deliberately not rebuilt (only "no card").

**Test recipe 1 (first, harmless): trace.** Dropin as now (`RG_BNGRW=trace`), `WINEDEBUG=-all,warn+seh`, `RG_STALL_DUMP=20,45,90`. In `rg-mfcam.log` lines `bngrw: ...`: what does the real library give? `Attach(reader 0, COM4,...) -> 1` and then a
`callback Attach(reader 0): a=0 b=0x...`? A `b=0xfffffed3` or no callback = the reader is missing and the game waits/fails there. If `IsCmdExec(reader N) -> 1` stays, the game is waiting for it.
**Test recipe 2: fake.** `RG_BNGRW=fake` in the `Env`; see whether `JVS: CreateFileA(COM3)` appears (then the reader was the blocker) and whether a picture comes; if not: the new stall dump.
**Test recipe 3: TP baseline (only if 1 and 2 give nothing).** Same dropin, `Core = teknoparrot` (the owner has TP), `WINEDEBUG=-all,warn+seh`, `RG_STALL_DUMP=10,20,30` (the stall dump also works under TP; ShareK/JVS/drive/bngrw are
off then). Look at (a) in what order the game's `*INF*` lines come after `Startup ShareK-Client.` and at which second the first one after that is, (b) whether the main thread at 10/20 s is also in `0x83D7D6` or already in a scene, (c) the time to the camera line and the 500
effect calls in `rg-mfcam.log`. The first difference in the `*INF*` order points to the missing step.

### Mario Kart DX: the card reader is not reached; recipe for the TP baseline (08:38 measurement)

**Measurement 08:38 (`RG_BNGRW=fake`).** `bngrw: Init (nep)` appears 2.4 s after the dll loads, 7 ms before the ShareK line (`BngRwInit` is in the init at `0x771C7D`, so the init runs through to ShareK). After that **not a single** other `bngrw:` line:
the card-reader state machine (`0x601550`) is therefore not called, and the main thread at 20 s and 45 s is unchanged in `PeekMessageW` (`0x83D7D6`, stack identical to that of 07:42). That makes the card reader **not** the blocker: it is
a step that only comes up when something earlier in the startup sequence happens. What that is, is not in our code (the TP core is closed) and cannot be read statically without knowing which task the scheduler (`0x8451E0`) is supposed to get.
Hence the TP baseline: the same game, the same dropin, with the closed core.

**Recipe (user layer `~/.local/share/ravengun/patches/MarioKartDX/game.ini`, next to what is already there: `[Config] Network/PlayerId` and `[Loader] Core = teknoparrot`).** Add:

```ini
[Wine]
Debug = -all,warn+seh

[Env]
RG_STALL_DUMP = 10,20,30,45
```

* `Core = teknoparrot` (already there) is the baseline; leave `RG_SHAREK_FAKE`, `RG_DRIVE_FIXUP`, `RG_BNGRW` and `RG_JVS_COMPORT` in the dropin: `rg-mfcam.dll` turns them off itself as soon as `TeknoParrot.dll` is in the process (log lines `... uit: de TeknoParrot-kern is geladen`). The stall dump does run.
* Set **nothing else** (no `RG_BNGRW`, no other dropin change) and use the same dll (`0e3b6e5d...`) and the same view; the TP run must be exactly the own run with only the core swapped.
* Double-click in the app, let it run 50 s, stop. The owner plays online here: the game may connect.
* Afterwards restore the user layer (remove the two lines `Debug` and `RG_STALL_DUMP`).

**What to send** (two runs: own core as 08:38, and TP):
1. The game messages in order: `tools/dev/game-log-order.py <log-own> <log-TP>` (decodes Shift-JIS, shows the common head and the first difference; without a second argument all messages of one run). The first difference is the first step that does happen under TP.
2. `rg-mfcam.log` of the TP run from the last `rg-mfcam geladen`: the stall dumps at 10/20/30/45 s (is the main thread still in `0x83D7D6` or deeper in a scene?) and whether `MFEnumDeviceSources`/the 500 `D3DX10CreateEffectFromFileA` appear and at which second.
3. Whether there is a picture at 10 s, 20 s and 30 s (and what).

### Mario Kart DX: TP baseline read (2026-10-18 10:46); own core hangs in the first PeekMessageW

**What the TP run shows** (`game-log-order.py`, stall dump 20/45 s):
* **TP changes the game's strings, not the APIs.** The game itself logs the path it uses: under TP `./Fplaydata/1812/`, under our core `F:/playdata/1812/` (the exe constant is at `0x9b2954`). So `F:/` becomes `./F` in memory
  (same length). As a result all files are in the view root (`FTestMode/`, `Fsystem/`, `Fplaydata/`, `GBookkeeping/` ...: the old TP files) and the game uses the same path everywhere (CreateFile, rename, FindFirstFile, ifstream, `_mkdir`).
  The exe contains these F:-/G: strings: `F:/system/` (`0x9b0c88`), `F:/income/` (`0x9b2284`), `F:/playdata/` (`0x9b2724`), `F:/playdata/1812/` (`0x9b2954`), `F:/` (`0x9b5340`, plus `Bookkeeping/...`), `F:/RecordRanking/` (`0x9f4250`), `G:/Contents`
  (`0x9b2d04`) and wide `F:\income\Income_*.bin`, `F:/playdata/1812/`, `F:\playdata\*`, `G:/Contents...`; the `F:\workspace\...` are source paths (__FILE__) and stay; `G:/ H:/ ... M:/` (`0xa11444`) is a drive list.
* **No ShareK lines under TP** and the TP run stays for 20 s in `0x4B6E50` (the load steps of `0x4B7500`, at step 9, `fgetc` per byte through `ifstream`) within the init `0x770770`; at 45 s the main loop runs (scheduler `0x8451E0`, task
  `0x841C31`). So the init takes ~20 s ("NOW LOADING"), and the loop begins after that.
* **Our core is in the FIRST `PeekMessageW` at 20 s and at 45 s** (`0x83D7D4`, before the loop): `EIP win32u+0x11d38`, `ESP`/stack identical in all dumps (07:42, 08:14, 08:38). A loop with ticks would show different places; this is a
  blocking syscall. Our init was done after ~10 s (ShareK line 10 s after the dll loads; the TP run is still in the load steps after 20 s, so TP is slower: see the load steps below) and ran into a `PeekMessageW` that does not return.
  That explains black (no `Present`), JVS closed and the missing `bngrw` calls: the loop never starts.

**Built (EIGHTH TASK of `rg-mfcam.dll`, `drive_fix.h` `patch_strings`).**
1. **The same drive-letter change as TP** (`RG_DRIVE_FIXUP=F`): `patch_strings` turns `F:/` and `F:\` at the start of a string (narrow and wide, in the non-executable sections of the exe) into `./F` and `.\F`, except source paths
   (`workspace`) and the drive list. `RG_DRIVE_STRINGS=0` turns that off. G is left alone (ShareK stays off); the API redirection from the previous step stays as a safety net. Tested: `tests/test_drive_fix.cpp` (41 checks, with the real cases) and a throwaway program under Wine.
2. **`RG_MSGTRACE=1`** (now in `dropin.ini`): wrappers around `PeekMessageW`, `DispatchMessageW`, `ShowWindow`, `SetWindowPos`, `CreateWindowExW`. `rg-mfcam.log` gets: the first five `PeekMessageW` calls with `binnen` and `terug met N na X ms`
   (if there is a `binnen` without `terug`, the call blocks), the thread's windows at the first call (`zichtbaar`, `iconic`, dimensions, foreground/active/focus), `CreateWindowExW`/`ShowWindow`/`SetWindowPos` with the outcome, the first 40 messages
   and every 5 s a summary (only while the loop runs).

**Why the TV run now proves something:** if `PeekMessageW #1: binnen` stays without `terug`, this is a Wine/gamescope problem (window not ready yet, or a wait on the window manager) and not the game's logic; then the next
experiment is to keep the core out of it (other `gamescope` flags, `WINE_NO_...`) or to let the first `PeekMessageW` run only after a delay (like TP's ~20 s load time); if it returns and the loop runs, the summary shows what the loop does.
**Not tested:** the game itself, and whether TP's ~20 s load time (which we do not have) hides something: our init is shorter than TP's (10 s versus > 20 s); if the load steps do less work for us without settings/files, that is a difference that can disappear with the TP files in the view root (now found).

**Test recipe.** `Core = ravengun` (remove `Core = teknoparrot` from the user layer), dropin + dll of this commit, `[Wine] Debug = -all,warn+seh`, `[Env] RG_STALL_DUMP = 10,20,30,45`, `DXVK_HUD = fps`. Double-click, 50 s. Deliver (1) `game-log-order.py` of this run
against the TP run (`...104617`): now `./Fplaydata/1812/` must be equal and the first difference follows; (2) the lines of `rg-mfcam.log` from `berichtenlus volgen aan`: `PeekMessageW #1 binnen/terug`, the windows, the summaries; (3) the stall dumps; (4) picture yes/no.

### Mario Kart DX: the first PeekMessageW really blocks (11:44 measurement); diagnosis and trials

**Proven (11:44, `RG_MSGTRACE=1`).** `PeekMessageW #1: binnen (hwnd 0, 0..0, vlaggen 0x0)` appears 6.1 s after `CreateWindowExW(ex 0x8 [TOPMOST], mkart3, stijl 0x90000000 [POPUP|VISIBLE], 1360x768) -> 0001006e` + `ShowWindow(.., 1)`
(+2.4 s after the load) and directly after the ShareK object; **a `terug` never comes**. At that moment the window `0001006e` is visible, not iconic, foreground = active = focus (rect 0,0 1368x768), plus `Wine IME` and `IME` (1x1, invisible, from imm32).
The call is therefore a blocking syscall in Wine (`win32u+0x11d38`), the main thread is not waiting on game logic; in all dumps there are no wndproc frames of the exe above the call, so a message handler of the game itself does not block.
Other threads: 20 workers in `Sleep(30)` (`0x8527FB`), pollers (`SleepEx`), the input thread, audio; no thread visible in a SendMessage wait of the game itself (only exe frames were shown).

**Candidates for why `NtUserPeekMessage` blocks** (not yet distinguishable):
1. a **low-level hook** (WH_KEYBOARD_LL/WH_MOUSE_LL, for example from `dinput8` or the core's DirectInput hooks, `HookIDirectInput8` is in `OpenParrot.dll`): Wine calls such a hook *inside* the PeekMessage of the installing thread;
   if the hook procedure blocks on a lock that another thread holds, PeekMessage hangs without exe frames;
2. the **X11 driver** doing a roundtrip to the X server (gamescope/Xwayland) in `PeekMessage` that does not answer (window just mapped, 6 s later; TP only gets there after ~25 s, after its long load steps);
3. a **cross-thread SendMessage** to a window of this thread from a thread that is itself waiting (for example the camera or audio) or an **imm32/IME** switch.

**New tooling.**
* `tools/dev/proc-threads.py [NAME]` (read-only from `/proc`): per thread of the game process `wchan`, state, cpu ticks and, if `/proc/<pid>/task/<tid>/syscall` is readable, the syscall with the file behind the fd. **Run it in the middle of the hang** (at 20-30 s).
  `futex_do_wait` on all threads without a socket = a lock deadlock (candidate 1/3); a thread in `poll`/`recvmsg` on the X/Wayland socket = candidate 2.
* The stall dump now also shows, per thread, return addresses in **other modules** (`(module) user32.dll+0x...`, `dinput8.dll`, `OpenParrot.dll`, `win32u.dll`, `kernelbase.dll`) next to the exe addresses. A main thread with `dinput8`/`OpenParrot` above `win32u` confirms candidate 1.
* `RG_PEEK_DELAY_MS=N` and `RG_PEEK_SKIP_FIRST=1` (now in `rg-mfcam.dll`; not enabled in `dropin.ini`): trials that delay or skip the call.

**Test recipe (three short runs; always `Core = ravengun`, `RG_MSGTRACE=1`, `RG_STALL_DUMP = 10,20,30,45`, `WINEDEBUG = -all,warn+seh`).**
1. **Diagnosis** (no trial env): start, wait until 25 s and then run `tools/dev/proc-threads.py` (send the output), let the run continue until after the dump at 30 s. Send the stall-dump block of 20 s and 30 s from `rg-mfcam.log` (main thread plus all threads with `(module)` lines).
2. **Race trial**: `RG_PEEK_DELAY_MS = 25000` in `[Env]`. If after 25 s a `terug` and a loop and picture come, it was a race between window construction and the first PeekMessageW (then: a small fixed delay or waiting for an event in the dll as the real fix).
3. **Skip trial**: `RG_PEEK_SKIP_FIRST = 1`. If the next `PeekMessageW` (flags 0x1) is then also inside without returning, every PeekMessage of this thread blocks (candidate 1 or 2), not just the first.
4. Without gamescope: `[Display] Gamescope = false` (only as measured in run 1; separates candidate 2 from the rest).

### Mario Kart DX: the cause of the black picture is full screen (DXUT), TP runs in a window (static + logs 2026-10-18)

**Proven.** (1) The DXVK log of the TP run (`direct-MarioKartDX-20261018-104617.log`) has no `Setting display mode` and the D3D10 swapchain presents
1360x768 (`Presenter: Actual swapchain properties ... Buffer size: 1360x768`); all four own runs (`...-120315/120437/120559/120720`) log twice
`Setting display mode: 1368x768@60` and never a `Presenter` line. The TP profile sets `Windowed=1` (`teknoparrot.ini` in the view); our core does not read that.
(2) The game is DXUT (D3D10): `0x83D630` is `DXUTMainLoop` (first `PeekMessageW(PM_NOREMOVE)`, then the `PM_REMOVE` loop, otherwise `0x8451E0` =
`DXUTRender3DEnvironment`), `0x86D2..` the DXUT mode enumeration, `0x843D10` the `WH_KEYBOARD_LL` hook of `DXUTAllowShortcutKeys` (only in full screen).
(3) Full screen is a fixed default: the constructor `0x405740` of the settings object (`ds:0xEC29C8`) sets `+0x28 = 1360`, `+0x2c = 768`,
`+0x30 = 1` (`0x4057AD`); only `LAGUAGE_ID` comes from `config.csv`. The byte goes via `[0xEC29C0]+0xC` to the DXUT command line (`0x770656`:
`"-forceapi:10 -fullscreen -forcevsync:1 noerrormsgboxes -forcehal"`, otherwise `-windowed`) and to `DXUTCreateDevice(bWindowed = byte != 1, 1360, 768)` (`0x846808`).
(4) The main thread does not block but runs (proc-threads: `running`/`R`, 6-7 s cpu in 28 s) in the Unix side of `NtUserPeekMessage`.
**Not proven:** which Wine mechanism holds the loop in full screen (mode switch, LL hook or something else); that cannot be seen without a Wine trace.

**Built: NINTH TASK of `rg-mfcam.dll` (`window_mode.h`).** `RG_WINDOWED=1` looks for the pattern `mov [ebx+0x28],0x550; mov [ebx+0x2c],0x300; mov [ebx+0x30],1`
in the exe's code (exactly one hit, also in the original on the NAS) and sets the imm32 to 0: the game then chooses `-windowed` itself, without a mode switch, like TP.
Off with the TeknoParrot core. Tested: `tests/test_window_mode.cpp` (12 checks). Not tested: the game itself.

**Test recipe.** Candidate as the 12:03 runs, plus `RG_WINDOWED=1` in the `Env` of `dropin.ini`. In `rg-mfcam.log`: `venstermodus aan: mov [ebx+0x30],1 -> 0 op VA 0x4057ad`;
in the run log **no** `Setting display mode` and yes `Presenter: Actual swapchain properties` with `Buffer size: 1360x768`; `PeekMessageW #1: ... terug`; picture (NOW LOADING).

### Mario Kart DX: E22-12 "STR PCB 通信エラー" is the steering board on COM1 (static 2026-10-18, after the TV run of 12:35)

**TV 12:35 (window mode):** picture, "NOW LOADING", then the red frame E22-12 "STR PCB 通信エラー". In `rg-mfcam.log` no `JVS: CreateFileA(COM3)`: the IO check is not reached.

**Proven (objdump).** (1) The STR object (constructor `0x5F5F40`) has at `+0x188` a serial connection with port index 1 and baud index 2 (`0x5F5FD9`/`0x5F5FEA`); the serial class
(vtable `0xA11764`) opens `\\.\COM<n>` (table `0xA1166C`) with `FILE_FLAG_OVERLAPPED` (`0x853E1D`), `SetupComm(0x2000,0x2000)`, `GetCommState`, `BuildCommDCBA`
(tables `0x98BC10..0x98BC7C`: `COM1: baud=19200 parity=N data=8 stop=1`), `SetCommState`, `PurgeComm(0xF)`; reading/writing with `ReadFileEx`/`WriteFileEx` and
`WaitForSingleObjectEx(INFINITE, alertable)`. The board is therefore **not** attached to JVS. (2) The read thread (`0x5F57D0`) reads 3 bytes per round (`0x5F5D00`): `"C01"` sets
`+0x49` (connected), `"C06"` clears it, `"E00"` sets `+0x4A`; other `E` codes are errors (`0x56..0x5B`, `0x5C..`, `"E20"` = `0x61`). Only after the startup check
(`+0x4B = 1`, `0x602EC4`) does the game first send 10 bytes per round (`0x5F5C80`). (3) The startup check (`0x602DA6..0x602EE2`): waits for `"E00"`, then for `"C01"`;
after `0x12C0` ticks without a connection error `0x61` (`0x602E39`), with the screen texts `反力ＯＦＦ` and `ステアリングに手を触れないで下さい` (`0x9B5940`, `0x9B5950`). The card reader follows after that (`0x602E8C`).
**Not proven:** that `0x61` is called E22-12 on screen (the link error number -> E code was not followed); `0x61` is, however, the only error this check gives.

**Built: TENTH TASK of `rg-mfcam.dll` (`str_board.h`).** `RG_STR_COMPORT=COM1`: `CreateFileA("\\.\COM1")` returns the client end of an own pipe (overlapped, the way the game
opens it); a thread is the board: `C01`, `E00`, then `C01` by itself as long as the game sends nothing, afterwards a `C01` per 10 bytes. The comm calls succeed on that handle
(the same wrappers as JVS, now converted once for both). Off with the TeknoParrot core. Tested: `tests/test_str_board.cpp` (23 checks). Not tested: the game.

**The choice in the app (owner 2026-10-17: "Choice in the app").** `patches/MarioKartDX/game.ini` now has `[Loader] Core = ravengun` as the
default (without TeknoParrot, offline) and `options.json` an option **Online play** (`ravengun.core`, `launch_env` `RG_LOADER_CORE`, kEnvMap
`[Loader] Core`; Dolphin model: the app writes the choice into the user layer): `ravengun` (default) or `teknoparrot`. The option's text
says that playing online requires TeknoParrot (the folder `TeknoParrotUi-2.0.0.130-linux-x64` in RavenGun's runtime). If someone chooses `teknoparrot`
without that folder, the start refuses with "kern ontbreekt: ...TeknoParrot.dll" (measured with `rg-direct --plan` without the folder). With the closed core
`rg-mfcam.dll` turns off its own JVS redirection (`GetModuleHandle("TeknoParrot.dll")`), so that the core handles COM3 itself as before this change.
`--plan` measured: by default `core: ravengun`, `RG_JVS_COMPORT=COM3`; with the override `core: teknoparrot` (or the refusal if the folder is missing).

**Test on the TV.** (1) activate the candidate (only needed if the default above is unchanged; Mario Kart now has them as the real `game.ini`). (2) double-click. (3) `rg-mfcam.log`: "JVS-omleiding
aan: COM3 -> \\.\pipe\TeknoParrot_JVS", "JVS: CreateFileA(COM3) -> pipe ..., handvat ...", plus the existing camera and d3dx10 lines.
(4) `ravengunctl` shows JVS traffic; steering/gas/Item work as before. (5) what does the game do with the ALL.Net check: attract within ~1 min,
or network error screen? On a blocker: first look at `Data/System/Config/NetConfig.csv` and `AMCUS/AMConfig.ini` (see below).
(6) 5 minutes of attract/game, `/proc/<pid>/maps` without `TeknoParrot.dll`.

**If the ALL.Net check blocks (next steps, not done).** `Data/System/Config/NetConfig.csv` has switches
(`NET_USE_LAN`, `NET_USE_INTERNET`, `NET_IGNORE_ALLNET_MAINTENANCE`, `NET_DUMMY_SERIAL_NO`, `NET_SECURE_FLAG`; in the exe only
`NET_GAMESERVER_ADDRESS`, `NET_IGNORE_ALLNET_MAINTENANCE`, `NET_USE_SHAREK`, `NET_INTERNET_PORT`, `NET_GROUP` appear as key names, so
not all lines of this file are still read) and `DefaultConfig.csv` has `USE_MUCHA`. Those are settings of the game itself,
to be set in the view with `ViewCopies` + a file patch.
---

## Time Crisis 5 (EmulationProfile `TimeCrisis5`, 64-bit UE3, Namco ES3X)

*Stand 2026-10-17 (tweede ronde): **gebouwd en op de host getoetst, niet gestart.** Kandidaat: `docs/research/namco/patches/TC5/`.*

**Statisch gemeten (`TimeCrisisGame-Win64-Shipping.exe`, 25.766.400 B, `objdump -d` van de hele `.text`).** De exe importeert `WINUSB.DLL`
(8 functies), `SETUPAPI.dll` (`SetupDiGetClassDevsW`, `EnumDeviceInterfaces`, `GetDeviceInterfaceDetailW`, `DestroyDeviceInfoList` en de
`CM_*`-functies), `DINPUT8`, `XINPUT1_3` en `hasp_windows_x64_105293.dll`. **Geen van beide exe's importeert `bngrw.dll` of `iDmacDrv64.dll`**; dat die
naast de runtime-lader staan was een eis van de gesloten kern.

**Alles wat het spel van het USIO-bord wil staat in een compact blok `0x140001ca0`..`0x1400037ea` plus `0x140a55130`..`0x140a57620`:**

| Wat | Waar en wat het doet |
|---|---|
| Bord openen | `0x1400032d0`: `SetupDiGetClassDevsW({E54BF304-66A3-476F-A456-C6DDAB8C7078}, flags 0x12)` -> `EnumDeviceInterfaces` (tot `ERROR_NO_MORE_ITEMS`) -> `GetDeviceInterfaceDetailW` (twee keer, `cbSize` 8) -> `CreateFileW(pad, 0xC0000000, deel 3, OPEN_EXISTING, FILE_FLAG_OVERLAPPED)` -> `0x140003490`. De GUID staat in `.rdata` op `0x1414a5ba8`. **Geen `CM_*` hierbij.** |
| Pijpen zoeken | `0x140003490`: `WinUsb_Initialize` -> `QueryDeviceInformation(DEVICE_SPEED)` (moet niet-nul geven) -> `QueryInterfaceSettings(0)` -> `QueryPipe` voor elk eindpunt: bulk (type 2) met bit 7 = IN, bulk zonder bit 7 = OUT, interrupt (type 3) met bit 7 optioneel. Slagen = IN en OUT gevonden. |
| Register lezen | `0x140002f90`: `WinUsb_WritePipe(OUT, {0x10,0x00,adres(16 LE),lengte(16 LE)})`, dan `WinUsb_ReadPipe(IN, lengte)`; beide met een OVERLAPPED en daarna **`WaitForSingleObject(hEvent, INFINITE)`**: de gebeurtenis moet dus gezet zijn. |
| Register schrijven | `0x1400030b0`: `WritePipe(OUT, {0x90, ~(adres>>8)&0xF0, adres, lengte})`, `WritePipe(OUT, gegevens)`, `ReadPipe(IN, 1 byte)` als bevestiging (inhoud niet gelezen). |
| Communicatiedraad | `0x1400026b0`, elke ~10 ms: lees `0x100` @`0x0000`, `0x80` @`0x1080`, schrijf `0x20` @`0x1420`; eenmalig `0x80` @`0x1800`. Status `0` van de drie overdrachten = verbonden (`HasConnected`/`UpdateUSIOForUE`). |
| Opstart (`0x140a553a0`) | foutwoord @`0x0002` moet `0` zijn (anders een foutscherm met eindeloze lus: back-up-, firmware-/IRM1-, geheugen- of systeemfout), schrijft `0x220` bytes @`0x2000`, zet woord @`0x0028` op `0x8173`. |
| Invoer (`UpdateUSIOForUE`, `0x140a56d70`) | schakelaarwoord @`0x1080`: het spel leest de bits **9, 10, 11, 14 en 23** (vijf knopvlaggen die de UE-invoer vullen) en bits 14 en 15 in de muntlogica (`0x140002900`). Muntstanden @`0x1090` en `0x1092`: 14 bits teller + 2 bits status. |
| Aim en trekker | **niet over het bord en (anders dan eerst gedacht) niet via de muis: een geweer op een seriele poort.** Het bord levert alleen schakelaars, munten en uitgangen. De muis (`GetCursorPos`, `0x140a20c50`..) is de terugval (`IPT_Mouse`: relatieve delta's `aMouseX/Y`, geen trekker) als geen enkele COM-poort opent: dat was de tv-proef van 2026-10-18 (menu's liepen op hun timers). Zie de sectie "Het seriele geweer" hieronder. |
| De `CM_*`-boomwandeling | **geen onderdeel van het bord.** `0x1400020b0` (aangeroepen vanuit `0x140a6b8e0`) loopt over de USB-hostcontrollers (GUID `{3ABF6F2D-71C4-462a-8A92-1E6861E6AF27}` = `GUID_DEVINTERFACE_USB_HOST_CONTROLLER`), vraagt via `DeviceIoControl` 0x220424/0x220408 de driver-sleutel en rootnaam, zoekt met `CM_Locate_DevNodeW`/`CM_Get_Child/Sibling/Parent`/`CM_Get_DevNode_Registry_PropertyW(DRIVER)` het bijbehorende apparaat en zoekt een USB-apparaat op VID/PID (0x0B9A en 0x0C10, volgorde niet vastgesteld) waarvan de 12 tekens van een tekenreeks met een "verwachte" waarde (`0x1416f4330`/`0x1416f4350`, `*` = jokerteken) worden vergeleken. Uitkomst in `0x1416f39b8`: 0 gelijk, 1 ongelijk, 2 geen apparaat, 3 meer dan een. Het is een controle van de herkomst van het bord, niet het transport. |

**Gebouwd.**

* `tools/rg-winusb/usio_board.h` (+ `tests/test_usio_board.cpp`, 69 controles, ctest `ravengun_usio_board_tests`): het registerbeeld en het
  6-byte-protocol. `tools/rg-winusb/rg_winusb.cpp` -> **`winusb.dll` (x64)**: de 8 `WinUsb_*`-functies van het spel (+ 4 onschuldige extra's), met
  de OVERLAPPED-gebeurtenis die het spel afwacht. Bij het laden zet hij in de importtabel van het spel vier SETUPAPI-functies en
  `KERNEL32!CreateFileW` om; **alleen de GUID van het bord wordt nagebootst**, al het andere (ook de hostcontroller-GUID en dus alle `CM_*`) gaat
  ongemoeid naar de echte functies. Het "apparaatpad" is een verzonnen pad dat `CreateFileW` als gewone gebeurtenis teruggeeft.
* Waarom `winusb.dll` en geen preload-DLL: de exe importeert `WINUSB.DLL` rechtstreeks, dus `WINEDLLOVERRIDES=winusb=n` plus het bestand naast de
  exe laadt hem bij het starten; zijn `DllMain` draait volgens de laadvolgorde van Wine (eerst binden, dan de initialisatie van de afhankelijkheden) pas na het binden van de importtabel van de exe (verwacht, niet gemeten; het logboek zegt het). Dat laat `TP_PRELOAD_DLL` vrij.
* Aim en trekker: **nagebootste COM1** (zie "Het seriele geweer"): `CreateFileW("COM1")`, `ReadFile`, `WriteFile`, `CloseHandle`, `PurgeComm`,
  `ClearCommError`, `Get/SetCommState`, `Get/SetCommTimeouts` van het spel worden in de importtabel omgezet en gedragen zich op dat handvat als het
  geweer; de assen en de trekker (bit 0x10) komen uit `TeknoParrot_JvsState` (offset 12/16). `SendInput` (absolute muis + linkerknop) is nog
  een noodtrap, standaard uit: `[Env] RG_TC5_AIM = on` / `RG_TC5_TRIGGER = on`; het geweer zelf uit met `RG_TC5_GUN = off`.

**Het seriele geweer (statisch gemeten, 2026-10-18; `tools/rg-winusb/gun_com.h`, test `tests/test_tc5_guncom.cpp`).**

| Wat | Waar en wat het doet |
|---|---|
| Script | `TimeCrisisGame.u` (chunk-gecomprimeerd, LZO; de scripttekst staat erin): `TCMouseInterfacePlayerInput.UpdatePeripheral` laat bij `InputPeripheral == IPT_GunPeripheral` de COM-poort pollen (`UpdateCOMPortForUE`); lukt dat niet (`HasOpenedCOMPortForUE() == false`), dan `InputPeripheral = IPT_Mouse` en `GunDeviceConnectError`. `UpdatePlayerInput`: `AdjustPosition(X1/1024, (0x3ff-Y1)/1024)` (ijking 5 of 8 punten, `config(GunPeripheral)`), `SetMousePosition(X,Y)`, `SetPeripheralButton(Trigger1)`. Commentaar in het script: "op de kast is het COM2, op de pc zoeken we vanaf COM1". |
| Openen | `0x140a54c30`: `CreateFileW(naam, 0xC0000000, 0, NULL, OPEN_EXISTING, 0, NULL)` voor COM1, COM2, COM15, COM14 .. COM3 (UTF-16 tabel `0x14140a868`, 16 bytes per naam, geen `\\.\`); `GetCommState`, `SetCommState` (38400 baud, 8 databits, oneven pariteit), `GetCommTimeouts/SetCommTimeouts`, `ClearCommError` (`cbInQue` leegmaken), dan een eerste transactie; faalt die: `CloseHandle`, volgende naam. Handvat in `0x1416f2f70`; toestand in `0x1418604d8` (1 open, 3 communiceert, 4 verbroken na 120 mislukte transacties). |
| Transactie | `0x140a547b0`: `WriteFile(1 byte code)`, `ReadFile` tot de lengte vol is, controlesom = `~som(alle bytes ervoor)` in de laatste byte. Tabel opdracht -> {code, lengte} op `0x1416f2f80`: 0:{0x08,11} 1:{0x10,2} 2:{0x1c,11} 3:{0x14,2} 4:{0x18,2} 5:{0x0c,2} 6:{0x04,2} 7:{0xff,2}. Ontleding: sprongtabel `0x140a54ac0`. |
| Poll | `0x140a54f90` (draad `0x140a67a90`, `PurgeComm(h, 0xC)` ervoor): opdracht 0, antwoord `X1(16 BE) Y1 X2 Y2 vlaggen(b8) b9 controlesom`. Vlaggen: bit 5 = `Trigger1` (`+0x2fc`, vastgeklikt tot het spel hem leest), bit 4 = `Trigger2` (`+0x304`), bit 7 = buiten beeld (`+0x2f8` bit 1). Naar de speler: `0x140a57100` (`+0x2e8` X1, `+0x2ec` Y1, `+0x2f0` X2, `+0x2f4` Y2). |
| Versie | `0x140a54ae0` (alleen met de debugvlag `0x1418604f4`): `WriteFile(0x10)`, 31 bytes met als eerste `'R'`. |
| Niet bewezen | de ruwe richting en het bereik van X1/Y1 (aangenomen: 10 bits, Y1 = 0 onderaan zoals `0x3ff - Y1` suggereert, en een ijking die ongeveer de identiteit is: de gecookte standaardwaarden zijn niet uitgelezen); P2 (X2/Y2/`Trigger2` bestaan in het kader, het script gebruikt ze niet; de tp-shm-indeling van TC5 heeft alleen P1). `RG_TC5_GUN = x0=..,x1=..,y0=..,y1=..,flipy=0/1` past het aan zonder herbouwen. |
* Kandidaat `docs/research/namco/patches/TC5/`: `game.ini` met `[Loader] Build = ravengun`, `Core = ravengun` (de oude `PostStartMs = 3000`
  hoorde bij de gesloten kern), `dropin/dropin.ini` (HASP zoals voorheen + sectie `[winusb.dll]`, `To = .`, `DllOverride = native`),
  `dropin/winusb.dll` (gebouwd met `tools/dev/build-winusb.sh`; `*.dll` staat in `.gitignore`, bouw hem opnieuw). `rg-direct --plan` is `ok`,
  `core_source: ravengun`, nul verwijzingen naar de TeknoParrot-map (`tools/dev/namco-candidates-check.sh`).
* Het spel start via `rg-direct` **rechtstreeks** `TimeCrisisGame-Win64-Shipping.exe` (niet `RSLauncher.exe`); de schijfletterlaag en
  `nbamsavdat.dll` van de launcher spelen dus niet mee.

**Niet bewezen / gegokt (in volgorde van waarschijnlijkheid dat het de eerste tv-proef breekt).**

1. **De bitafbeelding van pedalen en gunknop.** Wat het spel leest is gemeten (bits 9/10/11/14/23), wat elke bit fysiek is niet. Vastgesteld uit de
   Point Blank X-bibliotheek van dezelfde USIO-familie: test `0x80`, service `0x4000`, enter `0x200`. **Gegokt:** linker pedaal `0x800`, rechter `0x400`,
   gunknop `0x800000`. De DLL logt elke verandering van het schakelaarwoord; `[Env] RG_TC5_BITS = pedal_l=0x400,pedal_r=0x800,...` verandert de
   toewijzing zonder herbouwen.
2. **HASP.** De drop-in `ravengun-hasp` heeft nog een leeg `memory`-beeld (`dongle.json`); op de gesloten kern werd hij nooit aangeroepen
   (de kern vangt hem af). Zonder die kern moet het spel hem wel gebruiken. Het logboek van de HASP-laag (`RAVENGUN_HASP_VERBOSE=1`, stderr van de lader)
   zegt welke aanroepen de dongle krijgt.
3. **De controle op herkomst van het bord** (`0x1400020b0`). Wine geeft voor de hostcontroller-GUID geen controllers (verwachting, niet gemeten), dus
   de uitkomst is "geen apparaat" (2). Wat het spel daarmee doet (alleen de testmenu-regel, of ook een foutscherm) is niet vastgesteld. Als het een
   blokkade is, is het volgende werk een nagebootste hostcontroller met `DeviceIoControl`-antwoorden; dat is bewust niet gebouwd (het is een controle op
   de identiteit van het bord en vraagt een besluit).
4. **Aim/trekker via `SendInput`** (zie boven) en of het spel de cursor elke tik naar het midden zet.
5. **Munt:** standaard blijft `CoinKeys = lshift,t` (Shift+T, op de gesloten kern gemeten). Het bord telt de munt-bit niet (`coin = false`) om dubbel
   tegoed te voorkomen; `RG_TC5_BITS = coin=1` zet de teller @`0x1090` aan.
6. De bevestigingsbyte van een schrijfopdracht (nu `0x00`) en de waarden van het register `0x0000..0x00FF` (nu nul; alleen het foutwoord @2 is nodig).

**Test op de tv (exact).**

1. `cmake -S . -B build && cmake --build build -j` en `tools/dev/build-winusb.sh` (zet `winusb.dll` in `docs/research/namco/patches/TC5/dropin/`).
2. Kandidaat activeren: de inhoud van `docs/research/namco/patches/TC5/` (`game.ini`, `dropin/`) in de gebruikerslaag
   `~/.local/share/ravengun/patches/TC5/`, of controleren met `tools/dev/namco-candidates-check.sh`. Controle: `RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan TC5 --patches <map>`
   -> `verdict ok`, `core_source ravengun`, `dropin_files` bevat `winusb.dll`.
3. Dubbelklik in de app. Daarna in `~/.local/share/ravengun/runtime/views/TC5/TC5/Binaries/Win64/rg-winusb.log`: **eerste regel** "rg-winusb (winusb.dll) geladen";
   vijf regels "importtabel: ... omgezet"; "SetupDiGetClassDevsW(USIO-GUID ...)", "CreateFileW(USIO-pad) -> handvat", "eerste aanroep: WinUsb_Initialize" ..
   "WinUsb_QueryPipe"; daarna "WinUsb_WritePipe"/"ReadPipe" en, na de eerste beweging, "invoer: schakelaarwoord ...". Geen log = het spel las het echte `winusb.dll`
   (controleer `WINEDLLOVERRIDES` in het plan en dat de view van TC5 gematerialiseerd is).
4. Bewijs van het laden: `grep -E 'winusb|TeknoParrot' /proc/<pid van TimeCrisisGame>/maps` toont `winusb.dll` met de sha256 van de drop-in en geen `TeknoParrot*.dll`.
5. Spel: attract -> munt (Move-knop, de daemon geeft Shift+T) -> "SELECT A GAME MODE". Bij de eerste aanwijzing: staat het vizier waar de Move wijst? Trekker, elk pedaal
   apart (kijk bij elke knop welke bit in het logboek verschijnt en wat het spel doet), 5 minuten spelen. Staat er een foutscherm (Japanse tekst "ファームウェアエラー"),
   dan heeft het spel het foutwoord niet nul gelezen: stuur het logboek.
6. Terug naar de gesloten kern: `[Loader] Build = teknoparrot` + `Core = teknoparrot` en de sectie `[winusb.dll]` weg.

---

## HOTD Scarlet Dawn (EmulationProfile `ALLSHOTDSD`, 64-bit UE4, twee exe's)

*Besluit eigenaar 2026-10-17: RavenGun mag de platformgegevens van `amdaemon.exe` zelf leveren.*
### Actuele stand — 18 oktober 2026

**Gedeeltelijk werkend zonder TeknoParrot, nog niet volledig speelbaar verklaard.** Deze metingen vervangen de oudere blokkadestatus hieronder; die blijft staan als onderzoekshistorie.

- Gewone RavenGun-start van AMDaemon en Hodzero met eigen gebouwde OpenParrot-kern en `rg-allsio.dll`. Gecontroleerd startplan `direct-HOTDSD-20261018-174739.plan.json`: `tp = ""`, `core_source = "ravengun"`, `tp_files = []`. Mappingcontrole van beide processen toont onze DLL en eigen OpenParrot-build; de proprietary runtime-map bleef hernoemd naar `.verborgen`. Dit was geen volledige bestandsisolatie.
- Afzonderlijke synchrone COM1-sensoradapter in Hodzero, naast de overlapped USB-IO-adapter in AMDaemon. Werkelijke seriële instelling: 38400/8O1. Sensorprotocol, fragmentatie, checksum en assen: **237 controles, 0 fouten**.
- Het spel consumeerde aanvankelijk de lege muiscoördinaten (selector 1). Effectieve gebruikersconfiguratie: `AppData/Local/Hodzero/Saved/Config/WindowsNoEditor/Game.ini`, sectie `/Script/Hodzero.AppGameInstance`, `DevelopSetting_PlayerGunDevice=0`. Daarna selector **0** in de werkelijke coördinatenfunctie gemeten; de seriële en gekalibreerde richtcoördinaten worden gevuld.
- Echte spelersrondes met Entry → StageSelect → Stage0 → GameOver gelogd. Middensalvo: zichtbare schoteffecten, hulzen en een zombie met **AMAZING**. Losse links/rechts-proeven op het continue-scherm tellen niet als schietbewijs; de meerpuntscontrole is nog niet afgerond.
- **Duurproef 18:16:49–18:21:56 +02:00 (5:07):** Hodzero PID 3353404 en AMDaemon PID 3353347 bleven dezelfde processen, zonder debugger of bediening in dat interval. Screenshots per minuut; minuut 3 toont agent ranking, minuut 5 de attract-titel. Bestaande sessie, géén verse startproef. Bewijs: `/tmp/rg-hotdsd-independent-proof-{start,minute-1,minute-2,minute-3,minute-4,minute-5}.png`; meetlog `/tmp/deepcode-background/bash-9165c6ca-7ef9-4e09-acfc-ba1269552e00.log` (tijdelijke lokale artefacten).
- Automatische moduscorrectie toegevoegd achter expliciete opt-in `RG_ALLSIO_GUN_CONFIG=1` (alleen bij sensoradapter in Hodzero). Op gewone herstart getest vanaf modus 1: modus werd 0, een andere testsectie bleef behouden en `Game.ini.ravengun-backup` bevatte de oorspronkelijke modus 1. Dit is nog kandidaatcode; gebruikersconfiguratie niet handmatig vervangen of kalibratie wissen.
- Start is **niet betrouwbaar reproduceerbaar**: hostrapport `0x0080` en native Start-handler aangetoond, maar niet iedere puls begint een ronde. Laatste handlerproef kwam in **Mode Select** (`rg-hotdsd-handler-start.png`), met debugger; geen duurproef van deze nieuwe build.

**Nog doen:** betrouwbare Start/menu/selectie, gerichte treffers links/midden/rechts in een echte ronde, fysieke PS Move, P2 en extra knoppen, plus een verse gewone start en duurproef van de uiteindelijke levering. Actieve lokale kandidaat blijft `Status = unknown`; oude `working`-metadata van de gesloten kern is géén bewijs voor deze route.

### Historie — 17 oktober 2026

*Kandidaat via ravengund gestart, beide exe's laden onze kern en rg-allsio.dll. Wit beeld en fout 4105; toen NIET speelbaar. Kandidaat: `docs/research/namco/patches/HOTDSD/`.*

**Actuele meting.** `direct-HOTDSD-20261017-214823.log`: registerimport en preload slagen, de besturingspijp levert 64 bytes. `OSVersion` op de hoofdsleutel `HKLM\System\SEGA\SystemProperty` wordt via `RegQueryValueExA` gelezen (`0x14022edc0`), daarna als `%u_%u_%u` ontleed. De expliciet synthetische benchwaarde `1_0_0` verwijdert de bijbehorende fout. `amLibGetMainSerialId` (`0x140234030`) gebruikt `GetComputerNameA` en eist 15 tekens. `RG_ALLSIO_MAIN_ID=RAVENGUN0000001` haakt alleen de daemon-API aan; de hostnaam en gedeelde Wine-computernaam veranderen niet. De serial-id-fout verdwijnt. De validator verwerpt ongeldige waarden en behoudt dan de echte API; 179 protocol-/identiteitscontroles slagen.

**Resterende blokkades.** `amDongleInit -16`, `amHmInit -5`, ongeldige game-id en afhankelijke NVRAM-fouten blijven. De donglelaag opent `\??\FddDriver` (`0x14025e120`, `CreateFileW`, overlapped, lezen/schrijven); apparaatprotocol en hardwaremonitor zijn niet nagebouwd. Fout 4105 is hiermee nog niet exact aan één oorzaak gekoppeld. Het DLL-log bewijst hookinstallatie en pijpontvangst, niet dat HID-enumeratie of bordinitialisatie al is bereikt. Geen vijf minuten attract/spel en geen live bewijs met hernoemde TP-map. Tijdelijke actieve user-overrides en benchmuis zijn na de proeven uitgeschakeld; het werkende meegeleverde profiel is niet vervangen.

**Waarom `amdaemon.exe` zonder kern in 1 s stopt: gevonden (eerste ronde).** `am::platform::Manager::initialize()` (`amdaemon.exe` `0x1401e66a0`) roept
`amPlatformGetPlatformInformation()` (`0x14022eec0`) aan; die leest `HKLM\System\SEGA\SystemProperty\Static`, waarde **`PlatformId`** (4 tekens,
`ACA0` volgens `pxGetHwInfo.ini`). Ontbreekt die, dan volgt "ErrCode 0". `data/platform.reg` zet hem (`[Launch] Registry =`, mechanisme `docs/registry.md`).

**Het USB-IO-bord: nu in kaart gebracht (`amdaemon.exe`, libam `amUsbio` Rev 7255, `0x140230790`..`0x140233570`).**

| Wat | Gemeten |
|---|---|
| Zoeken | `0x14026f4a0`: `SetupDiGetClassDevsW(HID-GUID uit HidD_GetHidGuid, flags 0x12)`, `EnumDeviceInterfaces(index)`, `GetDeviceInterfaceDetailW`; eindigt met `ERROR_NO_MORE_ITEMS` -> `0x81000015`. `0x140232f80` zet elk pad in kleine letters en eist **`vid_0ca3`** (Sega). |
| Openen | `CreateFileW(pad, 0xC0000000, deel 3, OPEN_EXISTING, 0x40000080 = overlapped)`; `0x1402330d0`: `HidD_GetManufacturerString` moet met **"SEGA"** beginnen, `HidD_GetProductString` is de node-informatie. |
| Productstring (`ParseNodeInfo` `0x140231860`) | **7 velden gescheiden door `;`, dan `;` en een staart.** Velden: (0) naam, **exact `I/O CONTROL BD`** (< 16 tekens); (1) model < 9 tekens; (2) hex-byte; (3) hex-byte; (4) hex-woord; (5) tekst < 6 tekens; (6) hex-byte. Staart: items met `_` ertussen, `NAAM=waarde[,waarde]` in hex: `GOUT=n`, `PWMOUT=n`, `ADIN=n,m`, `ROTIN=n`, `COININ=n`, `SWIN=spelers,knoppen`, en `UQ1=`, `UQ2=`.. (commando + tot 3 parameters). `config.json` eist munt 2 sloten, schakelaars speler 2 met 8 knoppen, uitgang 20, eigen commando 65 met parameter 6. |
| Rapporten | 64 bytes. Verzoek: byte 0 = **0x10**, byte 1 = commando (0x01 time-out, 0x02 samplingtelling, 0x03 wis status, 0x04 algemene uitgang, 0x05 PWM, eigen UQ-commando's over de **interrupt**pijp: `WriteFile`/`ReadFile` overlapped; 0x84 modus, 0x85 firmware, 0x88 reset (arg 0xD9) over de **besturing**spijp: `HidD_SetOutputReport`/`HidD_GetInputReport`). Antwoord/ingang: byte 0 = **0x01**; ingang = 63 bytes erna. `RecvAcknowledge` (`0x140232c30`) is een gewone overlapped lezing; de daemon leest in een lus tot "nog niet klaar" (`GetOverlappedResult` = `ERROR_IO_INCOMPLETE`) en eist **minstens een rapport** per ronde (`0x1401eb813`). |
| Indeling van de ingang | volgens het bestaande AllsUsbIo-rapport (`packaging/control-pipe-profiles.json`): analoog 0-15, draaiknoppen 16-23, muntteller 24-27, schakelaars speler 1 28-29 en 2 30-31. |

**Gebouwd.**

* `tools/rg-allsio/allsio_proto.h` (+ `tests/test_allsio_proto.cpp`, 170 controles, ctest `ravengun_allsio_proto_tests`): de productstring (getoetst tegen de eisen
  hierboven), de rapportindeling, het afbeelden van de AllsUsbIo-pijp op het ingangsrapport.
  `tools/rg-allsio/rg_allsio.cpp` -> **`rg-allsio.dll` (x64)**: een preload-DLL die alleen in `amdaemon.exe` iets doet. Hij zet in de importtabel van de
  daemon `hid.dll` (5 `HidD_*`), `setupapi.dll` (4 functies; **alleen de HID-GUID**, de echte HID-apparaten van de machine blijven onzichtbaar) en `kernel32`
  (`CreateFileW` voor ons pad, `ReadFile`/`WriteFile`/`GetOverlappedResult`/`CancelIo` voor ons handvat) om. Een draad leest `\\.\pipe\TeknoParrotPipe` (de pijp
  die `rg-direct` al voedt, het AllsUsbIo-rapport); een tikdraad (2 ms) levert het nieuwste rapport aan de daemon (rapport-id 1 + pijpbytes 0..62).
* Kandidaat `docs/research/namco/patches/HOTDSD/`: `game.ini` (`[Loader] Core = ravengun`, zonder `RemoteThread =`: de standaardroute van de lader voor een eigen
  kern), `data/platform.reg`, `dropin/dropin.ini` (`[rg-allsio.dll]`, `To = .`, `Preload = true`) en `dropin/rg-allsio.dll` (`tools/dev/build-allsio.sh`; `*.dll` is
  genegeerd door git). `rg-direct --plan` is `ok`: kern `ravengun`, `TP_PRELOAD_DLL` naar `rg-allsio.dll`, starts `amdaemon.exe` en na 5 s `Hodzero-Win64-Shipping.exe`,
  nul verwijzingen naar de TeknoParrot-map (`tools/dev/namco-candidates-check.sh`).
* De twee-exe-start, de `AmDaemon`-antwoorder van `rg-direct` (voor de Ring-titels) en de schijfletters uit patch 0004 zijn ongewijzigd en niet nodig voor het bord.

**Niet bewezen / gegokt.**

1. **De tekst van de staart** (`GOUT=14_ADIN=8,E_ROTIN=4_COININ=2_SWIN=2,E_UQ1=41,6`): de syntax is uit de code gelezen, de waarden van `ADIN`/`ROTIN`/`SWIN` zijn afgeleid van de
   eisen in `config.json` en van wat ik van dit bord meen te weten. Of de daemon "gelijk" of "minstens" eist is niet gemeten.
2. **De antwoorden op de verzoeken.** Besturingsverzoeken krijgen `{0x01, commando, 0...}` terug; de interrupt-verzoeken (time-out, sampling, wis status, uitgangen) krijgen
   **geen** antwoord. Of de daemon op die antwoorden wacht is niet vastgesteld; het logboek laat elk verzoek zien (de eerste 24 bytes in hex).
3. **Byte-uitlijning van het ingangsrapport** (pijpbyte k op rapportbyte k+1). Als START/trekker verschoven reageren: `RG_ALLSIO_*` bevat daar nog geen knop voor; de logregel
   "eerste AllsUsbIo-rapport" en de daemon-log laten zien wat de daemon leest.
4. **Cadans.** De daemon wil per ronde minstens een rapport; bij een te lage periode (`RG_ALLSIO_PERIOD_MS`, standaard 2 ms) kan een ronde leeg zijn.
5. **De schijfletters AMFS/D/Y.** `HKLM\System\SEGA\SystemProperty\mount` wordt door de daemon zelf geschreven; of `<L>:\` van patch 0004 volstaat is niet vastgesteld
   (zonder kern raakte de daemon de mountlaag niet, A3g). Het eerste logboek zegt het: `amdaemon.log` (met `-l`).
6. De sleutelchip-/dongle-laag (`\??\FddDriver`) is in de bench van 21:48 een actuele blokkade: `amDongleInit -16`; de hardwaremonitor meldt daarnaast `amHmInit -5`. Geen van beide apparaatprotocollen is nagebouwd.

**Test op de tv (exact).**

1. `cmake -S . -B build && cmake --build build -j` en `tools/dev/build-allsio.sh`.
2. Kandidaat: de inhoud van `docs/research/namco/patches/HOTDSD/` (`game.ini`, `data/`, `dropin/`) in `~/.local/share/ravengun/patches/HOTDSD/`; controle
   `RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan HOTDSD --patches <map>` of `tools/dev/namco-candidates-check.sh`.
3. Dubbelklik. In `~/.local/share/ravengun/runtime/views/HOTDSD/App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64/rg-allsio.log`: "rg-allsio geladen in amdaemon.exe";
   14 regels "importtabel: ... omgezet" (geen "NIET in de daemon" voor `hid.dll`/`setupapi.dll`); "SetupDiGetClassDevsW(HID-GUID", "CreateFileW(USB-IO-pad)", dan
   "verzoek #1 ..." (de eerste is waarschijnlijk 0x88 reset of een time-out), "besturingspijp ... geopend" en "eerste AllsUsbIo-rapport".
   In het daemon-logboek: `Process state: setup Game Mode` zonder `amw_platform_manager.cpp`-uitzondering, daarna `Process state: Advertise` en **geen** `UsbioNode`/`amUsbio`-fout.
4. Spel: START (Move), trekker, subtrekker, controllerkeuze, munt, test: werkt het zoals op de gesloten kern (8:44 min aaneen gemeten)? 5 minuten attract of spel.
   Bewijs van het laden: `/proc/<pid van amdaemon>/maps` bevat `rg-allsio.dll` en niet `TeknoParrot64.dll`.
5. Blijft de daemon bij het bord hangen: stuur `rg-allsio.log` en `amdaemon.log`; de verzoeken in het logboek zijn precies wat nog ontbreekt.

---

## Bestanden

| Pad | Wat |
|---|---|
| `tools/rg-usio/usio_model.h`, `rg_usio.cpp` | USIO-model (getoetst) en `bnusio.dll` |
| `tools/dev/build-usio.sh` | bouwt `bnusio.dll` in de mingw-container |
| `tools/rg-mfcam/jvs_com.h`, `rg_mfcam.cpp` | JVS-over-COM-logica (getoetst) en de derde taak van `rg-mfcam.dll` |
| `tools/dev/build-mfcam.sh` | kopieert nu ook `jvs_com.h` |
| `tests/test_usio_model.cpp`, `tests/test_jvs_com.cpp` | ctest `ravengun_usio_model_tests`, `ravengun_jvs_com_tests` |
| `patches/PointBlankX/`: `game.kandidaat.ini`, `dropin.kandidaat.d/`; `patches/MarioKartDX/` (nu de echte `game.ini`, `dropin/`, `options.json`); `patches/HOTDSD/` (since 2026-10-20 the shipped profile itself: `game.ini`, `dropin/`, `data/platform.reg`; its separate `game.kandidaat.ini` is gone) | kandidaat-`game.ini` + drop-ins (ravengun-patches, ongecommit) |
| `docs/research/namco/PointBlankX.game.ini`, `MarioKartDX.game.ini`, `HOTDSD.game.ini` | dezelfde `game.ini` in deze repo |
| `tools/rg-winusb/usio_board.h`, `rg_winusb.cpp`, `tools/dev/build-winusb.sh`, `tests/test_usio_board.cpp` | TC5: USIO-registerbeeld + 6-byte-protocol (getoetst) en `winusb.dll` (x64) |
| `tools/rg-allsio/allsio_proto.h`, `rg_allsio.cpp`, `tools/dev/build-allsio.sh`, `tests/test_allsio_proto.cpp` | HOTDSD: ALLS USB-IO-protocol (getoetst) en `rg-allsio.dll` (x64, preload) |
| `docs/research/namco/patches/TC5/`, `docs/research/namco/patches/HOTDSD/` | volledige kandidaten: `game.ini`, `dropin/` (met `dropin.ini`), HOTDSD ook `data/platform.reg`; de DLL's zijn door `.gitignore` uitgesloten en worden met de build-scripts gemaakt |
| `tools/dev/namco-candidates-check.sh` | legt de kandidaten over de echte patches-map (tijdelijke kopie) en maakt een `rg-direct --plan` zonder TeknoParrot-map |

### Follow-up measurement 2026-10-18/19: what ShareK needs (and where the picture stays black)

The two things that `init_w10.ps1` does on the cabinet have now also been measured here:

1. **`ShareK\ShareK.exe` really requires .NET, not mono.** Under wine-mono it falls over immediately:
   `System.NullReferenceException at ShareK.Program.Main [0x0002d]` (also standalone, with `ShareK.exe.config`
   next to it; presumably mono stumbles over `ConfigurationManager.AppSettings`). With
   `winetricks -q dotnet48` in the prefix **it does run** (stays alive, rewrites
   `E:\WritableConfig.ini`, logs `ntsync: up and running`). Note two things: winetricks sets the
   Windows version of the prefix to win7 (this prefix was on 6.3/win8.1 -- restore with
   `winetricks -q win81`, measured), and the installation takes ~2 minutes and ~1 GB.
2. **The COM registration was indeed missing.** `RegAsm.exe /codebase ipccl.dll` says "Types registered
   successfully"; before that the game stopped immediately as soon as our fake client was off, afterwards it keeps
   running (>70 s).

Furthermore: the engine can start `ShareK\ShareK.exe` as a second program (`HasTwoExecutables`,
`LaunchSecondExecutableFirst`, `[Identify] Path2`, `[Launch] SecondWait`), outside gamescope and in
the same view, prefix and environment. With the fake client on (`RG_SHAREK_FAKE=1`) the game keeps
waiting: that fake returns -1 on slot 7 (`StartIpcClient`) and slot 35 (status) is never
called, so the client never gets any further. **The picture stays black**, even with the real .NET,
the registration, the real client and the server next to it: the next question is what the client is waiting for
(does `ShareK.exe` listen on the port/pipe that `ipccl.dll` looks for? `ShareK` writes no
`ShareKLog.log`; `LogPath = E:/` in `ShareK.exe.config` is a writable drive on the cabinet and
`E:` is not mapped for us).

## ShareK IPC (Mario Kart Arcade GP DX): where the game keeps waiting (2026-10-19 03:45)

The own ShareK client (`rg-mfcam.dll`, `tools/rg-mfcam/sharek_fake.h`) **is** used: the
log shows `CoCreateInstance({CCC44B2A-709A-33A8-A1B0-5DBE25B19C2C}) -> eigen object` and
then `ShareK-object: slot 7 aangeroepen` (`StartIpcClient`, answer VARIANT_TRUE).

**But slot 35 (the ShareK status) is never called** (zero times in the entire log, over three
runs). The game's tick (`0x47ecd0`) only asks for the status when the client has finished starting up,
and that point is never reached. The conclusion that follows: the client normally reports the connection
**back** to the game (a callback/event from the client DLL to the game), and that
notification never comes from our empty object. That is why the picture stays black, regardless of the status value.

What exists now:
* `RG_SHAREK_STATUS` in `tools/rg-mfcam/sharek_fake.h` chooses the status that slot 35 returns
  (default **1** = "配信機能OFF": the game cleans up the distribution once and then simply does
  work; 2 = "waiting for the network" was the old fixed value and makes the tick do nothing; 0 = normal
  work). This is a knob for the next step, not a solution: with the callback not there, the
  tick never gets to the status.
* Measured with a run of 75 seconds: the game keeps running (no crash), `rg-mfcam.log` shows the
  CoCreateInstance and slot 7, and the picture stays black (`bench-captures/MarioKartDX/`).

**Next step (precisely):** find out which callback the game hands to the client. Look at the
arguments of slot 7 (`0x47e892` in the exe: `StartIpcClient([out] VARIANT_BOOL*)`) and at the
used slots next to it -- the client almost certainly gets an interface pointer from the game (an
event sink) that is called on "connected". As long as that is not imitated, the game stays
on the black picture. The alternative remains the real `ShareK.exe` (with dotnet48 in the prefix, that is
measured) as a second program, but that aborted the run (docs/research/namco/README.md, older
part) and requires the engine's second-program route.

## ShareK IPC, second measurement: the gatekeeper, the sampler and the AM daemon (2026-10-19 04:20-05:05)

Continuation of the black picture. With an own measuring thread (the sampler of `rg-compat`,
`RG_COMPAT_SAMPLE=<ms>`, loaded by `rg-mfcam.dll` because the preload slot is already taken) it is possible to see
where the game sits: the main thread in `win32u+0x11d38` (the message loop) and the logic threads in
`ntdll+0xd410` (a wait). So the game does not fall over and is not in some odd call; it just
does not get any further.

**The gatekeeper.** The function that asks for the ShareK status is at `0x481490`:

```
0x481492  xor  %ebx,%ebx
0x481494  cmp  %bl,0x394(%eax)     ; flag in the own ShareK object
0x48149a  je   0x481567            ; flag 0 -> the status query is SKIPPED
0x4814a0  cmp  %ebx,(%eax)         ; interface pointer zero?
0x4814a2  je   0x481567
0x4814b7  mov  0x8c(%ecx),%eax     ; slot 35
0x4814bd  call *%eax               ; the status query we never saw
```

With the memory engine (which `rg-mfcam.dll` now loads itself when `RAVENGUN_PATCHMEM_FILE` is set) the
first gatekeeper was opened (`MK_AGP3_FINAL.exe|0x8149a|0f84c7000000|909090909090`, verified in memory:
`90 90 90 90 90 90`). After that the **second** condition turns out to block: the
interface pointer in the game's object is zero, so the status query still does not happen. Note the
pitfall that cost: the engine looks up the module by **name**, and this game is called `MK_AGP3_FINAL.exe`
(not `game.exe`, like Robin Hood); with the wrong name it applies nothing.

**What did not help:** `[Launch] AmDaemon = true` (the engine's AM daemon, `amdaemon.cpp`) against the
black picture. Measured: the game runs, the picture stays black. That line is therefore not in the
profile.

**What is there now** (all disabled in the drop-in, with the name to enable them):
* `tools/rg-mfcam/rg_mfcam.cpp` logs every `QueryInterface` with its IID and has `RG_SHAREK_ANY_IID=1`
  to answer unknown IIDs anyway (measuring, not the default);
* the engine and the sampler are loaded by `rg-mfcam.dll` itself when `RAVENGUN_PATCHMEM_FILE`
  or `RG_COMPAT_SAMPLE` respectively is set (the preload slot belongs to `rg-mfcam.dll`);
* `patches/MarioKartDX/dropin/patchmem.txt` with the gatekeeper patch and the explanation.

**Next step:** find out why `obj+0` (the interface pointer) stays zero. The game presumably only sets it
when another step succeeds -- the same question as with the callback: who writes that
field, and which method of our fake client is supposed to do that? Find the writer with the same
byte-search method (`89 8? <disp32>` on `+0x0` near the ShareK init at `0x47e800`).

## ALL.Net: the IAuth class of AMAuthd now works (2026-10-19 05:20)

**The hurdle was registration, not the class.** At startup the game asks

```
CoCreateInstance(CLSID {4603BB03-058D-43D9-B96F-639BE908C1ED}, NULL, CLSCTX_LOCAL_SERVER,
                 IID {045A5150-D2B3-4590-A38B-C1158678E1AC})        <- 0x4010c0
```

and always got `CLASS_E_CLASSNOTREG` (0x80040154). The class is in `AMCUS/AMAuthd.exe` and only becomes
findable when **`AMCUS/iauthdll.dll` is registered** (that file is the proxy/stub file with the
IID; it has `DllRegisterServer`). With

```
regsvr32 iauthdll.dll        (in the game's prefix, from the AMCUS folder)
```

the keys are in the prefix (`HKLM\Software\Classes\CLSID\{4603BB03-...}` with `LocalServer32` pointing to
`AMAuthd.exe`) and after that the same call gives **`hr 0x00000000`** -- so the game gets a real
IAuth from AMAuthd, instead of nothing.

What else was needed, in this order (the cabinet does the same in `init_w10.ps1`):

1. `PreprocessAmAuth.exe -wait` -- writes `WritableConfig.ini` (without that file AMAuthd complains
   "ERROR: file not found: WritableConfig.ini" and does nothing);
2. start `AMCUS/AMAuthd.exe` (stays running);
3. `regsvr32 AMCUS/iauthdll.dll` -- register the class.

**Correction to this note (lead, round 9):** `views/MarioKartDX/AMCUS` is a **symbolic
link to the NAS**, so that "copy in the view" overwrote the original on the NAS
(ALL.NET -> OFFLINE). The lead restored the original from `/tmp/AMConfig.origineel.ini`
(md5 e22e16ead82786c4b3d359943a4135b9, `amdcfg-authType=ALL.NET`) and keeps a backup. This
file is not touched any more. Such folders belong in the view's write layer, not as a link
to the NAS; see the night status for Jeroen.

**Measured result:** the call at 0x4010c0 now succeeds (hr 0), the game keeps running, but the picture
is still black. So authentication is a *necessary* step that now works, but not the
last: the next question is what the game does with `auth_state` afterwards (it reads fields of the
IAuth output; the earlier conclusion that the game calls the method on slot 9 and then shuts down is
**retracted** (review 10): the trial used stdcall with a fixed arity that was wrong, so that
conclusion is not reliable). With the real AMAuthd it keeps running, so the next measurement is:
which of the 29 methods does the game call, and what does the real server answer to that?
(`AMCUS/AMConfig.ini` knows `amdcfg-authType` = `ALL.NET` | `OFFLINE` | `NBLINE`; on ALL.NET the
registration works, an OFFLINE copy has not been tried yet.)

### IAuth: OFFLINE tried, and what is still to be measured (2026-10-19 05:28)

`AMCUS/AMConfig.ini` knows `amdcfg-authType` = `ALL.NET` | `OFFLINE` | `NBLINE`. A copy in the view
with `amdcfg-authType=OFFLINE` was tried: the IAuth call still gives
`hr 0` (so the registration works in both modes) and the picture stays black. OFFLINE therefore makes
no difference for this hurdle.

**Logging method calls** requires a *pass-through object*: the game now talks to the real AMAuthd over
COM, and those calls cannot be seen from outside. Our own trial (empty object, every method S_OK)
seemed to show which method the game calls (slot 9, vtable+0x24) and that the game then shut down cleanly,
but that conclusion was **retracted** (review 10): the trial used stdcall with a fixed
arity that was wrong, so the shutdown may be due to the trial itself and proves nothing about the game.
With the real server it keeps running (black picture). A real
proxy must pass the calls on to the original object, and for that the *arities* of the 32
methods are needed; they are in the proxy/stub file `AMCUS/iauthdll.dll`. That is the next step:
read out `iauthdll.dll` (the stub functions each have their own stack size), make a proxy per method
that logs and passes on, and then see what the real server returns per call.

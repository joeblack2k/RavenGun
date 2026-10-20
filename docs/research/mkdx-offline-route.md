# Mario Kart Arcade GP DX without the TeknoParrot core (offline route)

Mario Kart Arcade GP DX 1.18.16 (`MK_AGP3_FINAL.exe`, profile `patches/MarioKartDX`) starts from RavenGun with a
double-click, on RavenGun's own OpenParrot loader and core, with no TeknoParrot files, no TeknoParrotUi and no start
script. Everything the cabinet normally provides is stood in for by two files in the profile:

- `dropin/rg-mfcam.dll` (source `tools/rg-mfcam/`, build `tools/dev/build-mfcam.sh`), preloaded into the game process;
- `dropin/patchmem.txt`, six memory patches applied by our own memory engine (`ravengun-patchmem32.dll`).

Each part has its own switch in `dropin/dropin.ini` (`Env`). With the TeknoParrot core in the process
(Properties > Online play = TeknoParrot, `[Loader] Core = teknoparrot`) every part of rg-mfcam and the memory engine
switch themselves off, so the online route stays exactly as it was. That is also why `AMCUS/AMAuthd.exe` is still
started as the second exe: the TeknoParrot route needs it. On the offline route it runs but the game never talks to it
(the IAuth object is answered in the process); in full screen (gamescope) its console window is not visible.

Addresses below are virtual addresses of `MK_AGP3_FINAL.exe` (image base 0x400000, no ASLR). Error ids are the
argument of the game's error raise function at 0x6095C0; the screen codes were measured on the TV.

| Error id | Screen | Cause on a bare PC |
|---|---|---|
| 0x10 | E05-01 | no reply from the content router, or the route table rewrite failed |
| 0x11 | E05-02 コンテンツルーター不整合 | own address not in 192.168.112.x |
| 0x13 | (not seen) | own address not in .11..20 |
| 0x14 | E05-05 ローカルネットワークエラー | the local network monitor lost the content router (~2 min after start) |
| 0x39 | E05-56 初回ネットワーク認証エラー | first network authentication never succeeded |

## The parts in rg-mfcam.dll

| Switch | What the game expects | What we answer | Logic |
|---|---|---|---|
| `RG_DONGLE=fake`, `RG_DONGLE_USB=0b9a:0c10`, `RG_DONGLE_SERIAL=...` | a Namco USB dongle, found by walking the Windows USB tree (0x8B17B0) and a serial number check (0x607D18) | one host controller, root hub and device with that id and a serial built from the checked format (not taken from any real dongle) | `usb_dongle.h` |
| `RG_AMAUTH_FAKE=1` | AMAuthd's IAuth COM object (ALL.Net), CoCreateInstance at 0x8B2723 | an in-process object that reports an authenticated cabinet without an online connection | `am_auth_fake.h` |
| `RG_CONTENT_ROUTER=192.168.112.254` | the store content router answers `IcmpSendEcho` (boot check 0x71C8A0, network monitor 0x71BB70) | an echo reply for exactly that address; all other addresses go to the real function | `content_router.h` |
| `RG_SHAREK_FAKE=1` | the ShareK client (content distribution between cabinets) | "distribution off": a stand-alone cabinet | `sharek_fake.h` |
| `RG_JVS_COMPORT=COM3` | the JVS I/O board on COM3 | the pipe `\\.\pipe\TeknoParrot_JVS` to the daemon's MKDX board | `jvs_com.h` |
| `RG_STR_COMPORT=COM1` | the STR board (steering, force feedback) on COM1 | a board that answers its status; force feedback is off (no motor commands are sent) | `str_board.h` |
| `RG_BNGRW=fake` | two Banapass card readers (`bngrw.dll`) | readers without a card | `bngrw_fake.h` |
| `RG_DRIVE_FIXUP=F` | the cabinet's F: drive | the view | `drive_fix.h` |
| `RG_WINDOWED=1` | full screen at the cabinet resolution | a 1360x768 window, scaled by gamescope | `window_mode.h` |

The camera (Media Foundation) and the d3dx10 pHResult repair are the older parts of the same DLL (see
`mkdx-controller-webcam.md` and the top of `rg_mfcam.cpp`).

### IAuth: every slot pops its own arguments

All IAuth methods are stdcall. The game calls slots 5, 9, 10 and 19 in the boot check, and later 14 (0x461A05, two
dwords) and 23 (0x803938, two dwords), and slots 12, 13, 15, 29 and 30 with only the interface pointer. Slot 23 was
first answered by a one-argument stub: that left the caller's stack four bytes off and the game quit with exit code 9
about four minutes into attract mode. Every slot now has its exact signature (`am_auth_fake.h` lists the call sites).
The same rule bit the USB dongle earlier: a hook with one argument too few crashed the boot check. That was also why
an old patch of the drive board check (0x207C50) looked necessary; it is gone.

## The memory patches (`dropin/patchmem.txt`)

Format `module|RVA|old bytes|new bytes`; a line whose old bytes do not match is skipped, so another build of the game
is left alone. Each line has its reasoning in the file; in short:

| VA | Change | Why |
|---|---|---|
| 0x48149A | `je` -> NOPs | the ShareK status gate: the game only asks our ShareK client when a flag it never sets in our view is set |
| 0x603FBC | counter compare -> "timed out" | the boot check's ALL.Net wait never ends offline; this takes the game's own timeout path at once |
| 0x603D04 | `ExitWindowsEx` call -> NOPs | the boot check reboots the cabinet; under Wine that would just end the game |
| 0x71CB20 | `mov al,1; ret` | the content router check wants the host's own address in 192.168.112.11..20 and a gateway; we leave the host network alone (E05-01/E05-02) |
| 0x71C180 | `mov al,1; ret` | the route table rewrite would delete and rewrite routes on the host; the game must never touch the host routing table |
| 0x4653C1 | `jne` -> `jmp` | the first network authentication: offline the game takes its own offline-grace carry-on path instead of E05-56 |

Deliberately not done: suppressing the error screens themselves (that would also hide real errors), fabricating
`last_auth.bin`, faking network interfaces, or copying any value from TeknoParrot's binaries (the dongle serial is
built from the format the game checks).

## Controllers: Wine keeps its hands off hidraw

The game reads no controller itself (its controls come over the JVS pipe from RavenGun's engine), but Wine's HID bus
(`winebus.sys` in `winedevice.exe`) opened every `/dev/hidraw*` on the host. For a Steam Controller that is fatal: the
kernel driver (`hid-steam`) takes an open hidraw as "the Steam client is here" and removes its gamepad device, so RavenGun
lost the pad the moment the game started (measured 2026-10-20). `data/winebus-no-hidraw.reg` (`[Launch] Registry`, the
game's own prefix) sets `DisableHidraw = 1` and `Enable SDL = 0` for winebus; `PROTON_DISABLE_HIDRAW` alone was not enough
(it only covers devices that call themselves a gamepad). Measured after the change: no hidraw device is held by Wine while
the game runs, and the "Wireless Steam Controller" stays RavenGun's player 1. With the Steam client running, Steam owns the
controller and RavenGun reads Steam's virtual pad ("Microsoft X-Box 360 pad 0", 28de:11ff) instead.

Open (Steam): a game started by RavenGun's engine is not a child of the Steam launch, so Steam does not count its window
as part of the RavenGun shortcut. On the KDE desktop Steam then applies its desktop layout while the game has focus, and
that layout sends Escape for the controller's Start button; the game (DXUT) quits on Escape (reproduced with a single
Escape key press, 2026-10-20). Proton's Wine tags its windows from `SteamGameId`/`SteamAppId` and gamescope maps windows
to apps through those tags or Steam's launcher (`AppId=`), so the route for Game Mode is to hand the RavenGun shortcut's
Steam identity to the game process.

## How to check it

1. `rg-direct --plan MarioKartDX` has `rg-mfcam.dll` in `dropin_lines`, `TP_PRELOAD_DLL` pointing at it, the `RG_*`
   switches of `dropin.ini` in `env` and `RG_LOADER_CORE=ravengun`.
2. After a start, `rg-mfcam.log` in the view (`~/.local/share/ravengun/runtime/views/MarioKartDX`) shows, after the
   last "loaded in" line: "dongle: on (fake)" and the walk, "content router: on, echo requests to 192.168.112.254 are
   answered" followed by "echo N answered", "memory engine: ravengun-patchmem32.dll loaded (list patchmem.txt)", the
   "AMAuth-object" slot calls, and no "unmeasured" slot.
3. The run log (`~/.local/state/ravengun/logs/direct-MarioKartDX-*.log`) names RavenGun's own
   `OpenParrotLoader.exe` and no TeknoParrot path.
4. On the TV: the boot check passes (force feedback shows "反力OFF"), then attract mode with FREE PLAY; Item starts a
   game; the wheel, gas and brake work in a race; the photo step shows the camera picture.

## Measured (2026-10-20)

All on the TV, started with `ravengunctl game start MarioKartDX` (the same path as a double-click in the app), with
the TeknoParrot folder hidden and the installed profile identical to the repository's.

- Five starts in a row (12:10-12:39): each one in attract mode (title or "Press item button", FREE PLAY) at 210 s, in
  full screen 1920x1080 through gamescope; the title shows the serial of our dongle (S/N 271010-020001). The fifth
  start was still in attract mode at 510 s (8.5 min), with 300 content router pings answered and no error raised.
  Every run log names RavenGun's own `OpenParrotLoader.exe` and mentions TeknoParrot nowhere.
- Played with a gamepad (11:55-11:59): Item (A) leaves attract mode and confirms the menus, the left stick moves the
  selection, the photo step shows the live PS Eye picture, and in a Grand Prix 50cc race RT drives, the stick steers,
  LT held keeps the kart standing still, and an item was used; the race ran into lap 2.
- Force feedback is OFF: the boot check shows "ステアリング 反力OFF" and the STR board got no motor command the whole
  session.

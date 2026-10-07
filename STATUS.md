# RavenGun — status

Actuele fase: 1 (Halo op de televisie) met parallel fase 2 (Move). G1 is nog niet gehaald: Halo via TeknoParrot staat op het gun-kalibratiescherm en wacht op Test/trigger (knoppen nog niet gebonden).

## Bewezen resultaten (op deze host vastgesteld, 2026-10-07)

Host en sessie
- SteamOS 3.9.2 (build 20260925.101), kernel 7.2.7-valve1, x86_64, geen container (systemd als PID 1).
- AMD Ryzen 7 7800X3D, 30 GiB RAM, AMD Radeon RX 6950 XT (Navi 21).
- Desktop Mode actief: KDE Plasma op Wayland (kwin_wayland), sessie op seat0 via sddm-autologin. Wayland-socket `wayland-0`, Xwayland op `:0`. De agent werkt via SSH; grafische opdrachten krijgen de sessievariabelen expliciet mee.
- Eén beeldscherm: HDMI-A-1, EDID "LG TV SSCR2", actief op 1920x1080 @ 60 Hz, SDR (HDR uit). DP-1 t/m DP-3 niet aangesloten.
- Geluid: standaard-sink is de HDMI-audio van de Navi 21 naar de LG TV.
- Camera (alleen gedetecteerd, nog niet onderzocht): OmniVision "USB Camera-B4.09.24.1", vermoedelijk PS3 Eye.

NAS
- Bestaande systemd-automount: `//192.168.2.3/emulation` op `/home/deck/mnt/spidernas_emulation` (SMB 3.0). De mount is rw; dit project gebruikt de NAS alleen lezend.
- Echte paden:
  - Game: `/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo` (9,9 GB)
  - UI: `/home/deck/mnt/spidernas_emulation/Teknoparrot/TeknoParrotUi-2.0.0.130-linux-x64` (204 MB)

Halo-build
- Native Linux x86-64 Raw Thrills-game, verwacht absoluut pad `/pm/g7/halo` (zie `utils/run_game.sh`).
- `game_original`: Sentinel-envelope (ingepakt). `game`: uitgepakte variant.
- `halo.so`: niet-TeknoParrot LD_PRELOAD-shim die de Sentinel HASP-dongle emuleert (hasp_login/hasp_read/hasp_encrypt, capstone/plthook-hooking). Herkomst onbekend. Zie docs/decisions.md.
- Benodigde libraries: alles aanwezig in /usr/lib64 behalve `libfmod.so.10` en `libfmodstudio.so.10` (FMOD 1.10, proprietary, niet in de dump).
- Cabinettypes uit run_game.sh / rtinit: 9 = dual-screen 3880x1080; 3, 4, 8 = 1920x1080.

TeknoParrot
- TeknoParrotUi 2.0.0.130 linux-x64 (Avalonia/.NET, SHA-256 6ab5ed33…cc2cc). Lokaal gestaged en per bestand met SHA-256 geverifieerd in `~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64`.
- Halo-profiel: EmulatorType `ElfLdr2`, `Patreon=true`, ExecutableName `game`, CabinetType standaard "STANDARD TETHERED", Input API RawInput, InvertedMouseAxis.
- De UI-build bevat geen ElfLdr2. De UI-strings tonen dat ElfLdr2 (BudgieLoader.exe) op Linux via Wine/Proton draait en dat abonnementsactivatie via BudgieLoader in de Wine-prefix gebeurt. Een native Linux-route voor ElfLdr2-games is in deze build niet gevonden.
- Startpoging 1: de UI start en is zichtbaar op de tv (screenshot). Ze wacht op de privacymelding (Accept/Quit) en toont status "Free" (geen abonnement).

Gekozen route (beslissing gebruiker, 2026-10-07): Halo native starten met de meegeleverde halo.so-shim, buiten TeknoParrot om. Zie docs/decisions.md.

Na de keuze voor de halo.so-route
- De game is lokaal gestaged in `~/.local/share/ravengun/runtime/halo-g7/{halo,dump}` (9,9 GB). Een rsync-vergelijking op grootte en tijd toont 0 verschillen met de NAS.
- `scripts/run-halo.sh`: een bubblewrap-sandbox met `/pm/g7/halo` als overlay (schrijfacties gaan naar `~/.local/share/ravengun/halo-overlay`), `/pm/g7/dump` read-only, geen netwerk (unshare-net), alleen het X11-socket en Xauthority van de sessie, en pulse/pipewire. De sessievariabelen worden uit de kwin_wayland-opdrachtregel gelezen.
- Startpoging 2 (zonder FMOD): faalt in de dynamic loader met `libfmod.so.10: cannot open shared object file`, exitcode 127.
- `tools/fmod-stub`: een diagnostische stub (geen FMOD-code; elke functie geeft een foutcode). Met deze stub zijn alle libraries en symbolen opgelost (loader-trace, exit 0, geen undefined symbols). De game zelf is er nog niet mee gestart.
- Startpoging 3 (met de stub op de tv): de auto-mode-beveiliging van Claude Code weigerde mij de game te starten, dus de gebruiker startte hem zelf (14:14:47).
- Resultaat (op echte hardware, screenshot van de tv): Halo zelf draait fullscreen op 1920x1080 via Xwayland (vensternaam "Halo") en rendert OpenGL-tekst. Het proces blijft stabiel draaien.
- Eigen log van de game (`halo-overlay/upper/pmuser/gamelog.txt`): "Welcome to Halo v03.11", Game Rev 57529, Library Rev 1412, Country Code 48. FileTest Failure op `game`, `utils/rtinit` en `utils/run_game.sh`. Alle data-bestanden zijn in orde.
- Op het scherm: "FileSystem integrity test failed! ... Press START or TEST to continue."
- Oplossing voor 2 van de 3 FileTest-fouten (voorbereid, nog niet getest): de launcher toont binnen de sandbox `rtinit_original` en `run_game.sh_original` op de oorspronkelijke paden. `game` blijft afwijken, omdat de halo.so-route het aangepaste programma nodig heeft. De controle wordt niet gepatcht.
- Invoer: de game leest het toetsenbord via X11 (XQueryKeymap/XLookupKeysym), maar via XTEST geïnjecteerde toetsen (1, Return, space, 5, 9, F1, F2, t, s) geven geen reactie. XTEST werkt aantoonbaar voor de muis. Dat de toetsen het venster bereiken is niet hard bewezen (xinput ontbreekt).
- De game bevat een FTDI/libusb-verbindingsbeheer (`cmgr_*`, `ftdi_usb_open_desc`, "Did not find exactly one FTDI chip"). Hypothese: START, TEST, munten en gun-invoer komen op een echte kast via een I/O-bord met een FTDI-USB-chip. halo.so emuleert alleen de HASP-dongle, niet dit I/O-bord.

TeknoParrot-route (gebruiker heeft een abonnement; uitgevoerd door de Sonnet-subagent op 2026-10-07, gecontroleerd door de orchestrator)
- Componenten via de UI-updater: TeknoParrot core 1.0.0.3761, TeknoParrotElfLdr2 core 1.0.0.1434 (`ElfLdr2/x64/BudgieLoader_x64.exe`), OpenParrot 1.0.0.787. GE-Proton11-7 via Linux Setup in `~/.local/share/TeknoParrotUI/proton/`, gedeelde prefix `~/.local/share/TeknoParrotUI/prefixes/shared/proton/pfx`. Geen root gebruikt.
- Abonnement geactiveerd ("Subscribed", "Activated on this device"). De sleutel is niet gelogd.
- Eigen gamekopie voor TeknoParrot: `~/.local/share/ravengun/runtime/halo-g7-tp`. Profiel: `<TP-UI>/UserProfiles/Halo.xml`, met STANDARD TETHERED, CustomResolution 1920x1080, Windowed uit, HideCursor aan, BypassFilesystemIntegrityTest aan (een TeknoParrot-profielinstelling, geen patch). ExitKey Esc.
- Resultaat (screenshots sa_23 t/m sa_35): Halo start via ElfLdr2 onder Proton op 1920x1080. Zonder bypass verschijnt hetzelfde FileTest-scherm. Met bypass komt het gun-kalibratiescherm: "Player 1 gun (left): Pull trigger when ready to calibrate. Press the TEST button to skip calibration".
- Invoer: de knoppen zijn niet gebonden, omdat de subagent X11-toetsen injecteerde (xdotool) en TeknoParrot evdev leest. Het fysieke toetsenbord (Cooler Master ARES, event2) is al leesbaar voor deck via de SteamOS-ACL, dus voor het toetsenbord is geen udev-wijziging nodig. `/dev/uinput` heeft een ACL `deck:rw`, dus een virtueel RavenGun-apparaat kan zonder root.
- Linux Setup meldt dat winetricks, cabextract en gtk3/webkit2gtk ontbreken (alleen nodig voor TeknoParrot Online / redistributables). Niet geïnstalleerd.

Move en camera (subagent 2, tussenrapport)
- Move CECH-ZCM1 (054c:03d5) via USB op hidraw9; PSMoveAPI 4.0.12-206-g256b629 is user-local gebouwd (zonder tracker; OpenCV 5 is incompatibel). Via USB komen geen sensor- of knoprapporten binnen.
- Camera PS3 Eye (1415:2000, ov534, /dev/video0): 640x480@60 gemeten op 60,02 fps (p95 16,68 ms, 0 gemist); 320x240@187 gemeten op 187,3 fps. Een aanvraag van 120 levert 100 fps.
- Pairing: `psmove pair` vraagt root (schrijft `/var/lib/bluetooth/<host>/<move>/info` en herstart bluetooth.service). Kanttekening: `/etc` is op deze SteamOS een schrijfbare overlay, anders dan de subagent dacht. ClassicBondedOnly staat standaard true en kan de ZCM1 weigeren. Dat is nog te testen.

Move via Bluetooth (op echte hardware getest, 2026-10-07 15:11)
- Pairing: `psmove pair` (controllerkant gelukt, host-adres in de Move geverifieerd via feature report 0x04). btmon toonde eerst "Connection Rejected due to Unacceptable BD_ADDR" (de Move staat niet op de kernel-toelatingslijst), en na `scripts/move-bt-bond.sh --apply` "Authentication Failure" op L2CAP PSM 17 (ClassicBondedOnly=true eist authenticatie).
- Hostwijzigingen (met toestemming van de gebruiker, met back-up/rollback):
  1. `scripts/move-bt-bond.sh --apply`: een [LinkKey] in `/var/lib/bluetooth/<host>/<move>/info`. Back-up: `info.ravengun-backup`. Rollback: `--rollback`.
  2. `scripts/bluez-classic-bonded.sh --apply`: `ClassicBondedOnly=false` in `/etc/bluetooth/input.conf`. Dit is een globale verlaging voor Bluetooth-HID. Back-up: `input.conf.ravengun-backup`. Rollback: `--rollback`. Na een SteamOS-update opnieuw controleren.
- Resultaat: de PS-knop verbindt binnen 2 s. Kernel: "BLUETOOTH HID … Motion Controller" (hidraw9). `psmove list`: Bluetooth, batterij 100%.
- `src/tools/movetest` (15 s): 76,9 rapporten/s (ongeveer 154 Hz IMU, twee subsamples per rapport), grootste gat 36 ms. Accelerometer, gyro en magnetometer reageren op alle assen; trigger 0–255; knoppen Driehoek, Cirkel, Kruis, Vierkant, PS, Move en T gezien. Select en Start nog niet getest.

ravengund-kern (Opus-subagent, door orchestrator geverifieerd: build schoon, 3 CTest-suites groen)
- Docs: `docs/architecture.md` en een nieuwe sectie in `docs/decisions.md`.
- Opbouw: één eigenaar van de hardware, met een HID-thread, een IPC-thread (Unix-socket 0600 in `$XDG_RUNTIME_DIR/ravengun/`, getypeerde JSON) en een signal-thread.
- Inputmodel v1. IMU: twee subsamples per rapport, Mahony-fusie (IMU-only), gyro-bias alleen in stilstand.
- Aim: voorlopige oriëntatiemodus (rand bij 20° horizontaal en 11,25° verticaal), met unclamped en clamped waarden, plus een 1€-filter.
- Stale en disconnect: na 50 ms ongeldig, na 250 ms knoppen vrijgegeven, na 1,5 s reconnect-loop.
- CLI: `ravengunctl status|recenter|watch|filter`.
- Hardwaremeting (85 s, Move stil): 76,6 rapporten/s; gaten p95 21 ms, max 36,8 ms; verwerkingstijd p95 2,6 µs. Yaw-drift +2,1° in 60 s (ruim boven het doel), pitch +0,19°. Stilstandsjitter p95 met 1€: x 0,07 %, y 0,12 % van het bereik. Het volgnummer sloeg 1010 keer over (betekenis nog onbevestigd).
- Conclusie: alleen de IMU is niet genoeg voor richten. Een optische referentie (camera) is nodig tegen yaw-drift.
- Bekend: PSMoveAPI logt het volledige BT-adres naar stderr; filter dat voordat je logs deelt.

## Laatste werkende commando

    TeknoParrot-UI (unit ravengun-tpui) > Library > HALO Fireteam Raven > Launch
    Resultaat: kalibratiescherm van de gun op de tv, wacht op Test/trigger.

## Actuele fout / afhankelijkheid

Knoppen in TeknoParrot zijn nog niet gebonden (TeknoParrot leest evdev; X11-injectie werkt niet). Oplossing in uitvoering: een virtueel uinput-toetsenbord (echte evdev-events) of een fysieke toetsdruk.

## Volgende uitvoerbare stap

1. De gebruiker bindt in TeknoParrot (Halo > Controller Setup) met het echte toetsenbord: Test, Service, Coin, Start P1, Trigger P1, Reload P1, Grenade P1.
2. Opnieuw starten, met Test door de kalibratie, en controleren of attract mode of het titelscherm verschijnt (G1).
3. Move: gedaan (Bluetooth werkt). Volgende stap: de daemon-kern (HID-inname en IMU-oriëntatie) en een uinput-lightgun-backend testen in TeknoParrot.

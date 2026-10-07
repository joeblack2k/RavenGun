# RavenGun — status

Actuele fase: 1 (Halo op de televisie). G1 is nog niet gehaald: Halo draait zichtbaar op de tv, maar blijft hangen op het opstartscherm van de integriteitscontrole, omdat START/TEST niet te geven zijn.

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

## Laatste werkende commando

    ~/projects/ravengun/scripts/run-halo.sh --debug
    (door de gebruiker gestart; FMOD-stub automatisch, geen geluid; Halo zichtbaar op de tv)

## Actuele fout / afhankelijkheid

Het integriteitsscherm wacht op START of TEST. Die komen waarschijnlijk alleen via het (ontbrekende) FTDI-I/O-bord binnen. Zonder emulatie van dat bord is er geen voortgang voorbij dit scherm en is er ook geen attract mode.

Openstaand: FMOD 1.10 voor geluid (fmod.com, account nodig). TeknoParrot-route (Patreon, ElfLdr2 + Proton) als alternatief; die emuleert het I/O-bord wel.

## Volgende uitvoerbare stap

1. De gebruiker drukt fysiek op een toetsenbord met het Halo-venster in focus (bijvoorbeeld 1, 5, Enter, F2, 9) om hypothese 1 (XTEST komt niet aan) definitief uit te sluiten.
2. Bij geen reactie: beslissing van de gebruiker over de route voor het I/O-bord (zie docs/decisions.md).

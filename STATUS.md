# RavenGun — status

Actuele fase: 1 (Halo op de televisie). G1 is nog niet gehaald.

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

## Laatste werkende commando

    systemd-run --user --unit=ravengun-tpui --collect --same-dir \
      -E XDG_RUNTIME_DIR=/run/user/1000 -E WAYLAND_DISPLAY=wayland-0 -E DISPLAY=:0 \
      -E XAUTHORITY=<actuele /run/user/1000/xauth_*> \
      -E DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
      /bin/sh -c 'exec ./TeknoParrotUi >>~/.local/state/ravengun/logs/tpui-<tijd>.log 2>&1'
    (uitgevoerd vanuit ~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64)

## Actuele afhankelijkheden

1. Privacymelding van TeknoParrotUI: toestemming voor het verzamelen van gebruiksgegevens. Dit is jouw keuze.
2. Halo is een Patreon-titel: een TeknoParrot-abonnementssleutel is nodig. Die voer je zelf in de UI in; niet in chat, code of logs.
3. ElfLdr2 en een Proton/Wine-runtime moeten via de officiële updater van de UI binnenkomen, in de lokale kopie.
4. FMOD 1.10 (`libfmod.so.10`, `libfmodstudio.so.10`) ontbreekt. Nog vast te stellen of het ElfLdr2-pakket dit levert.

## Volgende uitvoerbare stap

Na jouw beslissing over 1 en 2: in de UI ElfLdr2 en Proton laten installeren, Halo toevoegen met gamepad naar de gestagede gamemap, cabinet "STANDARD TETHERED" bij 1920x1080, en starten op de tv.

# Bouwen

Status per onderdeel: geimplementeerd en automatisch getest tenzij anders vermeld.

## ravengunctl en camprobe (op de host)

De host heeft g++ en cmake (geen ninja). Geen containers nodig:

    cmake -S . -B build
    cmake --build build -j
    ctest --test-dir build --output-on-failure
    ./build/ravengunctl doctor
    ./build/ravengunctl devices --json

Eisen: C++20, CMake >= 3.16, alleen de standaardbibliotheek en Linux-kernelheaders (`linux/videodev2.h`). Testframework: geen; `tests/test_main.cpp` gebruikt een eigen `CHECK`-macro (actief bij NDEBUG) via CTest. 88 controles slagen op deze host.

Exitcodes van `ravengunctl`: 0 alles in orde (of `devices` gelukt), 1 minstens een waarschuwing, 2 minstens een fout, 64 verkeerd gebruik, 70 interne fout. Het programma is alleen-lezen. Het roept alleen `bluetoothctl --version`, `bluetoothctl devices` en `bluetoothctl devices Paired` aan. Bluetooth-adressen worden standaard geredigeerd (`--full-addresses` toont ze).

`camprobe` (`src/tools/camprobe`) meet fps en frame-intervallen via V4L2-timestamps en leest nooit beeldinhoud:

    ./build/camprobe --format YUYV --width 640 --height 480 --fps 60 --seconds 10 [--json]

## ravengund (de daemon)

`cmake -S . -B build` zoekt PSMoveAPI onder `~/.local/share/ravengun/psmoveapi` (`-DPSMOVEAPI_ROOT=…` voor een andere plek). Gevonden: ravengund wordt ertegen gelinkt, met een rpath naar `…/psmoveapi/lib` (na `cmake --install`: `$ORIGIN/../lib`, zie "Installatie-indeling"). Niet gevonden: ravengund wordt zonder HID gebouwd (waarschuwing bij cmake). `-DRAVENGUN_BUILD_DAEMON=OFF` slaat de daemon over. Op de host is alleen g++ nodig; de PSMoveAPI-headers en -library komen uit de containerbouw hieronder.

    ./build/ravengund                 # voorgrond; Ctrl-C of SIGTERM stopt netjes
    ./build/ravengunctl status        # of --json
    ./build/ravengunctl recenter
    ./build/ravengunctl watch --rate 10 [--duration 30] [--json]
    ./build/ravengunctl filter --off | --on [--min-cutoff 1.0] [--beta 0.5]

Opties van ravengund: zie `ravengund --help` (`--no-filter`, `--h-edge-deg`, `--v-edge-deg`, `--controller <adres>`, `--no-auto-center`, `--no-hid`). Exitcodes van ravengund: 0 normaal gestopt, 64 verkeerd gebruik, 69 al actief of geen `XDG_RUNTIME_DIR`, 71 socket- of systeemfout. Nieuwe exitcodes van ravengunctl: 69 daemon niet bereikbaar, 75 daemon weigert tijdelijk (bijvoorbeeld recenter zonder Move), 76 onverwacht antwoord.

Tests: `ctest --test-dir build` draait vijf tests, onder andere (plus `ravengun_gamesettings_tests` en `ravengun_gamesettings_py`: de lezer van `patches/<Game>/game.ini` in C++ en Python, de migratie en `tp-add-game.py`; en `ravengun_launch_direct`: ook de bestandspatches): `ravengun_tests` (88 controles), `ravengun_core_tests` (279 controles: quaternions, assen, eenheden, fusie, bias, aim, 1€, knoppen, stale/disconnect, JSON en protocol) en `ravengund_ipc` (`tests/ipc_smoke.sh`: daemon zonder hardware, rechten 0700/0600, enkele instantie, CLI, SIGTERM). Alle slagen op deze host. `ravengun_output_tests` (188 controles) dekt de coördinaattransformaties (inversie, schaal, offset, clamp, letterboxing precies één keer), arm/disarm, het vrijgeven van knoppen bij disarm, schrijffouten en afsluiten (met een nep-apparaat), de testpatronen en de nieuwe IPC-commando's. Op echte hardware getest: zie `docs/hardware.md`, de meting met ravengund.

Fase 0/1/3 voegt `ravengun_phase013_tests` toe (rapportparsing, Move-klok, stempel-dt, opnameformaat, deterministisch afspelen, ellipsoïdefit, kwaliteitspoorten, opslag, verstoringsdetectie en de nieuwe IPC-commando's), en breidt `ravengund_ipc` en `ravengund_web` uit met opnemen, LED en magnetometerstatus. Het analyseprogramma `ravengun-analyze` (replay, timebase, mag, magfit) leest alleen opnamebestanden en opent geen hardware. Nieuwe CLI-commando's: `ravengunctl record`, `led` en `calibrate mag` (zie `docs/procedures.md`).

## De app `ravengun` (Qt 6, in een container)

SteamOS heeft Qt 6.11.1 als systeembibliotheek maar geen headers en geen CMake-bestanden. De app wordt daarom gebouwd in een rootless podman-container met dezelfde Qt (besluit en achtergrond: `docs/decisions.md`, "De Qt-app: Qt 6.11 uit een container"):

    scripts/build-app.sh            # image maken als die ontbreekt, configureren en bouwen
    scripts/build-app.sh --test     # plus de app-tests, in de container en daarna op de host
    scripts/build-app.sh --clean    # build-app/ eerst leegmaken
    build-app/ravengun              # starten (op het bureaublad)

- Image: `localhost/ravengun-qt-build` uit `packaging/containers/qt-app/Containerfile` (`--rebuild-image` bouwt hem opnieuw). Basis `archlinux:latest`, teruggezet naar het Arch Linux Archive van 2026-07-01: `qt6-base` 6.11.1, glibc 2.43, GCC 16.1.1, gelijk aan de host. De container heeft geen netwerk tijdens het bouwen en mount alleen de repo, op hetzelfde pad.
- Configuratie: `cmake -G Ninja -DRAVENGUN_BUILD_DAEMON=OFF -DRAVENGUN_BUILD_CAMPROBE=OFF` in `build-app/`, doelen `ravengun_app` (binary `ravengun`) en `ravengun_app_tests`. In de gewone host-build (`build/`) vindt CMake geen Qt en slaat de app over (melding "Qt 6 (met headers) niet gevonden"); alle andere doelen en ctest-suites blijven gelijk.
- Controle: het script draait `ldd build-app/ravengun` op de host en stopt bij een ontbrekende bibliotheek; `libQt6Core.so.6` en `libQt6Widgets.so.6` komen uit `/usr/lib` (de systeem-Qt). De app-tests draaien daarna nog eens op de host tegen de systeem-Qt (`QT_QPA_PLATFORM=offscreen`).
- Installeren: de app installeert zichzelf (zie "Installatie-indeling" verderop: wizard, stap 1, of `ravengun --install`); voor een boom uit de bouw `cmake --install` (de app-build installeert met `cmake --install build-app --component app`). De snelkoppeling komt uit het sjabloon `packaging/desktop/ravengun-qt.desktop.in`, ingevuld door de app (`src/app/core/Install.cpp`); de oude `packaging/desktop/ravengun.desktop` en `ravengun-qt.desktop` (vaste repopaden) zijn alleen nog voor de bestaande installatie op deze machine.

Schermafdrukken voor ontwikkelaars (nooit op de tv; tegen een eigen test-engine):

    XDG_RUNTIME_DIR=/tmp/rg/run HOME=/tmp/rg/home XDG_CONFIG_HOME=/tmp/rg/cfg \
      build/ravengund --no-hid --no-camera --web-port 18765 --controllers-fake tests/fake_controllers.json \
      --tp-dir /tmp/rg/tp --games-dry-run &
    XDG_RUNTIME_DIR=/tmp/rg/run HOME=/tmp/rg/home XDG_CONFIG_HOME=/tmp/rg/cfg XDG_DATA_HOME=/tmp/rg/home/.local/share \
      QT_QPA_PLATFORM=offscreen RAVENGUN_SYSTEMCTL=/bin/false \
      build-app/ravengun --screenshot-dir /tmp/rg/shots --screenshot-game tp:TC5

`--screenshot-dir` opent na elkaar het hoofdvenster (lijst en raster), Properties (alle panelen), Controllers, de Move-instellingen (alle tabbladen), de koppelwizard, Settings (alle panelen), Options en General (route) van vier games (`$RAVENGUN_SCREENSHOT_GAMES`, standaard TC5, Terminator, Transformers, Jurassic Park) plus de open routelijst, de acht stappen van de wizard en de fullscreen-modus op 1920x1080 (menu, vizier, gekozen tegel, alle games, gedimd, schermbeveiliging, Exit-vraag), slaat ze op met `QWidget::grab()` en stopt. Er wordt niets aangeklikt; `RAVENGUN_SYSTEMCTL=/bin/false` zorgt dat ook een vergissing geen echte unit raakt. Zet daarnaast `RAVENGUN_PKEXEC=/bin/false`, `RAVENGUN_NO_GAMEPAD=1` en `RAVENGUN_STEAM_ROOT=<een kopie of lege map>`, zodat de wizard en Add to Steam nooit de echte Steam-bestanden of root raken.

Tests van de app: `ravengun_app_tests` (kern, zonder vensters: opties, bibliotheek, routes, notities, fullscreen-logica, VDF en Steam, wizardopdrachten) en `ravengun_app_widget_tests` (vensters offscreen: Properties voor alle games met een echte `patches/<Game>/options.json`, fullscreen, wizard met een nep-pkexec, Add to Steam op een kopie). `ravengun_components_tests` (onderdelen, zie hieronder: catalogus, standen, sha256, uitpakken, staging, downloaden via `file://`, Steam- en Wine-recepten met nepbestanden; 23 controles). `scripts/build-app.sh --test` draait alle drie, in de container en op de host. De test op een kopie van een echte `shortcuts.vdf` draait alleen met `RAVENGUN_TEST_SHORTCUTS_VDF=<kopie>` (anders overgeslagen).

### Onderdelen uit de app (geen shellscripts)

De wizardstap "Components" (ook Tools > Components) haalt alles op wat in de gebruikersmap (`~/.local/share/ravengun`) hoort: Wine (Kron4ek `proton-11.0-2`), DXVK, BepInEx, mods, PhysX, de DLL's uit Steam, 32-bits bibliotheken, eigen onderdelen en de TeknoParrot-runtime van de gebruiker. De catalogus is gegevens: `packaging/components.json` (ingebakken in de binary, de bestanden in de datamap gaan voor), gelezen door `src/app/core/Components.*`; installeren doet `core/ComponentInstaller.*`; de pagina is `widgets/ComponentsPage.*`. Zonder venster (geen display nodig, offscreen): `ravengun --components` (lijst: id, naam, versie, grootte, stand) en `ravengun --install-component ID` (herhaalbaar; of `--install-components a,b`; `all` = alles wat ontbreekt of verouderd is; exitcode 0/1, voortgang hoogstens elke 5%), in `core/ComponentsCli.*` met dezelfde `ComponentInstaller`. Test: `ravengun_components_tests` draait het echte programma met een nepcatalogus (`RAVENGUN_DATA_DIR`), `XDG_DATA_HOME` in een tijdelijke map en `file://`-downloads.

- Soorten: `download` (https, sha256 vastgepind, hervatten met Range, `.part` -> controle -> hernoemen), `bundled` (komt mee in `<datamap>/components/<id>/`, wordt gekopieerd), `userfolder` (gezocht in de gamemappen van de gebruiker, gekopieerd zonder iets te overschrijven), `steam` (recepten `redist-directx`, `redist-vc2010`, `gst32`, `lib32-sniper`: lezen uit de Steam-installatie) en `wine` (`wine-prefix`, `wine-physx`: starten de Wine van RavenGun rechtstreeks).
- Mods staan niet in de catalogus maar komen uit `[Mod <naam>]` in de `game.ini` van de games die de app kent (zoekvolgorde van `gamesettings`, dus ook bij een release zonder `patches/` in de repo); `TreeSha256` bewaakt de inhoud, zoals in het oude `scripts/fetch-mods.py`.
- Staging: alles wordt eerst uitgepakt in `<data>/components/.staging/<id>` en pas na de controle met een `rename` op zijn plek gezet (een bestaande map gaat eerst opzij en wordt daarna verwijderd). Een installatie krijgt een stempel in `<data>/components/<id>.json`; een met de hand klaargezet onderdeel dat voldoet aan `detect` telt als geïnstalleerd, een andere versie in de stempel als verouderd.
- Uitpakken met **libarchive** (tar.gz/xz, zip, zst, cab), zonder `tar` of `unzip`; paden met `..` en symlinks naar buiten de map worden geweigerd. libarchive zit in de buildcontainer (`archive.h`, via pacman) en op SteamOS (`libarchive.so.13`); de hoofd-`CMakeLists.txt` bouwt de app niet als libarchive-headers ontbreken. Er hoeft niets aan `packaging/containers/qt-app/Containerfile` te veranderen.
- Handmatig proberen zonder de echte map te raken: `XDG_DATA_HOME=/tmp/x build-app/ravengun`. De opt-in tests `RAVENGUN_COMPONENTS_REAL=1` (alleen lezen: de stand op deze machine) en `RAVENGUN_COMPONENTS_NET=1` (echt downloaden naar een tijdelijke map) staan in `ravengun_components_tests`.

## PSMoveAPI (in een container)

SteamOS levert geen ontwikkel-headers voor hidapi, libusb, OpenCV, glm en BlueZ. Daarom wordt PSMoveAPI gebouwd in een rootless, niet-geprivilegeerde podman-container (geen distrobox: die start met `--privileged` en mount de hele host).

    podman build -t ravengun-build -f cmake/container/Containerfile cmake/container
    mkdir -p ~/.cache/ravengun/{src,build,pc} ~/.local/share/ravengun/psmoveapi
    cp cmake/container/glm.pc ~/.cache/ravengun/pc/
    git clone https://github.com/thp/psmoveapi ~/.cache/ravengun/src/psmoveapi
    git -C ~/.cache/ravengun/src/psmoveapi checkout 256b6299e6076fe7b113e7277ed67016903f067a
    cd ~/.cache/ravengun/src/psmoveapi
    RUN=~/projects/ravengun/cmake/container/run.sh
    $RUN sh -c 'PKG_CONFIG_PATH=$HOME/.cache/ravengun/pc cmake -S . -B ~/.cache/ravengun/build/psmoveapi -G Ninja \
        -DPSMOVEAPI_VENDORING=OFF -DPSMOVE_BUILD_TRACKER=OFF -DPSMOVE_BUILD_NAVCON_TEST=OFF -DPSMOVE_USE_SIXPAIR=OFF \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON "-DCMAKE_INSTALL_RPATH=\$ORIGIN/../lib" \
        -DCMAKE_INSTALL_PREFIX=$HOME/.local/share/ravengun/psmoveapi && ninja -C ~/.cache/ravengun/build/psmoveapi'

De container mount alleen `~/.cache/ravengun`, `~/projects/ravengun` en `~/.local/share/ravengun/psmoveapi` (zie `cmake/container/run.sh`).

Installeren gebeurt met de hand (`ninja install` NIET gebruiken: de installatiestap schrijft `99-psmove.rules` naar `/etc/udev/rules.d` en faalt daar terecht):

    P=~/.local/share/ravengun/psmoveapi; B=~/.cache/ravengun/build/psmoveapi; S=~/.cache/ravengun/src/psmoveapi
    mkdir -p $P/bin $P/lib/pkgconfig $P/include/psmoveapi
    cp $B/psmove $B/example $B/example_new_api $B/multiple $P/bin/
    cp $B/libpsmoveapi.so $P/lib/
    cp $S/include/*.h $B/psmove_config.h $P/include/psmoveapi/
    sed 's#lib64#lib#' $B/psmoveapi.pc > $P/lib/pkgconfig/psmoveapi.pc

Gekozen opties en reden:
- `PSMOVEAPI_VENDORING=OFF`: de submodules (hidapi, libusb, PS3EYEDriver) zijn niet gecloned; systeem-hidapi (hidraw) uit de container.
- `PSMOVE_BUILD_TRACKER=OFF`: de tracker vereist OpenCV 4 (`find_package(OpenCV 4)`). De container heeft OpenCV 5.0.0, dat de C-API (`opencv2/core/core_c.h`) niet meer heeft; een proefbouw met aangepaste versiecontrole faalde daarop. RavenGun gebruikt een eigen V4L2-pijplijn, dus de tracker is voorlopig niet nodig. Camerasupport via `PSMOVE_USE_PS3EYE_DRIVER` is ook uit (vereist vendoring, deels GPL).
- `PSMOVE_USE_SIXPAIR=OFF`: alleen voor Navigation Controllers en trekt libusb-compat (`libusb-0.1.so.4`) in, die op de host ontbreekt. `PSMOVE_BUILD_NAVCON_TEST=OFF`: geen SDL2 nodig.
- De rpath is `$ORIGIN/../lib`; de binaries draaien op de host. Controle: `ldd` toont geen ontbrekende libraries; hoogste benodigde symbool-versie is GLIBC_2.43 (host: 2.43). Bij een host-update naar een oudere glibc dan de container kan opnieuw bouwen nodig zijn: pin dan de image op een Arch-archiefdatum.

Bijwerking bij gebruik: PSMoveAPI schrijft een calibratiecache naar `~/.psmoveapi/<serie>.calibration` (niet XDG; te verplaatsen met `HOME=`).

Gebouwd: `libpsmoveapi.so`, `psmove` (CLI: list, pair, register, battery, calibrate, enz.), `example`, `example_new_api`, `multiple`. Niet gebouwd: tracker, fusion-tracker, test_tracker, camera-tools, navcon-test, sixpair.

Getest op echte hardware (USB): `psmove list` en `psmove help`, `multiple` (1 controller zichtbaar). Niet getest: pairing, Bluetooth-verbinding, sensordata.

## OpenParrot-lader en -kern (eigen build, optioneel)

    scripts/build-openparrot.sh            # alles: OpenParrotLoader(64).exe en OpenParrot(64).dll
    scripts/build-openparrot.sh --versions # commit, basis-image en pakketversies

Haalt de gepinde commit van de openbare OpenParrot-bron op (`~/.cache/ravengun/src/openparrot`), past `third_party/openparrot/patches/` toe (de Wine-fix in de lader en wat MinGW-w64 nodig heeft) en bouwt in een
rootless podman-container (`packaging/containers/openparrot/Containerfile`: MinGW-w64 en clang, geen netwerk tijdens het bouwen). Uitvoer in `~/.local/share/ravengun/openparrot/<commit>/` (niet in de repo), met
`BUILDINFO.txt` en de verwijzing `current`. Gebruik: `[Loader] Build = ravengun` in `game.ini` (`docs/per-game-patches.md`). Details en wat niet gebouwd is: `third_party/openparrot/README.md`.

## Uitvoer (virtuele lightgun)

De daemon maakt het uinput-apparaat pas na `arm`. Zonder Move en camera, voor de uitvoerproef:

    ./build/ravengund --no-hid --no-camera &     # raakt Move, Bluetooth en camera niet
    ./build/ravengunctl arm
    ./build/ravengunctl test-pattern center      # of corners, grid, sweep, point X Y, stop
    ./build/ravengunctl output-button trigger tap
    ./build/ravengunctl set-output --invert-y 1  # transformatie live aanpassen
    ./build/ravengunctl disarm

De udev-regel (`packaging/udev/70-ravengun.rules`, installeren met `sudo scripts/udev-ravengun.sh --apply`, terugdraaien met `--rollback`) is nodig voor leesbaarheid, de stabiele by-id-symlink en het negeren door libinput. Volgorde voor een spelsessie: eerst de daemon met `arm` (en het virtuele toetsenbord), dan TeknoParrot/Halo starten; de daemon moet blijven draaien zolang de game draait.


## Installatie-indeling (A9)

Engine en app hangen niet meer van `~/projects/ravengun` af. Standaardprefix: `~/.local/opt/ravengun` (geen root; de thuispartitie overleeft SteamOS-updates).

- `bin/`: `ravengun` (de Qt-app), `ravengund` (de engine), `ravengunctl`. Rpath `$ORIGIN/../lib`.
- `lib/`: `libpsmoveapi.so` (de enige niet-systeembibliotheek) en de LD_PRELOAD-hulpen `librg_comm_shim.so`, `lib32/librg_comm_shim.so` en `librg_rt_input.so`. Qt, hidapi, BlueZ, dbus en libudev komen van het systeem (Qt 6.11 of nieuwer).
- `share/ravengun/` (de "datamap"): `scripts/`, `packaging/` (JSON-gegevens, sjablonen voor units en snelkoppelingen, udev-regels, icons, `containers/unity-plugin`), `docs/user-guide.md`, `plugins/unity/`, `third_party/` (readmes en licenties), `LICENSE`, `THIRD_PARTY.md`, `VERSION` (versie en commit) en bij een releasebouw `REQUIRES` (de glibc-eis) en `licenses/`. Niet mee: `scripts/build-app.sh`, `tools/dev/make-release.sh`, de containers van de Qt-app en de release, en de per-game snelkoppelingen van deze machine.

Zoekvolgorde voor de datamap (engine: `src/ravengun/paths.*`, app: `AppPaths::dataRoot`, scripts: `scripts/rg-paths.sh`; beide C++-kanten hebben tests: `ravengun_paths_tests` en `AppCoreTest::dataRootOrder`):

1. `$RAVENGUN_DATA_DIR` (als die scripts/ en packaging/ bevat),
2. `<prefix>/share/ravengun`, met `<prefix>` = de map boven de map van het programma (`bin/..`),
3. de repo bij ontwikkeling: `$RAVENGUN_ROOT`, de map boven `build/` of `build-app/`, en als laatste `~/projects/ravengun`.

De engine meldt bij het starten welke datamap hij gebruikt ("datamap: ... (installatie ...)"); `ravengun --print-paths` toont datamap, programmamap en engine. De scripts leiden `ROOT` (de datamap) af uit hun eigen pad; `scripts/rg-paths.sh` geeft `RG_BIN` en `RG_LIB`: in de repo `<repo>/build`, bij een installatie `<prefix>/bin` en `<prefix>/lib` (`RAVENGUN_BIN_DIR` en `RAVENGUN_LIB_DIR` gaan voor). De Unity-plugin wordt bij een installatie naar `~/.local/share/ravengun/unity-plugin` gebouwd (`RAVENGUN_UNITY_PLUGIN_OUT` gaat voor). Snelkoppelingen per game (`make-game-shortcut.sh`) komen bij een installatie onder `~/.local/share/ravengun/shortcuts`, zodat een update ze niet meeneemt.

Zelf installeren (het productpad, zonder enig script): de app draait uit een uitgepakt releasearchief of een `cmake --install`-boom (een volledige boom: `bin/ravengund`, `bin/ravengunctl` en `share/ravengun/packaging` naast `bin/ravengun`). De wizard (stap 1, "Install RavenGun"; of zonder venster `ravengun --install [--no-start]`) doet, in `src/app/core/Install.cpp` (`Install::installTree`):

1. de prefix controleren (`prefixIsSafe`: absoluut, schoon, geen spaties of `% " ' \`, niet `/`, de thuismap of erboven, niet onder een systeemmap) en weigeren als root draait of als de map al bestaat, niet leeg is en geen RavenGun-installatie is;
2. `bin/`, `lib/` en `share/` kopiëren naar `<prefix>.new.<pid>` (rechten en symlinks behouden), daar de eisen controleren (glibc uit `share/ravengun/REQUIRES` tegen `gnu_get_libc_version()`; ontbrekende systeembibliotheken per programma met `ld-linux --list`, dus Qt 6.11 voor de app) en dan atomisch verwisselen met de bestaande installatie (`rename` naar `<prefix>.old.<pid>`, `rename` van de nieuwe, oude weg). Bij elke weigering blijft de bestaande installatie staan. `~/.config/ravengun`, `~/.local/share/ravengun` en `~/.local/state/ravengun` worden niet aangeraakt;
3. de units schrijven uit `packaging/systemd/*.in` (`@BINDIR@`, `@DATADIR@` ingevuld met de geïnstalleerde kopie; paden met spaties of `%` worden geweigerd): `~/.config/systemd/user/ravengund.service` en `ravengun-oled-saver.service` (alleen het bestand; aan/uit doet de app). Alles via een tijdelijk bestand en `rename`;
4. het menu-item `~/.local/share/applications/ravengun-qt.desktop` (en op het bureaublad, als `XDG_DESKTOP_DIR`/`~/Desktop` bestaat) uit `packaging/desktop/ravengun-qt.desktop.in`, plus koppelingen `ravengun` en `ravengunctl` in `~/.local/bin`;
5. `systemctl --user daemon-reload` en `enable --now ravengund.service`. Draaide de engine al, dan alleen `enable` en een herstart als er geen game draait (`/proc` en `ravengun-tpui.service`); anders een melding. Met `--no-start` alleen `enable`;
6. de wizard start `<prefix>/bin/ravengun --setup-wizard` en sluit zichzelf.

Wie uit een uitgepakt, nog niet geïnstalleerd archief start, krijgt de wizard ook als hij eerder was afgerond; draait de app uit de installatie zelf, dan is stap 1 alleen een bevestiging (en een knop "Uninstall RavenGun..."). `$RAVENGUN_PREFIX` kiest een andere prefix, `$RAVENGUN_SYSTEMCTL` vervangt `systemctl` (tests). De engine-stap van de wizard (stap 3) schrijft alleen de units opnieuw en activeert ze (`Install::writeEngineUnit`, `writeOledUnit`, `enableEngine`). Het downloaden van onderdelen (DXVK, Wine, enz.) hoort daar niet meer bij: dat staat in een eigen stap. De OLED-unit start `<prefix>/bin/ravengun --saver-loop` (en bij stoppen `--saver-stop-view`); aan/uit doet de app (Settings, Display).

Installeren vanuit de repo (voor ontwikkelaars; geen onderdeel van het product, de scripts verdwijnen nog): na `cmake --build build` en `scripts/build-app.sh` kan `scripts/install-user.sh` nog, of `cmake --install build --prefix ~/.local/opt/ravengun` (en `--component app` voor de app-build) gevolgd door `ravengun --install` uit die map.

Rechten (root): een klein eigen programma, `ravengun-setup-helper` (`src/syshelper/`), dat de wizard met `pkexec` start, en `ravengun-bt-helper`, dat de daemon met `sudo -n` start. Ontwerp:

- Waarom een apart programma en niet `ravengun --apply-permissions`: de app is een Qt-programma (plugins, platformthema, vele bibliotheken en omgevingsvariabelen); dat als root starten maakt het aanvalsoppervlak groot. `ravengun-setup-helper` is ongeveer 650 kB, zonder Qt, met alleen libc en een statisch gelinkte libstdc++, PIE, RELRO en `-z now`.
- Vaste argumenten: precies een van `--apply`, `--rollback`, `--check`, `--version`; alles anders is exit 2. Geen bestandsnamen, geen paden, geen tekst van de gebruiker. De enige invoer is wie pkexec startte: `PKEXEC_UID` (door pkexec zelf gezet; `SUDO_UID` als terugval voor handmatig gebruik), omgezet met `getpwuid_r` en gecontroleerd tegen `^[a-z_][a-z0-9_-]{0,31}$`, nooit root.
- Alles wat het installeert zit in het programma zelf ingebakken (`cmake/EmbedBlobs.cmake`): de drie udev-regels uit `packaging/udev` en het programma `ravengun-bt-helper`. Als root wordt dus niets uit een door de gebruiker beschrijfbare map gelezen, behalve het programmabestand zelf (dat pkexec al aan de gebruiker toont). Het is dezelfde afweging als vroeger met het script, maar zonder dat het script zijn bronnen elders zoekt.
- `--apply` schrijft de regels naar `/etc/udev/rules.d` (0644), herlaadt udev (`/usr/bin/udevadm`), zet `/opt/ravengun` en `/opt/ravengun/bin` (root:root 0755), de hulp `/opt/ravengun/bin/ravengun-bt-helper` (0755) en een kopie van zichzelf `/opt/ravengun/bin/ravengun-setup-helper` (0755; gelezen uit `/proc/self/exe`), en de sudoers-regel: eerst als `/etc/sudoers.d/.ravengun-bt-helper.check` (0440; sudo negeert namen met een punt), gecontroleerd met `visudo -cf`, dan `rename` naar `/etc/sudoers.d/ravengun-bt-helper` en nog `visudo -c`; mislukt dat laatste, dan gaat onze regel er weer uit. De regel staat alleen `<hulp> register *` en `<hulp> unregister *` toe voor die ene gebruiker, met `env_reset`. Alle bestanden gaan via een tijdelijk bestand in dezelfde map (`O_EXCL`, `O_NOFOLLOW`), `fsync` en `rename`.
- De kopie in `/opt` heeft een nut: `--rollback` (uninstall) en `--check` draaien daarna de root-kopie, die de gebruiker niet kan wijzigen. Alleen `--apply` (installeren of bijwerken) draait het programma bij de app, in een door de gebruiker beschrijfbare map; dat blijft zo (eerste installatie kan niet anders), en wordt bij elke prompt zichtbaar als het pad in het pkexec-venster. Wie dat wil dichten, installeert eenmalig met `sudo` vanaf een rootmap.
- Kindprocessen (`udevadm`, `visudo`, `sudo -n -l`, `systemctl`): absolute paden, `fork` + `execve`, altijd een lege omgeving, geen shell, geen PATH. Het programma sluit bij het starten alle geërfde bestandsbeschrijvingen en maakt de omgeving leeg (`PKEXEC_UID` wordt eerst gelezen).
- `--check` en `--version` werken zonder root; `--check` vergelijkt de installatie byte voor byte met wat dit programma zou installeren en meldt de eigenaar en rechten van hulp en mappen en of `sudo -n -l` de hulp toestaat. Exit 0 = alles in orde. `--version` toont het "content-id" van de ingebakken inhoud.

`ravengun-bt-helper` (`src/syshelper/bt_core.*`; vervangt `scripts/ravengun-bt-helper`, dezelfde interface en dezelfde BlueZ-bestanden): `register AA:BB:CC:DD:EE:FF move|move2|navigation` en `unregister AA:BB:CC:DD:EE:FF`. Precies deze argumenten (anders exit 2, ook zonder root, zodat het testbaar is), adres in hoofdletters en niet nul of broadcast, een type uit een vaste lijst; geen omgeving en geen PATH (alleen `/usr/bin/systemctl stop|start bluetooth.service`); `umask 077`; alle bestandsbewerkingen vanaf geopende mappen met `O_NOFOLLOW` (een symlink op de plek van een map of bestand betekent exit 4); precies een adapter onder `/var/lib/bluetooth`, niet het adres van de adapter zelf; alleen overschrijven of verwijderen wat een Sony-HID-registratie is (Vendor 1356, product 981, 3166, 1071) of al "Motion/Navigation Controller" heet; de eerste versie gaat naar `info.ravengun-backup`; de koppelsleutel uit `getrandom(2)`; Bluetooth wordt altijd weer gestart (ook na een fout); logging naar syslog en `/var/log/ravengun-bt-helper.log`. Exitcodes: 0 gelukt, 1 mislukt, 2 ongeldige argumenten, 3 niet als root, 4 geweigerd. De daemon (`src/ravengund/controllers.cpp`, `pairing::helper_argv`) roept `/usr/bin/sudo -n /opt/ravengun/bin/ravengun-bt-helper ...` aan, ongewijzigd; alleen de tekst "hoe installeer ik de hulp" verwijst nu naar de app (Tools > Setup Wizard > Permissions).

Verwijderen (wizard stap 1 "Uninstall RavenGun..." of `ravengun --uninstall [--remove-config]`; `Install::uninstall`): stopt en verwijdert de units (en de drop-in van de saver), het menu-item en pictogram (alleen bestanden waarvan `Exec=` naar de prefix wijst) en de koppelingen in `~/.local/bin`, draait `pkexec <root-kopie of hulp> --rollback` als er systeemonderdelen staan, verwijdert de prefix en bewaart standaard de instellingen (`--remove-config` verwijdert `~/.config/ravengun` en `~/.local/state/ravengun`). `~/.local/share/ravengun` (TeknoParrot-runtime, Wine, native overlays) wordt nooit automatisch verwijderd. `$RAVENGUN_PKEXEC` vervangt pkexec (tests).

## Release-artefact (A9)

`tools/dev/make-release.sh` bouwt `dist/ravengun-<versie>-x86_64-steamos.tar.gz` plus `ravengun-<versie>-x86_64-steamos.sha256` (SHA-256 van het archief en van `ravengun-<versie>-source.tar.gz`, de broncode van dezelfde commit, voor de GPL):

    tools/dev/make-release.sh                  # alles in containers; --no-test slaat ctest over, --rebuild-image bouwt de images opnieuw

- Alleen de gecommitte bronnen (`git archive HEAD`) plus PSMoveAPI op de gepinde commit (uit `~/.cache/ravengun/src/psmoveapi`, anders gekloond; eenmalig netwerk op de host). Met niet-gecommitte wijzigingen weigert het script tenzij `--allow-dirty` (die wijzigingen komen er dan toch niet in).
- Image `localhost/ravengun-release-build` (`packaging/containers/release/Containerfile`) bovenop `localhost/ravengun-qt-build` (Arch Linux Archive van 2026-07-01: Qt 6.11.1, glibc 2.43, GCC 16.1.1), met hidapi, libusb, glm, dbus, BlueZ- en systemd-headers. Het bouwen zelf draait zonder netwerk en mount alleen een tijdelijke werkmap (`/work`).
- In de container: PSMoveAPI (alleen `libpsmoveapi.so`, dezelfde opties als hierboven), daarna RavenGun volledig (engine, CLI, hulpen en de Qt-app), de ctest-suites (de installatietest en de JS-test slaan zichzelf over), `cmake --install` en `share/ravengun/REQUIRES` (de hoogste glibc-symboolversie in de programma's; `install-user.sh` weigert een oudere glibc met een duidelijke melding).
- Eigen runtime-onderdelen (op de host, vóór het samenstellen; `share/ravengun/components/<id>/`, de "bundled"-regels van `packaging/components.json`): `openparrot` (`scripts/build-openparrot.sh`: gepinde bron en gepinde container, bron met een fetch in `~/.cache/ravengun/src`), `jvsbridge` (`scripts/build-jvsbridge.sh`) en `rg-pe-hook` (`tools/dev/build-pe-hook.sh`): MinGW in de gepinde container, zonder netwerk; `hasp` (`scripts/build-hasp.sh --no-dropin`) en `memory` (`tools/dev/build-memory.sh`): Linux-delen met de compiler van de bouwmachine (`gcc -m32`), Windows-delen in de lokale image `localhost/ravengun-mingw` (NOG NIET hermetisch; zonder die image ontbreken de DLL's met een melding). `lindbergh-loader` alleen met `--with-lindbergh` en dan NIET voor verspreiding (bron CC BY-NC-SA, bevat Nvidia Cg en oude i386-bibliotheken die niet van ons zijn). `rio` en `loaders` staan niet meer in de catalogus: de RIO-bibliotheken zijn gepatchte kopieën van spelbestanden van Raw Thrills (niet van ons, niet te verspreiden) en de gepatchte OpenParrot-lader maakt de engine zelf (`patched/`). `--no-components` slaat dit over (snelle proef). TeknoParrot, DemulShooter en Wine blijven downloads of bestanden van de gebruiker. Dongle-gegevens gaan nooit mee.
- Patches: uit de patches-repo (`--patches MAP`, standaard `patches/` of `../ravengun-patches/patches`) alleen de levering (`tools/dev/ship-patches.py`: `<Game>/*.ini`, `native.json`, `unity.json`, `options.json`, `dongle.json`, `README.md`, `dropin/`, `data/`, `seed/`; dezelfde lijst als `install(DIRECTORY patches ...)`) naar `share/ravengun/patches`; geen dossier.
- Samenstellen: `tools/dev/release-assemble.sh` zet `RavenGun.desktop` (dubbelklik in Dolphin start `bin/ravengun`), `README.md`, `LICENSE` en de onderdelen en patches erbij, met `share/ravengun/licenses/PSMoveAPI-COPYING` en `COMPONENTS.txt` (SHA-256 per onderdeelbestand); er is GEEN installatiescript. Dan rechten genormaliseerd, `tar --sort=name --mtime=@<commit-tijd> --owner=0 --group=0 --numeric-owner` en `gzip -n -9`.
- Reproduceerbaar: vaste containerpaden, `SOURCE_DATE_EPOCH` = de commit-tijd van HEAD, vaste volgorde, eigenaar en tijdstempels, de commit in de binary vastgelegd met `-DRAVENGUN_GIT_COMMIT_OVERRIDE`. Twee keer bouwen van dezelfde commit met dezelfde images geeft dezelfde SHA-256 (zie `docs/todo.md`, A9, voor de controle). Verandert een image (andere pacman-momentopname), dan verandert het resultaat.

Inhoud van het archief (`ravengun-<versie>-x86_64-steamos/`, ongeveer 3 MB gecomprimeerd): `RavenGun.desktop`, `README.md`, `LICENSE`, `bin/` (met `ravengun-setup-helper`), `lib/` en `share/ravengun/` (met `components/` en `patches/`) zoals hierboven. De gebruiker pakt het archief uit, start `RavenGun.desktop` of `bin/ravengun` (dubbelklik) en kiest in de wizard "Install RavenGun"; geen terminal en geen script nodig. Wat de gebruiker doet staat in `README.md`.

### AppImage: onderzocht en niet gekozen

Gekozen voor de tarball (zonder installatiescript: de app installeert). Een AppImage past slecht bij dit programma:

- Systemd-gebruikersunits en `systemd-run`-scripts: de engine is een service die bij het inloggen start, en de engine start elke game zelf via `systemd-run` (geen startscript meer). Een AppImage koppelt zichzelf bij elk proces op een wisselend pad `/tmp/.mount_XXXX` (FUSE) en dat pad bestaat alleen zolang dat proces leeft. Een unit met `ExecStart=` op het AppImage-bestand zou bij elke start een FUSE-koppeling maken, en de scripts en JSON-bestanden die de engine later nodig heeft staan op een pad dat niet blijft.
- `pkexec` en root: de wizard voert `install-permissions.sh` uit als root. Root kan een gebruikers-FUSE-koppeling standaard niet lezen (geen `allow_other`), dus die scripts, de udev-regels en de Bluetooth-hulp moeten sowieso uitgepakt op een gewoon pad staan.
- FUSE op SteamOS: libfuse2 staat nu op de Deck (`/usr/lib/libfuse.so.2`), maar het is geen gegarandeerd onderdeel van SteamOS; zonder moet de gebruiker `--appimage-extract-and-run` kennen, en dan is het in feite een uitgepakte tarball.
- Qt: de app is gebouwd tegen Qt 6.11, dezelfde hoofd/minor-versie als de systeem-Qt van SteamOS (zie "De app `ravengun`"). Een AppImage zou Qt, de platformplugins en ICU zelf moeten meeleveren (grofweg 60 MB of meer, gebundeld met `linuxdeploy`, dus met netwerkdownloads en slecht reproduceerbaar). Een meegeleverde Qt laadt bovendien de KDE-platformthema-plugin van het systeem (Breeze, Plasma-integratie) niet en ziet er in Desktop Mode anders uit dan de rest. Met de systeem-Qt gebruikt de app het thema en de dialogen van de gebruiker en blijft de download klein. Binaire compatibiliteit: een bouw tegen Qt 6.11 draait op elke latere 6.x; een nieuwere glibc op de bouwmachine wordt door `REQUIRES` afgevangen.
- Werk: een AppImage zou alleen de app dekken (geen units, udev, hulp, engine als service), dus er is toch een installer naast nodig.

Een Flatpak valt om dezelfde redenen af, en doordat de sandbox uinput, udev, BlueZ en `systemctl --user` blokkeert. Later eventueel een AppImage alleen voor het venster, als de engine los geïnstalleerd is; niet nodig voor het doel "downloaden, starten, werkt".

## Tests van de installatie (A9)

`ctest` bevat `ravengun_syshelper_tests` (de programma's met rootrechten: argumentcontrole, validators, de BlueZ-bestanden in een tijdelijke map, de udev-regels, hulp en sudoers in een tijdelijke root, de echte programma's zonder root; 194 controles), in `ravengun_app_tests` de padlogica, de unit- en snelkoppelingsbestanden, een volledige zelfinstallatie, een update, weigeringen, de engine-service, de uninstall en `ravengun --install` met de echte app-binary (alles in een tijdelijke HOME met nep-`systemctl` en nep-`pkexec`), in `ravengun_app_widget_tests` de wizardstappen 1 tot 3, `ravengun_paths_tests` (zoekvolgorde van de datamap), `ravengun_compat_list` (docs/compatibility.md is actueel; zie `tools/dev/gen-compatibility.py`) en `ravengun_install` (`tests/install_test.sh`; overgeslagen met exit 77 zolang `build-app/ravengun` ontbreekt). De installatietest werkt in een tijdelijke HOME met alle XDG-mappen daarbinnen, een namaak-`systemctl` en -`pkexec` die alleen loggen (`RAVENGUN_SYSTEMCTL`, `RAVENGUN_PKEXEC`) en draait de engine met een eigen `XDG_RUNTIME_DIR`:

- installeren vanuit de repo; controle van de bestanden, units en snelkoppelingen (geen repopad erin), rpath en `ldd`;
- `ravengun --print-paths`, de engine (`--no-hid --no-camera`) en `ravengunctl status` vanuit de installatie, en de app offscreen met `--screenshot-dir` tegen die engine (minstens tien schermafdrukken);
- bijwerken, doorzetten naar een tweede prefix (de route van het releasearchief), weigeren van een vreemde map en van `/usr/local`;
- uninstall: units, snelkoppelingen en prefix weg; instellingen en `~/.local/share/ravengun` blijven; `--remove-config` en `--keep-root`; de aanroep `pkexec ... install-permissions.sh --rollback`.

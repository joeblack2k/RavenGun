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
- Installeren: zie "Installatie-indeling" verderop (`scripts/install-user.sh`, `cmake --install`; de app-build installeert met `cmake --install build-app --component app`). De snelkoppeling komt uit het sjabloon `packaging/desktop/ravengun-qt.desktop.in`; de oude `packaging/desktop/ravengun.desktop` en `ravengun-qt.desktop` (vaste repopaden) zijn alleen nog voor de bestaande installatie op deze machine.

Schermafdrukken voor ontwikkelaars (nooit op de tv; tegen een eigen test-engine):

    XDG_RUNTIME_DIR=/tmp/rg/run HOME=/tmp/rg/home XDG_CONFIG_HOME=/tmp/rg/cfg \
      build/ravengund --no-hid --no-camera --web-port 18765 --controllers-fake tests/fake_controllers.json \
      --tp-dir /tmp/rg/tp --games-dry-run &
    XDG_RUNTIME_DIR=/tmp/rg/run HOME=/tmp/rg/home XDG_CONFIG_HOME=/tmp/rg/cfg XDG_DATA_HOME=/tmp/rg/home/.local/share \
      QT_QPA_PLATFORM=offscreen RAVENGUN_SYSTEMCTL=/bin/false \
      build-app/ravengun --screenshot-dir /tmp/rg/shots --screenshot-game tp:TC5

`--screenshot-dir` opent na elkaar het hoofdvenster (lijst en raster), Properties (alle panelen), Controllers, de Move-instellingen (alle tabbladen), de koppelwizard, Settings (alle panelen), Options en General (route) van vier games (`$RAVENGUN_SCREENSHOT_GAMES`, standaard TC5, Terminator, Transformers, Jurassic Park) plus de open routelijst, de acht stappen van de wizard en de fullscreen-modus op 1920x1080 (menu, vizier, gekozen tegel, alle games, gedimd, schermbeveiliging, Exit-vraag), slaat ze op met `QWidget::grab()` en stopt. Er wordt niets aangeklikt; `RAVENGUN_SYSTEMCTL=/bin/false` zorgt dat ook een vergissing geen echte unit raakt. Zet daarnaast `RAVENGUN_PKEXEC=/bin/false`, `RAVENGUN_NO_GAMEPAD=1` en `RAVENGUN_STEAM_ROOT=<een kopie of lege map>`, zodat de wizard en Add to Steam nooit de echte Steam-bestanden of root raken.

Tests van de app: `ravengun_app_tests` (kern, zonder vensters: opties, bibliotheek, routes, notities, fullscreen-logica, VDF en Steam, wizardopdrachten) en `ravengun_app_widget_tests` (vensters offscreen: Properties voor alle games uit de echte game-options.json, fullscreen, wizard met een nep-pkexec, Add to Steam op een kopie). `scripts/build-app.sh --test` draait beide, in de container en op de host. De test op een kopie van een echte `shortcuts.vdf` draait alleen met `RAVENGUN_TEST_SHORTCUTS_VDF=<kopie>` (anders overgeslagen).

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
- `share/ravengun/` (de "datamap"): `scripts/`, `packaging/` (JSON-gegevens, sjablonen voor units en snelkoppelingen, udev-regels, icons, `containers/unity-plugin`), `docs/user-guide.md`, `plugins/unity/`, `third_party/` (readmes en licenties), `LICENSE`, `THIRD_PARTY.md`, `VERSION` (versie en commit) en bij een releasebouw `REQUIRES` (de glibc-eis) en `licenses/`. Niet mee: `scripts/build-app.sh`, `scripts/make-release.sh`, de containers van de Qt-app en de release, en de per-game snelkoppelingen van deze machine.

Zoekvolgorde voor de datamap (engine: `src/ravengun/paths.*`, app: `AppPaths::dataRoot`, scripts: `scripts/rg-paths.sh`; beide C++-kanten hebben tests: `ravengun_paths_tests` en `AppCoreTest::dataRootOrder`):

1. `$RAVENGUN_DATA_DIR` (als die scripts/ en packaging/ bevat),
2. `<prefix>/share/ravengun`, met `<prefix>` = de map boven de map van het programma (`bin/..`),
3. de repo bij ontwikkeling: `$RAVENGUN_ROOT`, de map boven `build/` of `build-app/`, en als laatste `~/projects/ravengun`.

De engine meldt bij het starten welke datamap hij gebruikt ("datamap: ... (installatie ...)"); `ravengun --print-paths` toont datamap, programmamap en engine. De scripts leiden `ROOT` (de datamap) af uit hun eigen pad; `scripts/rg-paths.sh` geeft `RG_BIN` en `RG_LIB`: in de repo `<repo>/build`, bij een installatie `<prefix>/bin` en `<prefix>/lib` (`RAVENGUN_BIN_DIR` en `RAVENGUN_LIB_DIR` gaan voor). De Unity-plugin wordt bij een installatie naar `~/.local/share/ravengun/unity-plugin` gebouwd (`RAVENGUN_UNITY_PLUGIN_OUT` gaat voor). Snelkoppelingen per game (`make-game-shortcut.sh`) komen bij een installatie onder `~/.local/share/ravengun/shortcuts`, zodat een update ze niet meeneemt.

Installeren vanuit de repo (na `cmake --build build` en `scripts/build-app.sh`):

    scripts/install-user.sh                      # naar ~/.local/opt/ravengun
    scripts/install-user.sh --prefix /andere/map --no-start --no-desktop

Het script doet `cmake --install` van `build-app/` (alleen de app, `--component app`) en `build/` naar een tijdelijke map naast de prefix, controleert de eisen (glibc uit `REQUIRES`, `ldd` op ontbrekende bibliotheken), verwisselt die met de bestaande installatie en vult daarna de sjablonen in: `~/.config/systemd/user/ravengund.service` (via `install-service.sh`; geactiveerd en gestart), `ravengun-oled-saver.service` (alleen het bestand; aan/uit doet de app) en `ravengun-qt.desktop` in het menu en op het bureaublad, plus koppelingen `ravengun` en `ravengunctl` in `~/.local/bin`. Het weigert root, een prefix die geen RavenGun-installatie is en onverstandige mappen (`/usr`, de thuismap zelf). Draait er al een engine, dan wordt die alleen herstart als er geen game draait (`safe-restart-daemon.sh`); `--no-start` slaat dat over. Vanuit een uitgepakt releasearchief (of een eerdere installatie) werkt hetzelfde script: het kopieert dan de boom. `$RAVENGUN_SYSTEMCTL` vervangt `systemctl` (tests).

Root-onderdelen (udev-regels 70 en 71, Bluetooth-hulp met sudoers-regel) blijven apart: `pkexec <prefix>/share/ravengun/scripts/install-permissions.sh --apply` (de wizard doet dit; `--check` zonder root, `--rollback` voor de uninstall). Die scripts lezen hun bronnen naast zichzelf, dus uit de geïnstalleerde kopie. Let op: pkexec voert een script uit een map uit die de gebruiker kan schrijven. Wizard en uninstall vragen daarom altijd om het beheerderswachtwoord, maar wie het script wijzigt kan er root-code in kwijt. Dat was vóór A9 hetzelfde (toen vanuit de repo); wil je het dichten, kopieer de scripts dan naar een rootmap (zoals `/opt/ravengun`) en roep ze daar aan.

Verwijderen: `scripts/uninstall-user.sh` (in de geïnstalleerde kopie; `--help` voor opties): stopt en verwijdert de units, snelkoppelingen en koppelingen, roept `pkexec install-permissions.sh --rollback` aan als er systeemonderdelen staan (`--keep-root` slaat dat over), verwijdert de prefix en bewaart standaard de instellingen (`--remove-config` verwijdert `~/.config/ravengun` en `~/.local/state/ravengun`). `~/.local/share/ravengun` (TeknoParrot-runtime, Wine, native overlays) wordt nooit automatisch verwijderd.

## Release-artefact (A9)

`scripts/make-release.sh` bouwt `dist/ravengun-<versie>-x86_64-steamos.tar.gz` plus `ravengun-<versie>-x86_64-steamos.sha256` (SHA-256 van het archief en van `ravengun-<versie>-source.tar.gz`, de broncode van dezelfde commit, voor de GPL):

    scripts/make-release.sh                  # alles in containers; --no-test slaat ctest over, --rebuild-image bouwt de images opnieuw

- Alleen de gecommitte bronnen (`git archive HEAD`) plus PSMoveAPI op de gepinde commit (uit `~/.cache/ravengun/src/psmoveapi`, anders gekloond; eenmalig netwerk op de host). Met niet-gecommitte wijzigingen weigert het script tenzij `--allow-dirty` (die wijzigingen komen er dan toch niet in).
- Image `localhost/ravengun-release-build` (`packaging/containers/release/Containerfile`) bovenop `localhost/ravengun-qt-build` (Arch Linux Archive van 2026-07-01: Qt 6.11.1, glibc 2.43, GCC 16.1.1), met hidapi, libusb, glm, dbus, BlueZ- en systemd-headers. Het bouwen zelf draait zonder netwerk en mount alleen een tijdelijke werkmap (`/work`).
- In de container: PSMoveAPI (alleen `libpsmoveapi.so`, dezelfde opties als hierboven), daarna RavenGun volledig (engine, CLI, hulpen en de Qt-app), de ctest-suites (de installatietest en de JS-test slaan zichzelf over), `cmake --install` en `share/ravengun/REQUIRES` (de hoogste glibc-symboolversie in de programma's; `install-user.sh` weigert een oudere glibc met een duidelijke melding).
- Daarna op de host: `install.sh` (roept `install-user.sh` aan), `README.md`, `LICENSE` en `share/ravengun/licenses/PSMoveAPI-COPYING` erbij, rechten genormaliseerd, `tar --sort=name --mtime=@<commit-tijd> --owner=0 --group=0 --numeric-owner` en `gzip -n -9`.
- Reproduceerbaar: vaste containerpaden, `SOURCE_DATE_EPOCH` = de commit-tijd van HEAD, vaste volgorde, eigenaar en tijdstempels, de commit in de binary vastgelegd met `-DRAVENGUN_GIT_COMMIT_OVERRIDE`. Twee keer bouwen van dezelfde commit met dezelfde images geeft dezelfde SHA-256 (zie `docs/todo.md`, A9, voor de controle). Verandert een image (andere pacman-momentopname), dan verandert het resultaat.

Inhoud van het archief (`ravengun-<versie>-x86_64-steamos/`, ongeveer 3 MB gecomprimeerd): `install.sh`, `README.md`, `LICENSE`, `bin/`, `lib/` en `share/ravengun/` zoals hierboven. Wat de gebruiker doet staat in `README.md`.

### AppImage: onderzocht en niet gekozen

Gekozen voor de tarball met `install.sh`. Een AppImage past slecht bij dit programma:

- Systemd-gebruikersunits en `systemd-run`-scripts: de engine is een service die bij het inloggen start, en de engine start elke game zelf via `systemd-run` (geen startscript meer). Een AppImage koppelt zichzelf bij elk proces op een wisselend pad `/tmp/.mount_XXXX` (FUSE) en dat pad bestaat alleen zolang dat proces leeft. Een unit met `ExecStart=` op het AppImage-bestand zou bij elke start een FUSE-koppeling maken, en de scripts en JSON-bestanden die de engine later nodig heeft staan op een pad dat niet blijft.
- `pkexec` en root: de wizard voert `install-permissions.sh` uit als root. Root kan een gebruikers-FUSE-koppeling standaard niet lezen (geen `allow_other`), dus die scripts, de udev-regels en de Bluetooth-hulp moeten sowieso uitgepakt op een gewoon pad staan.
- FUSE op SteamOS: libfuse2 staat nu op de Deck (`/usr/lib/libfuse.so.2`), maar het is geen gegarandeerd onderdeel van SteamOS; zonder moet de gebruiker `--appimage-extract-and-run` kennen, en dan is het in feite een uitgepakte tarball.
- Qt: de app is gebouwd tegen Qt 6.11, dezelfde hoofd/minor-versie als de systeem-Qt van SteamOS (zie "De app `ravengun`"). Een AppImage zou Qt, de platformplugins en ICU zelf moeten meeleveren (grofweg 60 MB of meer, gebundeld met `linuxdeploy`, dus met netwerkdownloads en slecht reproduceerbaar). Een meegeleverde Qt laadt bovendien de KDE-platformthema-plugin van het systeem (Breeze, Plasma-integratie) niet en ziet er in Desktop Mode anders uit dan de rest. Met de systeem-Qt gebruikt de app het thema en de dialogen van de gebruiker en blijft de download klein. Binaire compatibiliteit: een bouw tegen Qt 6.11 draait op elke latere 6.x; een nieuwere glibc op de bouwmachine wordt door `REQUIRES` afgevangen.
- Werk: een AppImage zou alleen de app dekken (geen units, udev, hulp, engine als service), dus er is toch een installer naast nodig.

Een Flatpak valt om dezelfde redenen af, en doordat de sandbox uinput, udev, BlueZ en `systemctl --user` blokkeert. Later eventueel een AppImage alleen voor het venster, als de engine los geïnstalleerd is; niet nodig voor het doel "downloaden, starten, werkt".

## Tests van de installatie (A9)

`ctest` bevat `ravengun_paths_tests` (zoekvolgorde van de datamap), `ravengun_compat_list` (docs/compatibility.md is actueel; zie `scripts/gen-compatibility.py`) en `ravengun_install` (`tests/install_test.sh`; overgeslagen met exit 77 zolang `build-app/ravengun` ontbreekt). De installatietest werkt in een tijdelijke HOME met alle XDG-mappen daarbinnen, een namaak-`systemctl` en -`pkexec` die alleen loggen (`RAVENGUN_SYSTEMCTL`, `RAVENGUN_PKEXEC`) en draait de engine met een eigen `XDG_RUNTIME_DIR`:

- installeren vanuit de repo; controle van de bestanden, units en snelkoppelingen (geen repopad erin), rpath en `ldd`;
- `ravengun --print-paths`, de engine (`--no-hid --no-camera`) en `ravengunctl status` vanuit de installatie, en de app offscreen met `--screenshot-dir` tegen die engine (minstens tien schermafdrukken);
- bijwerken, doorzetten naar een tweede prefix (de route van het releasearchief), weigeren van een vreemde map en van `/usr/local`;
- uninstall: units, snelkoppelingen en prefix weg; instellingen en `~/.local/share/ravengun` blijven; `--remove-config` en `--keep-root`; de aanroep `pkexec ... install-permissions.sh --rollback`.

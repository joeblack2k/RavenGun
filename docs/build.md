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

`cmake -S . -B build` zoekt PSMoveAPI onder `~/.local/share/ravengun/psmoveapi` (`-DPSMOVEAPI_ROOT=…` voor een andere plek). Gevonden: ravengund wordt ertegen gelinkt, met een rpath naar `…/psmoveapi/lib`. Niet gevonden: ravengund wordt zonder HID gebouwd (waarschuwing bij cmake). `-DRAVENGUN_BUILD_DAEMON=OFF` slaat de daemon over. Op de host is alleen g++ nodig; de PSMoveAPI-headers en -library komen uit de containerbouw hieronder.

    ./build/ravengund                 # voorgrond; Ctrl-C of SIGTERM stopt netjes
    ./build/ravengunctl status        # of --json
    ./build/ravengunctl recenter
    ./build/ravengunctl watch --rate 10 [--duration 30] [--json]
    ./build/ravengunctl filter --off | --on [--min-cutoff 1.0] [--beta 0.5]

Opties van ravengund: zie `ravengund --help` (`--no-filter`, `--h-edge-deg`, `--v-edge-deg`, `--controller <adres>`, `--no-auto-center`, `--no-hid`). Exitcodes van ravengund: 0 normaal gestopt, 64 verkeerd gebruik, 69 al actief of geen `XDG_RUNTIME_DIR`, 71 socket- of systeemfout. Nieuwe exitcodes van ravengunctl: 69 daemon niet bereikbaar, 75 daemon weigert tijdelijk (bijvoorbeeld recenter zonder Move), 76 onverwacht antwoord.

Tests: `ctest --test-dir build` draait vijf tests, onder andere: `ravengun_tests` (88 controles), `ravengun_core_tests` (279 controles: quaternions, assen, eenheden, fusie, bias, aim, 1€, knoppen, stale/disconnect, JSON en protocol) en `ravengund_ipc` (`tests/ipc_smoke.sh`: daemon zonder hardware, rechten 0700/0600, enkele instantie, CLI, SIGTERM). Alle slagen op deze host. `ravengun_output_tests` (188 controles) dekt de coördinaattransformaties (inversie, schaal, offset, clamp, letterboxing precies één keer), arm/disarm, het vrijgeven van knoppen bij disarm, schrijffouten en afsluiten (met een nep-apparaat), de testpatronen en de nieuwe IPC-commando's. Op echte hardware getest: zie `docs/hardware.md`, de meting met ravengund.

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

## Uitvoer (virtuele lightgun)

De daemon maakt het uinput-apparaat pas na `arm`. Zonder Move en camera, voor de uitvoerproef:

    ./build/ravengund --no-hid --no-camera &     # raakt Move, Bluetooth en camera niet
    ./build/ravengunctl arm
    ./build/ravengunctl test-pattern center      # of corners, grid, sweep, point X Y, stop
    ./build/ravengunctl output-button trigger tap
    ./build/ravengunctl set-output --invert-y 1  # transformatie live aanpassen
    ./build/ravengunctl disarm

De udev-regel (`packaging/udev/70-ravengun.rules`, installeren met `sudo scripts/udev-ravengun.sh --apply`, terugdraaien met `--rollback`) is nodig voor leesbaarheid, de stabiele by-id-symlink en het negeren door libinput. Volgorde voor een spelsessie: eerst de daemon met `arm` (en het virtuele toetsenbord), dan TeknoParrot/Halo starten; de daemon moet blijven draaien zolang de game draait.

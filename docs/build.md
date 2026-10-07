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

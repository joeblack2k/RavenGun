# Externe afhankelijkheden (pinning)

Alleen VQF is gevendord (zie hieronder en `third_party/vqf/README.md`). De rest staat buiten git: bronnen en builds onder `~/.cache/ravengun/src` en `~/.cache/ravengun/build`.

## PSMoveAPI

- Bron: https://github.com/thp/psmoveapi (clone in `~/.cache/ravengun/src/psmoveapi`)
- Gepind op commit `256b6299e6076fe7b113e7277ed67016903f067a` (5 aug 2026, "Fix hang at end of camera firmware upload (#525)"), `git describe`: `4.0.12-206-g256b629`.
- Reden: de laatste release-tag `4.0.12` is van december 2020 en 206 commits oud; er is geen recentere tag. De gepinde commit is de huidige master op het moment van bouwen.
- Licentie: vereenvoudigde BSD-licentie (bestand `COPYING` in de bron). Opties met afwijkende licentie (PS3EYEDriver, deels GPL) zijn niet ingeschakeld.
- Build-opties en wat wel/niet is gebouwd: zie `docs/build.md`.

## Build-omgeving

- Container-image `localhost/ravengun-build`, gebouwd uit `cmake/container/Containerfile` (basis: `docker.io/library/archlinux:latest`, rolling). Pakketten: base-devel, cmake, ninja, hidapi, libusb, libusb-compat, glm, dbus, bluez-libs, systemd-libs, opencv, v4l-utils.
- Versies op het moment van bouwen: gcc 16.2.1, cmake 4.4.4, glibc 2.44 (container) tegen glibc 2.43 (host), hidapi 0.15.0, BlueZ-libs 5.87, OpenCV 5.0.0.

## VQF (2026-10-08)

- Bron: https://github.com/dlaidig/vqf, gepind op commit `86ba56bdd3158b9b05f9f9fe5596866ba326438c` (tag `v2.1.2`), licentie MIT. Alleen `vqf.cpp` en `vqf.hpp` (C++-hoofdimplementatie), ongewijzigd, in `third_party/vqf/` met `LICENSE` en `README.md`.
- Reden: alternatieve oriëntatieschatter naast Mahony (fusiemodi `vqf` en `vqf_mag`), zie `docs/decisions.md`.

## Overige afhankelijkheden van ravengund (2026-10-07)

Verder is niets gevendord. De daemon gebruikt naast PSMoveAPI alleen de C++-standaardbibliotheek, pthreads en Linux-systeemaanroepen. JSON lezen en schrijven gebeurt met eigen code (`src/ravengun/json.*`, `src/ravengun/json_parse.*`). Het 1€-filter is een eigen implementatie op basis van het artikel van Casiez, Roussel en Vogel (CHI 2012). Het Mahony-filter is een eigen implementatie op basis van de gepubliceerde vergelijkingen.

## Webinterface (2026-10-07)

Geen externe HTTP-bibliotheek: de webserver van ravengund is eigen code (zie `docs/decisions.md`, "Webinterface in de daemon"). Er is geen frontend-bibliotheek en geen CDN; HTML, CSS en JS staan in `src/ravengund/web/` en worden bij het configureren in de binary gebakken.

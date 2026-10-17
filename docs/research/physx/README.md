# PhysX 2.8.1 voor Linux uit NVIDIA's eigen archief (2026-10-17)

**Kort:** ja, er is een officiele bron. NVIDIA serveert de Linux-pakketten van PhysX 2.8.1 nog steeds van `developer.download.nvidia.com`
(HTTP 200, `Last-Modified: 2026-06-05`), al staan ze niet meer op de publieke downloadpagina's. De vier bibliotheken die
JurassicPark, AliensArmageddon en Terminator laden zijn daarin **byte-voor-byte gelijk** (sha256) aan die in `ElfLdr2/libs` van
TeknoParrot. Het onderdeel `physx-linux` in `packaging/components.json` haalt ze nu van NVIDIA zelf; geen enkel bestand uit de
TeknoParrot-map is meer nodig voor PhysX.

## 1. De bron

| | |
|---|---|
| URL (deb-tarball) | <https://developer.download.nvidia.com/PhysX/2.8.1/PhysX_2.8.1_SDK_CoreLinux_deb.tar.gz> |
| sha256 | `2a4822f6f5ba9c00993e529392c6fd957bb0c5e07518e9a08bae9ee502428913` |
| grootte | 23.211.902 bytes |
| tweede vorm (rpm, niet gebruikt) | `.../PhysX_2.8.1_SDK_CoreLinux_rpm.tar.gz`, sha256 `6c7f2a8595acf53aa132f62975f73d78eedb0334c430f9121cb840a3ddcef01a`, 23.396.780 bytes |
| inhoud (deb) | `libphysx-2.8.1_4` (core, i386), `libphysx-common` (loader + `/etc/PhysX.conf`), `libphysx-extras` (NxCharacter), `-dev`, `-doc`, `-samples` |
| gevonden via | Wayback-CDX voor `developer.download.nvidia.com/PhysX/*`: alleen **2.8.1** heeft Linux-pakketten (deb en rpm); 2.6.4 t/m 2.8.0 en 2.8.1 hebben verder alleen Windows-installers |

Gemeten op 2026-10-17: gedownload, uitgepakt (`ar` + `tar`, in een eigen map; niets uitgevoerd), en daarna met de app zelf
geinstalleerd (`ravengun --install-component physx-linux`, tijdelijke datamap, 23 MB, sha256 klopt, uitpakken 100%).

| bestand | sha256 | zit in | gelijk aan TeknoParrot (`ElfLdr2/libs`) |
|---|---|---|---|
| `libPhysXLoader.so.1` (SONAME `libPhysXLoader.so.1`) | `18eaf2d3efa5f0830cb7efa4beb72bec194c6ec7b97a8a24bd9accc21f80dfd0` | `libphysx-common_2.8.1-4_i386.deb`, `usr/lib/` | ja |
| `libPhysXCore.so` (SONAME `libPhysXCore.so.1`; in de deb heet het `.so.1`) | `e7abebbd2c8203e7172e0e3a905abb94b034dd7cbb6a12e4666a8b9409815d42` | `libphysx-2.8.1_4_i386.deb`, `usr/lib/PhysX/v2.8.1/` | ja |
| `libNxCooking.so` (SONAME `libNxCooking.so.1`; deb: `.so.1`) | `0e09448b4b08e98905f2de136f0fbaf08c647080c5bde812e64c60dc6e29ffff` | idem | ja |
| `libNxCharacter.so.1` | `81a52480329e90fc64cd60c2f9dd50bde2c0f97ea96940fe11bda75e9685ae59` | `libphysx-extras-2.8.1_4_i386.deb`, `usr/lib/PhysX/v2.8.1/` | ja |

De games zelf bevestigen de versie: `game` van Jurassic Park en Terminator hebben `DT_RPATH = /usr/lib/PhysX/v2.8.1/` en
`DT_NEEDED libPhysXLoader.so.1` (readelf); de loader leest `/etc/PhysX.conf` of `~/.PhysX.conf` (`PhysXCorePath`) en laadt dan
`libPhysXCore.so`. De namen `libPhysXCore.so` en `libNxCooking.so` (zonder `.1`) zijn dus precies wat de games/loader verwachten;
de uitpakregel in `components.json` hernoemt ze (de `map`-lijst). Omdat de bytes gelijk zijn, gedraagt het spel zich met
deze bestanden hetzelfde als met die van TeknoParrot; alleen de herkomst van de bestanden verandert.

## 2. Licentie, eerlijk

De licentietekst die in het pakket zit staat in `AGEIA-PhysX-licentie.txt` (zelfde map): *AGEIA PhysX Driver License
Agreement*. Wat erin staat, letterlijk samengevat:

- toegestaan: "use, display and reproduce" voor "evaluation, testing and non-commercial production purposes", alleen door de
  persoon die de licentie aanvaardt (niet overdraagbaar);
- niet toegestaan: doorgeven/verspreiden ("distribute, rent, loan, lease, transfer") zonder voorafgaande schriftelijke toestemming
  van AGEIA, en geen commerciele verspreiding (1.c); decompileren, aanpassen.

Gevolg voor RavenGun, en daarom is de keuze zo gemaakt:

- RavenGun **verspreidt de bibliotheken niet**: ze staan niet in git, niet in het releasearchief en niet op de wiki. De app downloadt
  ze op de computer van de gebruiker rechtstreeks van NVIDIA (sha256 vastgepind), zoals ze ook Wine, DXVK en de Ubuntu-debs
  downloadt. De licentietekst wordt er naast bewaard (`runtime/licenses/physx-2.8.1-copyright.txt`, via de `map`-regel).
- Het gebruik valt onder "non-commercial": een thuis-kast. Dat is de situatie van de eigenaar; een commercieel gebruik valt er niet onder.

**Niet bewezen / voor de eigenaar om te beslissen:**

1. De licentie zegt dat je instemt door te installeren of te gebruiken. De app toont de tekst nu **niet** voor het downloaden. Wil de
   eigenaar dat (een bevestigingsvenster met `AGEIA-PhysX-licentie.txt` in de Components-stap), dan is dat een kleine app-wijziging die ik
   niet gedaan heb omdat het zijn spelervaring/eigen keuze raakt.
2. De licentie is geschreven voor het *PhysX Driver*-pakket; of exact dezelfde tekst voor de SDK-runtime geldt, staat nergens. NVIDIA biedt
   de bestanden wel zelf aan op zijn downloadserver, met deze tekst in het pakket.
3. De URL staat niet meer op NVIDIA's publieke pagina's. Hij kan verdwijnen. Terugval: de pinned sha256 maakt het pakket
   herkenbaar; een gebruiker kan het tarball-bestand zelf in de downloadmap van de app leggen (`cachedDownloadIsReused` in de tests).
   Het oude "userfolder"-onderdeel (eigen map `physx-2.8.1`) is **vervangen**; wie geen internet heeft heeft nu het bestand nodig,
   niet de TeknoParrot-map.
4. **2.8.3 / 2.8.4 voor Linux** bestaan op NVIDIA's server niet (Wayback-CDX: alleen 2.8.1). De Linux-SDK 2.8.3.3 was een gesloten beta
   (Phoronix), 2.8.4 is alleen Windows; de NVIDIA-forumthread "2.8.4 SDK for linux download" heeft geen antwoord. Voor deze games is 2.8.1
   in elk geval de juiste versie (RPATH en sha256).

Andere bronnen die ik gecontroleerd heb en die **niet** bruikbaar zijn: een Arch-PKGBUILD uit 2008 (`physx` 2.8.0, haalt dezelfde soort
RPM's van Ageia's ontwikkelaarssite, die niet meer bestaat), distributiearchieven (Ubuntu/Debian/Arch/AUR/Gentoo: geen pakket met
`libPhysXCore`), NVIDIA's GitHub (PhysX 3.4 en nieuwer, andere ABI), `physx-system-software` 9.x (alleen Windows).

## 3. Wat er veranderd is

- `packaging/components.json`: onderdeel `physx-linux` is nu `kind: download`, een `tar.gz+deb`-archief met de pins hierboven, vier
  `map`-regels naar `runtime/lib32/` (dezelfde plek als `libstdcxx5`, `libcsv`, `openssl10-i386`) en de licentietekst naar
  `runtime/licenses/`. `detect` eist de vier bibliotheken.
- `src/app/core/ArchiveExtract.*`, `ComponentInstaller.cpp`, `Components.cpp/.h`: nieuw archieftype `tar.gz+deb` (een tar.gz dat
  `.deb`-pakketten bevat; de debs worden in een tijdelijke map uitgepakt en met dezelfde `map`/`include` verwerkt). NVIDIA levert de debs
  alleen binnen een tarball, daarom was dit nodig.
- `src/ravengun/launch/native.cpp`: `component_hint` noemt `physx-linux` bij een ontbrekende PhysX-bibliotheek in `@lib32`.
- Tests: `tests/app/test_components.cpp` (pins, uitpakken van een nagebootste tarball met debs, installeren, status `Installed`) en
  `tests/test_native_launch.cpp` (de kandidaat-profielen hieronder lezen en bevatten geen `ElfLdr2` meer; ook de melding bij een
  ontbrekende PhysX-bibliotheek). Fixture: `tests/app/data/components/test-debs.tar.gz`.
- `docs/research/physx/patches/{JurassicPark,AliensArmageddon,Terminator}/native.json`: de kandidaat-versies voor de patches-repo
  (alleen de bibliotheekregels zijn anders: `@tpui/ElfLdr2/libs/...` wordt `@lib32/...`; Aliens ook `libcsv.so` -> `@lib32/libcsv.so`,
  dat het onderdeel `libcsv` al levert; JurassicPark verliest `libfmod.so.7` en `libfmodstudio.so.7`, zie onder).

De `patches/`-map is een aparte repo (`~/projects/ravengun-patches`) en in deze worktree stuk gekoppeld en alleen-lezen; de coordinator kopieert de
drie bestanden naar `patches/<Game>/native.json` (en pas daarna, met de component geinstalleerd, ziet `rg-direct --tp-refs` 0 voor deze games).

### libfmod bij Jurassic Park (statisch nagegaan, niet gedraaid)

`JurassicPark/native.json` laadt `libfmod.so.7` en `libfmodstudio.so.7` uit de TeknoParrot-map. `readelf -d game`: **geen** `NEEDED` op
fmod; `strings game | grep -ci fmod` = 1 (de libm-functie `fmod`); de drop-in bibliotheken (`ravengun-hasp.so`, `libravengun-hasphook.so`)
hebben alleen `libc` als `NEEDED`. Het spel kan er dus alleen via `dlopen` bij komen, en die naam staat niet in het beeld. `docs/loskoppelen-per-game.md`
(rij 11) zegt hetzelfde ("de game verwijst er nergens naar"). In de kandidaat zijn de twee regels weggelaten. **Niet bewezen** door een run; als
JP bij de tv-proef toch iets met FMOD meldt, zet dan de twee regels terug (en dan is FMOD de volgende klus: de native FMOD-wrapper van Halo
is een mogelijke vervanger).

## 4. Tests die gedraaid zijn (geen spel gestart)

- `scripts/build-app.sh`-container, `ravengun_components_tests`: 28 geslaagd, 0 mislukt (2 overgeslagen: netwerk en echte machine).
- `build/ravengun_native_launch_tests`: 0 fouten.
- `ravengun --install-component physx-linux` met `XDG_DATA_HOME` op een lege map: downloadt van NVIDIA, sha256 klopt, vier bestanden in
  `runtime/lib32/`, licentie in `runtime/licenses/`; de vier sha256's zijn gelijk aan die van `ElfLdr2/libs` (tabel hierboven).
- `rg-direct --tp-refs --patches <overlay met de drie kandidaten>`: JurassicPark en AliensArmageddon 0 verwijzingen. Terminator staat in
  `game.ini` op `Mode = direct` (nog niet native), dus die regel meet de directe route (lader `BudgieLoader.exe`, zie `../budgie/`).
- Volledige `ctest` in deze worktree: faalt op dingen die hier niet horen te bestaan en niets met deze wijziging te maken hebben
  (de `patches/`-koppeling is stuk -> gamesettings/compat; geen `gnu/stubs-32.h` -> elfloader-tests; `iobridge_tests` segfault); ze falen ook zonder
  deze commit (zonder de wijziging niet apart gedraaid; de oorzaken zijn omgevingsfouten).

## 5. tv-testrecept per game (dit moet de hoofdsessie doen; ik start niets)

Voorwaarde voor alles: de app installeert het onderdeel (Components -> "NVIDIA PhysX 2.8.1 for Linux", of
`ravengun --install-component physx-linux`) en de coordinator heeft de drie `native.json` uit `docs/research/physx/patches/` in
`patches/<Game>/` gezet. Controle zonder tv:
`ls -l ~/.local/share/ravengun/runtime/lib32/libPhysX*.so* ~/.local/share/ravengun/runtime/lib32/libNx*.so*` (4 bestanden) en
`sha256sum` tegen de tabel in sectie 1.

1. **JurassicPark** (werkt nu, dit is de regressietoets): dubbelklik in de app zoals gewoonlijk. Tijdens attract:
   `grep -E 'PhysX|NxC|NxCook|fmod' /proc/$(pgrep -nx game)/maps` -> de vier PhysX-bestanden onder `/rg/lib/` (bind-mounts van
   `runtime/lib32`), **geen** `libfmod`. Verwacht: precies hetzelfde gedrag als voor de wijziging (attract, munt, schieten, 5 min).
   Faalt het met een FMOD- of dlopen-fout: de twee fmod-regels terugzetten (zie boven).
2. **AliensArmageddon** (`enabled` in zijn native.json is true, `status` "niet-getest"; de spelmap op de NAS stond tijdens mijn werk leeg):
   dubbelklik; verwacht dezelfde uitkomst als zonder wijziging (de bekende open punten staan in zijn `note`: dongle statisch, richten via
   IR-camera's). Alleen de herkomst van libPhysX* en libcsv verandert; `libcsv` moet ook geinstalleerd zijn (`--install-component libcsv`).
3. **Terminator**: blijft via de directe route (`working`); zijn native.json is `enabled: false`. Geen tv-proef voor PhysX nodig tot Terminator native wordt
   (zie `../budgie/README.md`).

# Transformers: Shadows Rising

Instellingen: `game.ini`. Bevindingen en meetgeschiedenis: `findings/TransformersShadowsRising.md` op
de NAS naast de games, niet in deze repo.

## Wat deze game is

| | |
|---|---|
| Naam | Transformers: Shadows Rising |
| Fabrikant | Sega (platform Sega RingEdge 2; `findings/TransformersShadowsRising.md`, kop) |
| Jaar | 2018 |
| Platform | Sega RingEdge 2, Game Shell |
| Genre | Shooter (lightgun) |
| Lader | `Type = TeknoParrot`, `EmulationProfile = TransformersShadowsRising` → **eigen** `OpenParrotLoader.exe` + de TeknoParrot-kern |
| Profiel | `GameProfiles/TransformersShadowsRising.xml`, revisie 10 |
| Programma's | `Sega/shell/Shell.exe` (Game Shell versie 180605) start `Transformers2.exe` |

Naam, jaar en genre komen uit `Metadata/TransformersShadowsRising.json` (het **platformveld daarin is
leeg**); de fabrikant, het platform RingEdge 2 en de shell komen uit de kop van
`findings/TransformersShadowsRising.md`. De laderfamilie komt uit `docs/loskoppelen-per-game.md`
(rij 35) van de loader-repo.

`game.ini` volgt de shell: `Executable = shell.exe;Transformers2.exe` (de `ExecutableName` van het
profiel bevat beide namen) en `[Loader] PostStartMs = 3000`. De **TeknoParrot-kern zit alleen in
`Shell.exe`**, niet in `Transformers2.exe` — daarom is de shell de game en niet het tweede bestand:
`Transformers2.exe` direct starten blijft op "SYSTEM INITIALIZING... LOADING.." staan (meer dan 50 s).

De invoer loopt via `[Input] Route = hid-board` met `Board = SaeGunBoard`: `Transformers2.exe` leest
zijn gun via DirectInput 8, en de daemon bootst dat bord na als virtuele joystick
(`packaging/hid-boards.json` in de loader-repo). Het alternatief `--route tp-shm` werkt niet, want de
kern zit niet in het gameproces. De Wine-prefix is `ravengun-test-tsr`; daarin zitten native
`d3dx9_43`, `d3dcompiler_43` en `d3dcompiler_47`, want zonder die drie stopt de game met de
Windows-fout "E5017: Aborting due to not yet implemented feature: Writing fx_2_0 sampler objects
initializers is not implemented" (de HLSL-effectcompiler van Wine).

## Stand van de start

**START, en blijft draaien.** In `patches/STARTTEST.md`: spelproces gezien op seconde 2, de lader
leefde nog, laatste logregel `[launch-direct …] klaar (exitcode gestopt)`.

`[Status] Status = playable`. De meting uit `findings/TransformersShadowsRising.md` en de `Note` in
`game.ini`: laden duurt ongeveer 3,5 tot 4 minuten tot de attract, start (Move-start of toets 1),
trekker en richten werken, en het vizier is op 2026-10-11 op de tv nagemeten — binnen 1,5% van het
scherm op alle rasterpunten. Dat is bereikt met een **aangepast assenbereik** in het nagebootste bord
(assen verwisseld, `out_min`/`out_max` van -217..557); de kalibratie in het testmenu van de game is
niet nodig.

## Dongle en patches

**Sega-donglescan (`sega-scan`) — geen USB-sleutel die de game tegenhoudt.** Bewijs uit
`docs/loskoppelen-per-game.md` (rij 35 en open vraag 1): de spelmap bevat `Sega/shell/dk2win64.dll`,
`dk2win32.dll` en `dk2inst.dll` (Dinkey Pro) en `Sega/shell/Dongle - 261011.log` zegt
`DONGLE : MainThreadDongleScan: failed stage 1`. Dat is precies het `sega-scan`-type uit
`dongles/README.md` §2 van de loader-repo.

**De scan faalt, maar blokkeert niet:** de game is speelbaar (zie boven) — `docs/loskoppelen-per-game.md`
§5 zet Transformers expliciet bij "blokkeert niet (meer)".

Er is **geen `dongle.json`**, geen `dropin/` en geen `patches.ini`. `patches/STAND.md` telt de
ontbrekende `dongle.json` als gat (`sega-scan` verwacht). Er is geen `[Dongle]`-sectie en die hoort er
ook niet te komen: de loader kent geen dongles meer (`docs/geen-dongle-in-de-loader.md`).

**`sega-scan` past nog niet in de afspraak.** `docs/dropins.md` noemt vier `kind`-waarden (`hasp`,
`matrix`, `region`, `amdaemon`); deze game zou een **vijfde** nodig hebben. Dat staat als open vraag 9
in `docs/loskoppelen-per-game.md`: wordt `sega-scan` een vijfde `kind`, of valt Transformers onder
`region`?

## Wat er nog ontbreekt

1. **`dongle.json` van het soort `sega-scan`** — en eerst de beslissing of dat een eigen `kind` wordt.
2. **De eigen `Shell.exe`-drop-in** (`docs/loskoppelen-per-game.md`, rij 35: "de eigen
   `Shell.exe`-drop-in").
3. **Werksoort 2 — de eigen kern.** De gesloten `TeknoParrot(64).dll` draait mee en zit alleen in
   `Shell.exe`; dat is centrale arbeid, geen map.
4. **README** — dit bestand; in `patches/STAND.md` stond deze game tot deze ronde zonder README.

## Wat niet is vastgesteld

* **Of de donglescan echt onschadelijk is.** De game speelt, maar of fase 1 later alsnog iets
  blokkeert (een tweede fase, een langere sessie) is niet gemeten.
* **Van wie het logbestand is:** `Sega/shell/Dongle - *.log` — van de shell of van het spel? Dat is
  open vraag 1 in `docs/loskoppelen-per-game.md`.
* **Het vizier in het midden.** Bij midden-aim staat het vizier linksboven het midden (schaalverschil);
  de assen zijn bijgesteld, maar de kalibratie van de game zelf (`Sega/shell/testcalibrate.xml`) is
  niet gebruikt of getest.
* **Testmenu, gun-kalibratiescherm, hendels, speler 2 en recoil naar rumble**
  (`findings/TransformersShadowsRising.md`, kopje "Status" en "Open").
* **Twee sessies die kort na de start eindigden** met "Unexpect Game termination", niet reproduceerbaar.

## Verifiëren

```sh
cd ~/projects/ravengun-patches
python3 patches/stand-gen.py                            # de tabel en de tellingen in patches/STAND.md

cd ~/projects/ravengun
python3 tools/verify-all.py TransformersShadowsRising
python3 scripts/launch-direct.py TransformersShadowsRising --plan
```

## De SEGA Game Shell

Deze game start via de SEGA Game Shell (`Shell.exe` start `Transformers2.exe`). Deze dump is **voor-gepatcht**: zijn `Shell.exe` forceert het IO-bord en slaat de `/k=`-checksum over, en zijn `Game.ini` heeft `SecurityDisabled=1` en `NoVerify=1` — daardoor start hij onder onze lader zonder wijziging van ons. De vier poorten van de shell, de volgorde en waar elk antwoord vandaan komt (en het verschil tussen een voor-gepatchte en een kale dump) staan eenmalig in `patches/SEGA-GAME-SHELL.md`. Het IO-bord van de shell (voor lampen/toetsen/recoil) is iets anders dan het gun-bord van het spel (`SaeGunBoard`, `REPORT-io-board-and-loader.md`).

# RavenGun-patches

De patches per game voor [RavenGun](https://github.com/ravengun). **Dit staat bewust in een eigen
repo en niet in die van de loader:** de loader hoort schoon te zijn, en de patches zijn
gebruikersinhoud die per game verandert.

## Wat hier staat

```
patches/<Game>/
  game.ini        instellingen van de game
  patches.ini     bytepatches (Find/Replace)
  README.md       wat deze game nodig heeft en waarom
  dropin/         bestanden die in de view van de game komen
    dropin.ini    per bestand: To, DllOverride, Env, Preload
    <onze bibliotheek>   bv. ravengun-hasp-x64.so, matrix64.dll, librt-glfix.so
    <de gegevens>        bv. ravengun-hasp/<Game>/dongle.json + memory/
patches/_defaults/    wat alle games van een soort delen
patches/_template/    kopieer dit om een game toe te voegen
```

Een game toevoegen of bijwerken is **één map aanraken**. Zie `docs/een-game-toevoegen.md` in de
repo van de loader voor de stappen en `docs/dropins.md` voor het contract.

## Hoe de loader dit vindt

De loader zoekt de patches op deze plekken, in deze volgorde:

1. `$RAVENGUN_PATCHES_DIR` — zet dit op de map hier als je aan de loader werkt.
2. `~/.local/share/ravengun/patches-shipped` — een geïnstalleerde kopie.
3. `~/.local/share/ravengun/patches` — de gebruikerslaag.

De gebruikerslaag wint per sleutel. Wie alleen games toevoegt hoeft de loader dus niet te bouwen:
zet de map in de gebruikerslaag, of zet `RAVENGUN_PATCHES_DIR` in de omgeving.

## Wat hier niet hoort

- De loader zelf (`src/`, `scripts/`, `tools/`) — die staat in de repo van RavenGun.
- De **bron** van onze bibliotheken (`hasp/`, `matrix/`, `tools/rt-glfix/`) — die staat bij de
  loader. Wat hier staat is het **gebouwde** bestand dat de game nodig heeft; `scripts/build-*.sh`
  in de loader zet het hier neer.

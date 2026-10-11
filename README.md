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

## Kaarten: de stand per game

Vier documenten die samen vertellen wat elke game nodig heeft. Ze worden **gegenereerd**; pas de
map van de game aan en draai het script opnieuw in plaats van ze met de hand bij te werken.

| Bestand | Wat het geeft | Vernieuwen |
|---|---|---|
| `patches/STAND.md` | de volledigheidskaart: per game `game.ini`, `[Status]`, README, drop-in (bestanden tegen secties), dongle-soort, en wat er nog ontbreekt | `python3 patches/stand-gen.py` |
| `patches/STARTTEST.md` | de startmeting: per game of hij start en blijft draaien, en waar de `[Status]` daarvan afwijkt | `bash /home/deck/starttest3.sh 22` en daarna `python3 patches/starttest-gen.py` |
| `patches/REGIO-PER-GAME.md` | per game of hij een regio/printplaat-dongle vraagt, met het bewijs | zie de kop van dat bestand |
| `patches/PATCHES-PER-GAME.md` | per game welke patch nodig is (drop-in, `patches.ini`, eigen kern) en of die er is | zie de kop van dat bestand |

De dongle van een game staat in `patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json` (veld
`kind`: `hasp`, `matrix`, `region`, `sega-scan`, `amdaemon`). **Er is geen `[Dongle]`-sectie in
`game.ini`**: de loader weet niets van dongles, een software-dongle is een gewone drop-in.

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

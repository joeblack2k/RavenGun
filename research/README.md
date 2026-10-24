> **Merged 2026-10-24.** This was the separate `ravengun-patches` repository; it is now the `research/` folder of the one
> RavenGun repository (history kept in the merge commit). The live per-game files are in `patches/` at the top of the repo;
> this folder holds the research, measurements and evidence, and is neither installed nor part of a release
> (`.gitattributes`). Paths below that say `patches/<Game>/` mean `research/patches/<Game>/` here.

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

Acht documenten die samen vertellen wat elke game nodig heeft. **Zeven ervan worden gegenereerd**
(`STAND.md`, `STARTTEST.md`, `EIGEN-KERN.md`, `ZONDER-LADER.md`, `ELF-NATIVE.md`, `INVOER-ROUTE.md`
en de tabel van `REGIO-PER-GAME.md`); pas de map van de game aan en draai het script opnieuw in
plaats van ze met de hand bij te werken. **Eén is met de hand geschreven:** `PATCHES-PER-GAME.md`, want de kolom *welke
patch nodig is* is een oordeel op grond van meerdere bronnen en dat is niet uit de mappen af te
leiden.

| Bestand | Wat het geeft | Vernieuwen |
|---|---|---|
| `patches/STAND.md` | de volledigheidskaart: per game `game.ini`, `[Status]`, README, drop-in (bestanden tegen secties), dongle-soort, en wat er nog ontbreekt | `python3 patches/stand-gen.py` |
| `patches/STARTTEST.md` | de startmeting: per game of hij start en blijft draaien, en waar de `[Status]` daarvan afwijkt | `bash /home/deck/starttest3.sh 22` en daarna `python3 patches/starttest-gen.py` |
| `patches/REGIO-PER-GAME.md` | per game of hij een regio/printplaat-dongle vraagt, met het bewijs | zie de kop van dat bestand |
| `patches/PATCHES-PER-GAME.md` | per game welke patch nodig is (drop-in, `patches.ini`, eigen kern) en of die er is | **met de hand**; de kop noemt de commando's om de getallen na te rekenen |
| `patches/EIGEN-KERN.md` | welke games onze eigen OpenParrot-kern überhaupt **herkent** (dezelfde CRC die de kern zelf rekent) en welk `EmulationProfile` er dan nog moet komen — dus wat er per game nodig is om van de gesloten `TeknoParrot(64).dll` af te gaan | `python3 patches/kern-herkenning.py` |
| `patches/INVOER-ROUTE.md` | per game **wie de invoer levert**: de IO-brug van de daemon, of een kern. Dat scheidt de games die echt een eigen `EmulationProfile` vragen (route `tp-shm` op de gesloten kern) van de games waar de invoer al buiten elke kern om loopt — en dat zijn er minder dan het totaal aantal profielen dat `EIGEN-KERN.md` noemt | `python3 patches/invoer-route.py` |
| `patches/ZONDER-LADER.md` | welke games met **alleen `wine <exe>`** starten en blijven draaien — geen lader en geen kern. De scherpste vraag over loskoppelen, met de eerlijke grenzen erbij: de drop-in van een eerdere start staat nog in de view (de kaart meet per game hoeveel), en dit zegt of een game *blijft draaien*, niet of hij speelbaar is | `python3 patches/proef-zonder-lader.py` |
| `patches/ELF-NATIVE.md` | of de game-ELF van de Cygwin-lader (`ElfLdr2`) ook **los** te starten is — hij is een gewone Linux-ELF, dus dat is de vraag die de hele lader overbodig zou maken. Antwoord: nee, gemeten, met per game welke bibliotheken deze machine mist | `python3 patches/elf-native.py` |

Voor de familie van de Cygwin-lader is er daarnaast `patches/ELF-LADER-SPEC.md`: **geen
stand-kaart maar een technische spec** van wat een eigen ELF-lader moet doen, met in de kop de
commando's om elk getal na te rekenen. Ook met de hand geschreven, net als
`PATCHES-PER-GAME.md`.

De dongle van een game staat in `patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json` (veld
`kind`: `hasp`, `matrix`, `region`, `sega-scan`, `amdaemon`). **Er is geen `[Dongle]`-sectie in
`game.ini`**: de loader weet niets van dongles, een software-dongle is een gewone drop-in.

## Gereedschap

Elke kaart heeft zijn eigen generator. Eén script maakt geen kaart maar **controleert** iets.

| Script | Wat het doet |
|---|---|
| `patches/stand-gen.py` | schrijft `patches/STAND.md` |
| `patches/starttest-gen.py` | schrijft `patches/STARTTEST.md` uit de ruwe uitvoer van `starttest3.sh` |
| `patches/regio-gen.py` | schrijft de tabel van `patches/REGIO-PER-GAME.md` (scant de NAS) |
| `patches/kern-herkenning.py` | schrijft `patches/EIGEN-KERN.md` |
| `patches/invoer-route.py` | schrijft `patches/INVOER-ROUTE.md` uit de `[Input]`-secties van de games |
| `patches/proef-zonder-lader.py` | schrijft `patches/ZONDER-LADER.md` — start elke game met alleen `wine <exe>` |
| `patches/elf-native.py` | schrijft `patches/ELF-NATIVE.md` — start de ELF's van de ElfLdr2-games los |
| `patches/readme-gen.py` | vult de README van een game die nog de lege stub heeft, uit de kaarten; raakt README's met inhoud niet aan |
| `patches/dropin-aankomst.py` | **controleert** of elk drop-in-bestand met dezelfde sha256 in de view staat; exitcode 1 als er iets mis is |
| `patches/dongle-controle.py` | **controleert** elke dongle met het gereedschap van zijn soort (`hasp.py`, `matrix.py`, `region.py`) en meldt een rood oordeel; exitcode 1 als er een rood is |

**Let op bij `scripts/migrate-game-settings.py` (lader-repo):** dat script schrijft in **deze** map --
`ravengun/patches` is een symlink naar `ravengun-patches/patches`. Het haalt zijn invoer uit
`packaging/io-routes.json` en `packaging/games-notes.json`, en die staan niet in de working tree (ze
komen via `--rev` uit een andere revisie). Draai je het kaal, dan meldt het wel "33 games verwerkt",
maar produceert het **minimale** bestanden: op 2026-10-12 meldde `--check` dat **14**
`game.ini`-bestanden zouden veranderen, en in dat verschil ontbraken bij die bestanden de hele
`[Input]`, `[Status]`, `[Config]` en `[Identify]`. Dus: nooit kaal draaien, altijd eerst `--check`,
en liever met `--out` naar een map buiten de repo als je wilt zien wat er gebeurt. Zonder
`io-routes.json` blijven `[Input]` en `[Launch] Mode` ongemoeid en zonder `games-notes.json` blijft
`[Status]` ongemoeid -- maar de rest niet.

`tools/verify-all.py` (loader-repo) meldt "drop-ins die NIET aankomen: 0", maar dat kijkt naar het
**plan** — de paden die de launcher zou schrijven. `patches/dropin-aankomst.py` kijkt naar de
**bytes** die er staan, en meldt ook een symlink naar de NAS of een bestand met een andere inhoud.

Zo zijn er twee controles die geen kaart maken maar iets narekenen: `dropin-aankomst.py` kijkt of de
drop-in-bestanden echt aankomen, en `dongle-controle.py` of elke donglebeschrijving door het
gereedschap van zijn soort wordt goedgekeurd. Dat laatste vond in ronde 38 dat OG's rode oordeel aan
een ontbrekend gebruikersprofiel ligt en niet aan de gegevens (zie `patches/OG/README.md`).

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

# Nieuw op de NAS (gemeten 2026-10-12)

*De eigenaar heeft een batch games aan de `Teknoparrot`-map toegevoegd. Dit bestand legt vast **wat**
er bij staat en **wat voor soort game** het is, zodat de inventaris per game daarop verder kan
bouwen. Alles hieronder is **gemeten** met `ls`, `du`, `file` en `strings` op de NAS-mount; er is
geen enkele game gestart. De NAS staat technisch op `rw` maar wij schrijven er niet in.*

## De telling

* top-level mappen op de NAS: **90**
* daarvan op 2026-10-12 aangeraakt: **29** — één batch tussen 10:42 en 13:03, plus
  `Far Cry Paradise Lost` om 18:37.

De mtime is de betrouwbaarste weg hier: onze eigen mapnamen (`GalagaAssault`, `HOTDSD`,
`MedaruNoGunman`) verschillen van de weergavenamen op de NAS (`Galaga Assault`,
`House of the Dead Scarlet Dawn`), dus een naamvergelijking gaf eerst 43 valse treffers. Wie dit
opnieuw wil: `ls -lt --time-style=+%Y-%m-%d_%H:%M` op de NAS-top.

## Groep 1 — de ElfLdr2-familie (deze vragen onze eigen lader)

Kenmerk: een bestand dat precies `game` heet, en/of de `HASPE`-marker in dat bestand.

| game | `game`-bestand | oordeel |
|---|---|---|
| **Target Terror Gold** | 32-bits i386; de HASPE-treffer bleek een **hardwaresymbool**, niet de lader-marker | **ongepakt** (correctie 2026-10-12) |
| **Walking Dead** | 32-bits i386, **HASPE-treffers** | **gepakt (HASPE)** |
| **Big Buck Hunter Pro** | 32-bits i386, geen HASPE | **ongepakt** |
| **Big Buck Hunter Pro Home** | 32-bits i386, geen HASPE | **ongepakt** |
| Let's Go Jungle (Lost on the Island of Spice) | `game` is een **shell-script** | onbepaald |
| Let's Go Jungle Special | idem | onbepaald |
| Primeval Hunt | idem | onbepaald |
| The House of the Dead 4 | idem | onbepaald |
| Too Spicy | idem | onbepaald |

**Waarom dit het belangrijkste deel is:** de gepakte klasse gaat hiermee van **vier naar vijf**
games: **Walking Dead** komt erbij, en die is van hetzelfde soort als Jurassic Park, Galaga Assault
en Pacman Chomp-Mania — precies de klasse waar onze eigen ELF-lader voor gebouwd wordt.

**Correctie op deze tabel (2026-10-12, na een tweede meting).** Target Terror Gold is **niet**
gepakt. De HASPE-treffer kwam van een **hardwaresymbool** in het bestand, niet van de pakker-marker
`PATCHED_WITH_HASPE_LINUX_LOADER_1.1`; die marker staat er niet in. Het bestand heeft **36**
sectiekoppen, is niet gestript en heeft debug-informatie, het aantal symbolen uit `DT_HASH` is gelijk
aan het einde van de tabel, en de PLT pusht echte relocatie-indexen. Het is dus een gewone ELF, net
als de twee Big Buck Hunters. **Walking Dead is wél gepakt**: 807 `deadbeef`-stubs, `SHNUM = 0`, de
marker aanwezig, en 701 naamloze entries (86 procent, het hoogste van alle games tot nu toe).

**En `SHNUM = 0` bewijst op zichzelf niets:** de twee Big Buck Hunters hebben het ook, zonder enige
pakking.

**Eerlijke grens:** bij de vijf games met een shell-script als `game` meet de HASPE-telling het
**script** en niet de echte binary. Of die gepakt zijn is dus nog niet vastgesteld.

## Groep 2 — Windows-games (deze vragen de core-route, geen ELF-lader)

Een `.exe` binnen twee niveaus, dus een Windows-programma onder Wine.

Action Deka (`bms_GDK.exe`), After Dark (`GamePlayer.exe`), Aliens Extermination
(`abhRelease.exe`), Block King Ball Shooter (`game.exe`), Bug Busters (`BBPC.exe`), Coopers's 9
(`game.exe`), Drakons Realm Keepers (`Game.exe`), Dream Raiders (`Game.exe`), Elevator Action Death
Parade (`Game.exe`), Elevator Action Invasion (`ESGame.exe`), Far Cry Paradise Lost
(`Farcry_R.exe`), Friction (`Friction.exe`), Gunslinger Stratos 3 (`game.exe`, 43 GB), Lost Land
Adventure (`AMAuthd.exe`), Mars Sortie (`CH372DRV.EXE`), Men in Black (`SystemLauncher.exe`),
Mighty Moto (`motobikeV1.0.9.exe`), Night Hunter — After Dark Chapter 2 (`qumo2_en.exe`), Point
Blank X (`PBX100-2-NA-MPR0-A63.exe`), Pull The Trigger (`Corrector2.exe`), Robin Hood (`game.exe`),
SEGA Golden Gun (`RingGunR_RingWide.exe`), Silent Hill The Arcade (`KSHG.exe`), Transformers Human
Alliance (`FFBPluginGUI.exe`), Wartran Troopers (`BOOT.exe`), Wild West Shootout (`CowBoy.exe`).

## Groep 3 — nog onbepaald (waarschijnlijk Lindbergh)

Geen `game` en geen `.exe` binnen twee niveaus. Eén ervan is wél te duiden: **Rambo** heeft een
`elf/`-map met `libkswapapi.so` en `libosuser.so`, plus een `game.sh` — dat is de
**Lindbergh**-signatuur, een andere Linux-lader-familie dan ElfLdr2.

America's Army Arcade, Ghost Squad Evolution, Metal Gear Arcade, Police Trainer 2, Rambo, Silent
Scope Bone Eater, Star Trek Voyager, Virtua Cop 3.

## Hoe je dit narekent

```bash
NAS=/home/deck/mnt/spidernas_emulation/Teknoparrot
# welke mappen zijn recent aangeraakt?
ls -lt --time-style=+%Y-%m-%d_%H:%M "$NAS" | grep '^d' | head -30
# wat voor soort game is het? (per map)
find "$NAS/<naam>" -maxdepth 3 -type f -name 'game'          # ElfLdr2-familie
strings -a "$NAS/<naam>/game" | grep -c HASPE                # gepakt?
find "$NAS/<naam>" -maxdepth 2 -type f -iname '*.exe'        # Windows
```

## Wat hierna nog moet

* De vijf shell-script-games uitzoeken: wat start dat script, en is die binary gepakt?
* Groep 3 uitzoeken: welke laderfamilie, en hebben die een eigen route nodig?
* Per game de gewone inventaris: `patches/<Game>/` met `game.ini`, `README.md` en de kaarten
  opnieuw (`patches/STAND.md` en de andere generatoren).

# House of the Dead: Scarlet Dawn (HOTDSD)

Instellingen: `game.ini`. Bevindingen en meetgeschiedenis: `findings/HOTDScarletDawn.md` op de NAS
naast de games, niet in deze repo. **Let op de naam:** dat bestand heet `HOTDScarletDawn.md`, niet
`HOTDSD.md`; daarom staat er (nog) geen `Findings`-sleutel in `game.ini`.

## Wat deze game is

| | |
|---|---|
| Naam | House of the Dead: Scarlet Dawn |
| Fabrikant | Sega (platform SEGA ALLS; `findings/HOTDScarletDawn.md` noemt "Sega ALLS") |
| Jaar | 2018 |
| Platform | Sega ALLS, Unreal Engine 4.18, 64-bits |
| Genre | Shooter (lightgun) |
| Lader | `Type = TeknoParrot`, `EmulationProfile = ALLSHOTDSD` → `OpenParrotLoader64.exe` + `TeknoParrot64.dll` |
| Profiel | `GameProfiles/HOTDSD.xml`, revisie 21 |
| Programma's | **twee**: `amdaemon.exe` eerst, dan `Hodzero-Win64-Shipping.exe` |

Naam, jaar, platform en genre komen uit `Metadata/HOTDSD.json`; de UE4-versie en de 64-bits kern uit
de kop van `findings/HOTDScarletDawn.md`. De laderfamilie komt uit `docs/loskoppelen-per-game.md`
(rij 8) van de loader-repo.

`game.ini` legt de twee exe's vast zoals het profiel ze heeft: `Executable = Hodzero-Win64-Shipping.exe`,
`Executable2 = amdaemon.exe`, `HasTwoExecutables = true` en `LaunchSecondExecutableFirst = true`. De
volgorde en de wachttijd staan als open punt in het bestand zelf: eerst `amdaemon.exe`, 5 seconden
wachten (`DAEMON_WAIT_SECONDS` uit `StartApp.bat`), dan de game.

## Stand van de start

**Niet in `patches/STARTTEST.md`.** Dat bestand dekt de 33 games met een vindbaar pad; deze hoort bij
de drie die niet te plannen zijn. `python3 scripts/launch-direct.py HOTDSD --plan` stopt met **rc=4**
omdat `launch-direct.py` geen profielen met twee exe's start ("profielen met twee exe's worden niet
direct gestart").

**De speelbare route is TeknoParrotUi**, en die is gemeten: `[Launch] Mode = tpui`. De `[Status]` van
`game.ini` staat op `playable` met de meting erbij — attract met "FREE PLAY", start (toets 1),
Mode Select, itemkeuze, laden (ongeveer 1,5 minuut), de eerste ronde, trekker en subtrekker werken;
vizier gemeten op hoeken en midden. Speler 2, de echte Move-knoppen en het testmenu zijn niet getest.

Twee dingen die in `game.ini` staan omdat ze nodig zijn:

* `[Env] RG_UNSET_REMOTETHREAD = 1` met `[Wine] Runner = wrapper`: TeknoParrotUi zet onder Wine zelf
  `TP_REMOTETHREAD` (de remote-thread-route) en daar faalt de injectie ("Failed to find kernel32.dll
  in target process"); de wrapper haalt de variabele weg, zodat de lader zijn standaardroute neemt
  (entry-point-trampoline, "Have fun :)"). Zonder die route haakt de kern niet aan.
* `[Wine] Prefix = shared`, de gedeelde prefix.

## Dongle en patches

**Geen dongle in de zin van een USB-sleutel.** `findings/WAT-ELK-SPEL-NODIG-HEEFT.md` (Johan,
TeknoParrot-team) is expliciet: Scarlet Dawn staat in de lijst van games **zonder** HASP-bestand, en
wat deze game nodig heeft is `amdaemon.exe` — Sega's AM Daemon — die **vóór** de game moet draaien.
De open vervanging daarvoor is `minime` uit de SegaTools. `docs/loskoppelen-per-game.md` (rij 8)
groepeert deze game daarom onder het dongle-soort **`twee-exe (amdaemon)`**.

Er is **geen `dongle.json`**. In `patches/STAND.md` staat deze game dan ook bij "dongle verwacht maar
nog geen `dongle.json`" (`stand-gen.py` heeft `amdaemon` in zijn `EXPECTED_DONGLE`-lijst). Er is geen
`dropin/` en geen `patches.ini`.

Wat de eigenaar hiermee besloten heeft (ronde 23, verwerkt in `findings/HOTDScarletDawn.md`): de
dongle-laag is **niet overgenomen**, en de route via TeknoParrotUi blijft de speelbare route. Er is
dus geen drop-in nodig zolang die route loopt.

Er is geen `[Dongle]`-sectie en die hoort er ook niet te komen: de loader kent geen dongles meer
(`docs/geen-dongle-in-de-loader.md`).

## Wat er nog ontbreekt

1. **`dongle.json` van het soort `amdaemon`** — of de expliciete vastlegging dat deze game geen
   dongle heeft en alleen de startketen nodig heeft. Nu ontbreekt het bestand en telt de kaart het als
   een gat.
2. **Een directe start met twee exe's.** `launch-direct.py` weigert dat nu (rc=4). De proef is gedaan
   (beide loaders, tweede exe 3 s vooraf, `RG_EXTRA_PIPES=TeknoParrotPipe`) maar weer teruggedraaid:
   de game startte tot de attract, **zonder invoer** — start en test komen in deze route alleen via de
   `TeknoParrotPipe` van TeknoParrotUi. Dat staat in `findings/HOTDScarletDawn.md`.
3. **Een startmeting in `patches/STARTTEST.md`** — die is er niet, juist omdat de start niet te plannen is.
4. **De `Findings`-sleutel.** Het bevindingenbestand bestaat wel, maar heet anders dan de game; wie de
   sleutel wil zetten moet hem op `findings/HOTDScarletDawn.md` richten.
5. **README** — dit bestand; in `patches/STAND.md` stond deze game tot deze ronde zonder README.

## Wat niet is vastgesteld

* **Of `minime` AM Daemon hier echt kan vervangen.** Dat is de aanbeveling uit
  `findings/WAT-ELK-SPEL-NODIG-HEEFT.md`, maar het is niet gemeten; in de gemeten route draait de
  echte `amdaemon.exe` mee.
* **Speler 2, de echte Move-knoppen, het testmenu en langere sessies** (`findings/HOTDScarletDawn.md`,
  kopje "Open").
* **Het ene verdwenen spelproces** ongeveer 70 s na de itemkeuze; `AMDaemon/config.json` heeft
  `error.auto_reset = true`, dus een herstart na een fout is gewoon gedrag, maar de oorzaak van dat
  ene geval is niet gevonden.

## Verifiëren

```sh
cd ~/projects/ravengun-patches
python3 patches/stand-gen.py                       # de tabel en de tellingen in patches/STAND.md

cd ~/projects/ravengun
python3 tools/verify-all.py HOTDSD                 # verwacht: plan mislukt, rc=4
python3 scripts/launch-direct.py HOTDSD --plan
```

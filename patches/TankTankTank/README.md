# Tank! Tank! Tank!

Instellingen: `game.ini`. Er is **geen** bevindingenbestand: het spel staat niet op de NAS, dus er is
niets aan een draaiende game gemeten. Daarom staat er ook geen `Findings`-sleutel in `game.ini`.

## Wat deze game is

| | |
|---|---|
| Naam | Tank! Tank! Tank! |
| Genre | Action |
| Jaar | 2009 |
| Platform | Namco ES1 |
| Fabrikant | Namco — de kop van `game.ini` noemt hem; zie onder |
| Lader | `Type = ElfLdr2`, `EmulationProfile = DeadHeat` → `ElfLdr2/BudgieLoader(_x64).exe` |
| Profiel | `GameProfiles/TankTankTank.xml`, revisie 11, `ExecutableName = n_tank_release` |

Naam, genre, jaar en platform komen uit `Metadata/TankTankTank.json` van de TeknoParrotUi-runtime
(2.0.0.130); dat bestand heeft **geen fabrikantveld**. De fabrikant staat in de **kop van
`game.ini`** zelf — *"Tank! Tank! Tank! (Namco, ElfLdr2/N2-lader)"* — en het platform heet Namco ES1.
Dat is een uitspraak in deze repo, geen meting aan het spel. De laderfamilie komt uit
`docs/loskoppelen-per-game.md` (rij 32) van de loader-repo: `ElfLdr2` hoort bij BudgieLoader
(`ElfLdr2/BudgieLoader.exe` of `x64/BudgieLoader_x64.exe`). De kop van `game.ini` noemt hem de
"ElfLdr2/N2-lader"; de ladertabel zet **geen enkele** game in deze lijst op de N2-variant van
BudgieLoader, dus `ElfLdr2/BudgieLoader` is de juiste.

**Fabrikant nog bevestigen:** het spel dumpen en de exe en de bestandsboom bekijken, of de titel in
een arcadedatabase opzoeken. Zonder dump is dat niet te doen.

**Het profiel is een leenprofiel.** `EmulationProfile = DeadHeat` is een **raceprofiel**, en de
bindings in het profiel horen daarbij: een stuur-as (`Wheel Axis` → `Analog0`), gas en rem
(`Analog2`/`Analog4`) en `FFB Blaster/Enable`. Voor deze game zijn vooral de twee vuurknoppen en de
veiligheidsknop van belang (`Fire Button L` → `P1Button2`, `Fire Button R` → `P1Button3`,
`Safety Button` → `P2Button1`), plus Test, Service, Coin en Enter. Of die indeling klopt is **niet
gemeten** — het spel is er niet.

### Wat er in `game.ini` bijzonder is

* `[Env] TP_NUSOUND = 1`: TeknoParrotUi zet deze variabele voor dit profiel; de lader gebruikt dan zijn
  eigen geluidsroute. Dit stond vroeger als **vaste uitzondering op profielnaam** in
  `scripts/launch-direct.py`; sinds de migratie is het gegevens in `patches/` (`docs/decisions.md`).
* `[Game] Executable = n_tank_release` zonder extensie — precies zoals `ExecutableName` in het profiel.

## Stand van de start

**Nooit gemeten.** Deze game staat niet in `patches/STARTTEST.md`: dat bestand dekt de 33 games met
een vindbaar pad, en deze hoort bij de drie die niet te plannen zijn.

De oorzaak is dat er geen gamemap is: het spel staat niet op de NAS. Op de Deck is de NAS gemount op
`/home/deck/mnt/spidernas_emulation/Teknoparrot`, en daar bestaat **geen** map die op `*Tank*` past
(nagekeken; `docs/loskoppelen-per-game.md` rij 32 noemt dezelfde uitkomst op het NAS-pad).
`python3 scripts/launch-direct.py TankTankTank --plan` stopt daardoor met **rc=4** ("game niet
gevonden: '' — voeg de map met de game toe als gamemap").

## Dongle en patches

**Geen aanwijzing voor een dongle.** Het profiel heeft geen `DongleRegion`-, `PcbRegion`- of
`Region`-veld (`docs/loskoppelen-per-game.md`, rij 32), er is geen `dongle.json`, er is geen `dropin/`
en er is geen `patches.ini`. Volgens de afspraak betekent "geen `dongle.json`" ook echt "geen dongle"
(`docs/dropins.md`).

**Maar let op wat dit bewijs wel en niet is.** Het is een uitspraak over het **profiel**, niet over het
spel. De bestandsscan die bij de andere games de dongle aanwijst
(`docs/research/loskoppelen-dongle-bewijs.md`) heeft deze game nooit gehad, want er zijn geen bestanden
om te scannen. Er is dus **geen bestandsevidence**. Zodra de dump er is, moet die scan opnieuw.

Er is geen `[Dongle]`-sectie en die hoort er ook niet te komen: de loader kent geen dongles meer
(`docs/geen-dongle-in-de-loader.md`).

## Wat er nog ontbreekt

1. **Het spel zelf.** Zonder dump is alles hieronder onbekend.
2. **Een gamemap en een startmeting** — daarna `bash /home/deck/starttest3.sh 22 TankTankTank`.
3. **De bestandsscan op dongle-artefacten**, als het spel er is.
4. **`patches.ini`** als er bytepatches nodig blijken (nu niet aanwezig).
5. **`dongle.json` en `dropin/`** als de scan iets vindt (nu niet nodig volgens het profiel).
6. **README** — dit bestand; in `patches/STAND.md` stond deze game tot deze ronde zonder README.

`[Status] Status` stond niet in `game.ini` en is toegevoegd: **`untested`**, met de reden in de `Note`.

## Verifiëren

```sh
cd ~/projects/ravengun/research
python3 patches/stand-gen.py                      # de tabel en de tellingen in patches/STAND.md

cd ~/projects/ravengun
python3 tools/verify-all.py TankTankTank          # verwacht: plan mislukt, rc=4
python3 scripts/launch-direct.py TankTankTank --plan
```

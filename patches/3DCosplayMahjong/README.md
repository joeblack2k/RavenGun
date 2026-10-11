# 3D Cosplay Mahjong

Instellingen: `game.ini`. Er is **geen** bevindingenbestand: het spel staat niet op de NAS, dus er is
niets aan een draaiende game gemeten. Daarom staat er ook geen `Findings`-sleutel in `game.ini`.

## Wat deze game is

| | |
|---|---|
| Naam | 3D Cosplay Mahjong |
| Genre | Puzzle |
| Jaar | 2013 |
| Platform | Taito NESiCAxLive |
| Fabrikant | **niet vastgesteld** — zie onder |
| Lader | `Type = TeknoParrot`, `EmulationProfile = TaitoTypeXGeneric` → `OpenParrotLoader(64).exe` met de TeknoParrot-kern als argument |
| Profiel | `GameProfiles/3DCosplayMahjong.xml`, revisie 5 |

Naam, genre, jaar en platform komen uit `Metadata/3DCosplayMahjong.json` van de TeknoParrotUi-runtime
(2.0.0.130). Dat bestand heeft **geen fabrikantveld**: het noemt alleen het platform. NESiCAxLive is
Taito's systeem, maar daarmee is niet bewezen wie het spel gemaakt heeft. De laderfamilie komt uit
`docs/loskoppelen-per-game.md` (rij 1) van de loader-repo, het profiel uit `game.ini` en
`GameProfiles/3DCosplayMahjong.xml`.

**Fabrikant nagaan:** het spel dumpen en de exe, het logo en de bestandsboom bekijken (zoals de
bestandsscan in `docs/research/loskoppelen-dongle-bewijs.md` doet), of de titel opzoeken in een
arcadedatabase. Zonder dump is dat niet te doen.

### Wat er in `game.ini` bijzonder is

* `[Env] LANG = ja_JP.UTF-8` en `LC_ALL = ja_JP.UTF-8`: TeknoParrotUi start dit profiel met een
  Japanse landinstelling. Dit stond vroeger als **vaste uitzondering op profielnaam** in
  `scripts/launch-direct.py`; sinds de migratie is het gegevens in `patches/` (`docs/decisions.md`,
  ronde met de vaste uitzonderingen).
* `[Display] GamescopeCompatibility = RequireWindowed`, uit het profiel.
* `[Config] General/Windowed = 1` en `General/Input API = DirectInput` — de game draait in een venster.

## Stand van de start

**Nooit gemeten.** Deze game staat niet in `patches/STARTTEST.md`: dat bestand dekt de 33 games met
een vindbaar pad, en deze hoort bij de drie die niet te plannen zijn.

De oorzaak is dat er geen gamemap is: het spel staat niet op de NAS. Op de Deck is de NAS gemount op
`/home/deck/mnt/spidernas_emulation/Teknoparrot`, en daar bestaat **geen** map die op `*Cosplay*`
past (nagekeken; `docs/loskoppelen-per-game.md` rij 1 noemt dezelfde uitkomst op het NAS-pad).
`python3 scripts/launch-direct.py 3DCosplayMahjong --plan` stopt daardoor met **rc=4** ("game niet
gevonden: '' — voeg de map met de game toe als gamemap"). Zonder gamemap is er geen exe om te starten
en geen spelproces om te meten.

## Dongle en patches

**Geen aanwijzing voor een dongle.** Het profiel heeft geen `DongleRegion`-, `PcbRegion`- of
`Region`-veld (`docs/loskoppelen-per-game.md`, rij 1), er is geen `dongle.json`, er is geen `dropin/`
en er is geen `patches.ini`. Dat is ook de machineleesbare betekenis van "geen dongle": geen
`dongle.json` (`docs/dropins.md`).

**Maar let op wat dit bewijs wel en niet is.** Het is een uitspraak over het **profiel**, niet over het
spel. De bestandsscan die bij de andere games de dongle aanwijst (`docs/research/loskoppelen-dongle-bewijs.md`)
heeft deze game nooit gehad, want er zijn geen bestanden om te scannen. Er is dus **geen
bestandsevidence**. Zodra de dump er is, moet die scan opnieuw.

Er is geen `[Dongle]`-sectie en die hoort er ook niet te komen: de loader kent geen dongles meer
(`docs/geen-dongle-in-de-loader.md`).

## Wat er nog ontbreekt

1. **Het spel zelf.** Zonder dump is alles hieronder onbekend.
2. **Een gamemap en een startmeting** — daarna `bash /home/deck/starttest3.sh 22 3DCosplayMahjong`.
3. **De bestandsscan op dongle-artefacten**, als het spel er is.
4. **`patches.ini`** als er bytepatches nodig blijken (nu niet aanwezig).
5. **`dongle.json` en `dropin/`** als de scan iets vindt (nu niet nodig volgens het profiel).
6. **README** — dit bestand; in `patches/STAND.md` stond deze game tot deze ronde zonder README.

`[Status] Status` stond niet in `game.ini` en is toegevoegd: **`untested`**, met de reden in de `Note`.

## Verifiëren

```sh
cd ~/projects/ravengun-patches
python3 patches/stand-gen.py                      # de tabel en de tellingen in patches/STAND.md

cd ~/projects/ravengun
python3 tools/verify-all.py 3DCosplayMahjong      # verwacht: plan mislukt, rc=4
python3 scripts/launch-direct.py 3DCosplayMahjong --plan
```

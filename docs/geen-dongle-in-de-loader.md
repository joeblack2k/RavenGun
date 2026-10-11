# Geen dongle-begrip in de loader

*Beslissing van de eigenaar, ronde 12 (2026-10-11). Dit vervangt het `[Dongle]`-deel van
`docs/dropins.md` en de `dongle.json`-afspraak.*

## De beslissing

**De loader weet niets van dongles.** Geen keuzelijst in het menu, geen `[Dongle]`-sectie, geen
poort, geen exitcode 6, geen `dongle.json`.

Alles wat een game nodig heeft staat in `patches/<Game>/`. **Een software-dongle is vanaf nu gewoon
een drop-in**: een bibliotheek die in de view gezet wordt, met zijn eigen gegevens ernaast. De loader
kopieert bestanden en zet omgevingsvariabelen — meer niet.

Het argument van de eigenaar, en het is een goed argument: **de loader blijft schoon.** Wat een game
nodig heeft om te draaien is data in de map van die game, en de loader is een domme uitvoerder.

## Wat er verdwijnt

| Weg | Waarom |
|---|---|
| `[Dongle]` in `game.ini` (`Enabled`, `Source`) | de loader leest geen dongle-instellingen meer |
| `dongle.json` per game | de gegevens horen naast de drop-in die ze gebruikt |
| `dongle_gate()`, `DongleRefused`, exitcode 6 | er is geen poort meer om te bewaken |
| de keuzelijst "Dongle source" en het vinkje "Use dongle" | de app toont geen dongle meer |
| `scripts/dongle.py` (detectie van de fysieke dongle) | alleen de poort gebruikte hem |
| `scripts/gen-dongle-inventaris.py` | de dongle-inventaris is geen loaderbegrip meer |
| `DONGLE_SOURCES`, `kDongleSources`, `dongleSources()` | de bronnenlijst bestaat niet meer |
| `dropin_dongle()` en de `kind`-dispatch in `launch-direct.py` | de drop-in-motor doet het werk |

## Wat blijft, en hoe het heet

De **drop-in-motor** blijft precies zoals hij is — die is dongle-agnostisch en dat is de kern van de
beslissing:

```
patches/<Game>/
  game.ini
  patches.ini
  README.md
  dropin/
    dropin.ini            per bestand: To, DllOverride, Env, Preload
    <onze bibliotheek>    bv. ravengun-hasp-x64.so, matrix64.dll
    <de gegevens>         bv. hasp/dongle.json + hasp/memory/<fileid>
```

De **bibliotheek leest zijn eigen gegevens**, niet de loader. De drop-in geeft hem de weg:

```ini
[ravengun-hasp-x64.so]
To = .
Preload = true
Env = RAVENGUN_HASP_DIR=hasp;RAVENGUN_HASP_GAME=Halo
```

Daarmee is `hasp/dongle.json` + `hasp/memory/<fileid>` gewoon **data naast de bibliotheek**. De loader
weet niet wat erin staat en hoeft dat ook niet te weten.

## Wat dit betekent voor de spelbestanden

De map `patches/<Game>/dongle.json` **verhuist** naar `patches/<Game>/dropin/<laag>/dongle.json`,
naast de bibliotheek die hem leest. Voor de games waar nog geen drop-in is (Terminator, NERF,
GhostBusters, HOTDSD, Transformers) betekent het dat er niets in de loader verandert tot die drop-in
er is — en dat is precies de bedoeling: **geen dongle-configuratie in de loader, alleen een map met
bestanden.**

## De fysieke dongle (USB pass-through)

De eigenaar noemde de **software**-dongle. De fysieke dongle is iets anders: dat is een USB-apparaat
van de eigenaar dat Wine moet kunnen openen, en dat is een **systeem**-instelling (udev `uaccess`),
geen spellogica.

**Keuze die ik hier maak, en die je moet kunnen terugdraaien:** `packaging/udev/72-ravengun-dongle.rules`
blijft bestaan als los systeemhulpje — het geeft toegang tot USB-apparaten met de bekende
leveranciers-ID's en heeft niets met de loader te maken. De **detectie** (`dongle.py`) en de
**leverancierslijst** (`dongle-vendors.json`) verdwijnen, want alleen de poort gebruikte die. Als je
ook de udev-regel weg wilt, zeg het: dan gaat die mee.

## De volgorde van het werk

1. De drop-in-motor uitbreiden zodat hij **alles** kan wat de dongle-poort deed: bestanden plaatsen
   (ook als het doel een symlink naar de NAS is), `DllOverride`, `Env`, `Preload`, en dat **altijd**
   — niet alleen als er een dongle gekozen is.
2. `[Dongle]` en `dongle.json` uit de lezers halen (Python, C++, de app).
3. De poort, `DongleRefused` en exitcode 6 uit `launch-direct.py` halen.
4. De dongle-gegevens per game naar `dropin/<laag>/` verhuizen.
5. De app: het dongle-blok uit Properties en de statusregel eruit.
6. Tests en documentatie bijwerken.

**Let op:** stap 1 moet **eerst**, want anders valt er een gat: de poort doet nu het activeren, en de
drop-in-motor moet dat overnemen vóór de poort weg mag.

# RaccoonRampage

> **UPDATE 2026-10-21: the "guns are not calibrated" banner is gone on a fresh write layer.** Its cause was players 2..4
> (the Gun Test screen showed P1 Corrected, P2..P4 Uncorrected; the NAS file only has player 1). `seed/Setting_Hardware.ini`
> (+ `_Backups.ini`, made by `tools/dev/rr-calibration-seed.py`) calibrates all four players; see the NoteEN in `game.ini`.
>
> **STAND 2026-10-14, 01:30 — lees eerst `ATTRACT-RUN.md`.** De game draait zijn attract-lus op onze eigen
> kern (run rr10: 12 min 50 s, op de tv, onafhankelijk meegekeken), met de films en met zijn eigen bewaarde
> instellingen geladen. **Open punt, door de game zelf genoemd: de rode melding "The guns are not calibrated,
> please enter Setting Menu to calibrate!" op elk beeld**; er komt geen invoer aan: hij demonstreert, hij is
> op onze kern niet speelbaar. De standaard in `game.ini` blijft
> de gesloten kern, waarop de eigenaar deze game speelt. Wat er op onze kern ontbreekt staat in
> `ATTRACT-RUN.md` §3: de game praat met zijn kast via `RSGame/Plugins/UsbPluginsDll.dll` (107 `BIND_*`-functies)
> en leest `TeknoParrot_JvsState` niet zelf.
>
> Dit bestand kwam uit `patches/readme-gen.py`; de generatorregel is weggehaald omdat de alinea over het
> starten zonder TeknoParrot niet meer klopte (hieronder herschreven). De tabellen zijn van 2026-10-12.

Instellingen: `game.ini`, patches: `patches.ini`.
Bevindingen en meetgeschiedenis: `findings/RaccoonRampage.md` (op de NAS naast de games, niet in deze repo).

## In het kort

| | |
|---|---|
| lader | TeknoParrot |
| profiel | `WartranTroopers` |
| onze eigen kern herkent hem | — niet in de tabel — |
| dongle | regio/PCB: nee |
| patches.ini | nee |
| drop-in | geen `dropin/` |
| `[Status]` | playable |
| start met de lader | START |
| start zonder enige lader | **START** |

## De dongle

Geen `dongle.json` in deze map. Oordeel over een regio/printplaat-dongle in `patches/REGIO-PER-GAME.md`: nee.

Wat de game dan wél vraagt: niets.

## De patches

* **Nodig:** `eigen kern`
* **Is dat er:** **nee**
* **Bewijs:** alleen `game.ini` + `README.md`

## Starten

* Met de eigen lader: START
* Zonder enige lader (alleen `wine <exe>`): **START**

**Wat een start zonder TeknoParrot blokkeert:** niets — de game start en draait zijn attract-lus op onze eigen kern (gemeten 2026-10-14). Wat dan ontbreekt is de invoer en de kalibratiestatus: de melding *"guns are not calibrated"* staat op onze kern op elk beeld, ook met de bewaarde kalibratie van speler 1 geladen; op de gesloten kern staat hij er niet (`ATTRACT-RUN.md` §3).

## Waar de metingen staan

| Kaart | Wat je erin vindt |
|---|---|
| `patches/STAND.md` | volledigheid van deze map (`game.ini`, `[Status]`, drop-in, dongle) |
| `patches/STARTTEST.md` | of deze game met de eigen lader start en blijft draaien |
| `patches/ZONDER-LADER.md` | of hij ook met alleen `wine <exe>` start |
| `patches/PATCHES-PER-GAME.md` | welke patch nodig is en wat een start blokkeert |
| `patches/REGIO-PER-GAME.md` | of hij een regio/printplaat-dongle vraagt |
| `patches/EIGEN-KERN.md` | of onze eigen kern hem herkent, en welk profiel er nog moet komen |
| `patches/INVOER-ROUTE.md` | wie de invoer levert: de brug van de daemon, of een kern |

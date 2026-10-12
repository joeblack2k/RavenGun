<!-- Gegenereerd door patches/readme-gen.py uit de kaarten in deze map; bewerk de kaart of verwijder deze regel. -->
# Otomedius

Instellingen: `game.ini`, patches: `patches.ini`.
Bevindingen en meetgeschiedenis: `findings/Otomedius.md` (op de NAS naast de games, niet in deze repo).

## In het kort

| | |
|---|---|
| lader | TeknoParrot |
| profiel | `WartranTroopers` |
| onze eigen kern herkent hem | — niet in de tabel — |
| dongle | regio/PCB: nee |
| patches.ini | nee |
| drop-in | geen `dropin/` |
| `[Status]` | parked |
| start met de lader | START |
| start zonder enige lader | viel uit |

## De dongle

Geen `dongle.json` in deze map. Oordeel over een regio/printplaat-dongle in `patches/REGIO-PER-GAME.md`: nee.

Wat de game dan wél vraagt: niets.

## De patches

* **Nodig:** `eigen kern` (+ een ACIO-responder)
* **Is dat er:** **nee**
* **Bewijs:** alleen `game.ini` + `README.md`

## Starten

* Met de eigen lader: START
* Zonder enige lader (alleen `wine <exe>`): viel uit

**Wat een start zonder TeknoParrot blokkeert:** Wit venster; de game zoekt een **ACIO-micom op COM2** (`COM Port 2 Open Error`) en komt niet verder. `libacio.dll`/`HIdGet.exe` zijn I/O, geen dongle.

## Waar de metingen staan

| Kaart | Wat je erin vindt |
|---|---|
| `patches/STAND.md` | volledigheid van deze map (`game.ini`, `[Status]`, drop-in, dongle) |
| `patches/STARTTEST.md` | of deze game met de eigen lader start en blijft draaien |
| `patches/ZONDER-LADER.md` | of hij ook met alleen `wine <exe>` start |
| `patches/PATCHES-PER-GAME.md` | welke patch nodig is en wat een start blokkeert |
| `patches/REGIO-PER-GAME.md` | of hij een regio/printplaat-dongle vraagt |
| `patches/EIGEN-KERN.md` | of onze eigen kern hem herkent, en welk profiel er nog moet komen |

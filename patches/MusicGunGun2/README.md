<!-- Gegenereerd door patches/readme-gen.py uit de kaarten in deze map; bewerk de kaart of verwijder deze regel. -->
# MusicGunGun2

Instellingen: `game.ini`, patches: `patches.ini`.
Bevindingen en meetgeschiedenis: `findings/MusicGunGun2.md` (op de NAS naast de games, niet in deze repo).

## In het kort

| | |
|---|---|
| lader | OpenParrot |
| profiel | `MusicGunGun2` |
| onze eigen kern herkent hem | **MusicGunGun2** (tabel 2, 32-bits) |
| dongle | regio/PCB: nee |
| patches.ini | ja |
| drop-in | geen `dropin/` |
| `[Status]` | parked |
| start met de lader | START |
| start zonder enige lader | **START** |

## De dongle

Geen `dongle.json` in deze map. Oordeel over een regio/printplaat-dongle in `patches/REGIO-PER-GAME.md`: nee.

Wat de game dan wél vraagt: niets.

## De patches

* **Nodig:** `eigen kern` + `patches.ini`
* **Is dat er:** ja, **onvolledig**
* **Bewijs:** `patches.ini`: 2 secties. 1× `Kind = memory`, `AppliedBy = core`, `Enabled = false`: `Game.exe+0x137C70` `jmp-returns-true` `Size = 6`, `Category = loader`. verify-all: **0 aan / 1 uit / 1 geweigerd**

## Starten

* Met de eigen lader: START
* Zonder enige lader (alleen `wine <exe>`): **START**

**Wat een start zonder TeknoParrot blokkeert:** De game toont zijn **eigen D3D-foutvenster** "Direct3D-initialisatie mislukt" (ook met DXVK) en opent de JVS-pipe niet.

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

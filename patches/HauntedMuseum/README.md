<!-- Gegenereerd door patches/readme-gen.py uit de kaarten in deze map; bewerk de kaart of verwijder deze regel. -->
# HauntedMuseum

Instellingen: `game.ini`, patches: `patches.ini`.
Bevindingen en meetgeschiedenis: `findings/HauntedMuseum.md` (op de NAS naast de games, niet in deze repo).

## In het kort

| | |
|---|---|
| lader | OpenParrot |
| profiel | `HauntedMuseum` |
| onze eigen kern herkent hem | **HauntedMuseum** (tabel 2, 32-bits) |
| dongle | regio/PCB: nee |
| patches.ini | ja |
| drop-in | geen `dropin/` |
| `[Status]` | parked |
| start met de lader | START |
| start zonder enige lader | **START** |

## De dongle

Geen `dongle.json` in deze map. Oordeel over een regio/printplaat-dongle in `patches/REGIO-PER-GAME.md`: nee.

Wat de game dan wél vraagt: een **bestandswaarde**: de REGION-regel in zijn eigen `MUSEUM.ini`. Geen dongle.

## De patches

* **Nodig:** `eigen kern` + `patches.ini`
* **Is dat er:** ja, **onvolledig**
* **Bewijs:** `patches.ini`: 4 secties. 3× `Kind = memory`, `AppliedBy = core`, `Enabled = false`: `Game.exe+0x9E410` en `+0x9E3B0` `jmp-returns-true` `Size = 6`, `+0x9F778` `nop` `Size = 4`. Plus de gedeelde lader-patch. verify-all: **0 aan / 3 uit / 1 geweigerd**

## Starten

* Met de eigen lader: START
* Zonder enige lader (alleen `wine <exe>`): **START**

**Wat een start zonder TeknoParrot blokkeert:** Zwart beeld na de JVS-functiecontrole (zelfde als GaiaAttack4).

## Waar de metingen staan

| Kaart | Wat je erin vindt |
|---|---|
| `patches/STAND.md` | volledigheid van deze map (`game.ini`, `[Status]`, drop-in, dongle) |
| `patches/STARTTEST.md` | of deze game met de eigen lader start en blijft draaien |
| `patches/ZONDER-LADER.md` | of hij ook met alleen `wine <exe>` start |
| `patches/PATCHES-PER-GAME.md` | welke patch nodig is en wat een start blokkeert |
| `patches/REGIO-PER-GAME.md` | of hij een regio/printplaat-dongle vraagt |
| `patches/EIGEN-KERN.md` | of onze eigen kern hem herkent, en welk profiel er nog moet komen |

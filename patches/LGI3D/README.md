<!-- Gegenereerd door patches/readme-gen.py uit de kaarten in deze map; bewerk de kaart of verwijder deze regel. -->
# LGI3D

Instellingen: `game.ini`, patches: `patches.ini`.
Bevindingen en meetgeschiedenis: `findings/LGI3D.md` (op de NAS naast de games, niet in deze repo).

## In het kort

| | |
|---|---|
| lader | TeknoParrot |
| profiel | `SegaJvsLetsGoIsland` |
| onze eigen kern herkent hem | — niet in de tabel — |
| dongle | `region` |
| patches.ini | nee |
| drop-in | 1 / 1 |
| `[Status]` | playable |
| start met de lader | START |
| start zonder enige lader | **START** |

## De dongle

`dropin/ravengun-hasp/LGI3D/dongle.json` beschrijft een **region**-dongle.

| veld | waarde |
|---|---|
| `kind` | `region` |
| `game` | `LGI3D` |
| `title` | `Let's Go Island 3D: Lost on the Island of Tropics` |
| `platform` | `Sega RingWide` |
| `profile` | `LGI3D` |
| `emulation_profile` | `SegaJvsLetsGoIsland` |
| `exe` | `LGI.exe` |
| `region` | field = DongleRegion, name = USA, game_name = USA |
| `pcb` | field = PcbRegion, name = USA, game_name = USA |
| `io_board` | `837-14572` |
| `fallback` | `DefaultRegion` |
| `enforced` | nee |

**leesroutes die de game zelf gebruikt:** `amDongleGetRegion`, `readRegionEeprom`, `readRegionBuSram`

**API die de dongle moet beantwoorden:** `amDongleOpen`, `amDongleSetupKeychip`, `amDongleGetRegion`, `amDongleGetModelType`, `amDongleGetPlatformId`, `amDongleGetGameId`, `amDongleGetSystemFlag`, `amDongleBillingGetKeychipId`

Uit de beschrijving zelf:

> Zelfde bouwsel als LGI (fwAM), maar de USA-versie van het spel: het profiel staat op USA/USA en de game is speelbaar met die waarden. enforced = false om dezelfde reden als bij LGI.

## De patches

* **Nodig:** `drop-in` + `eigen kern`
* **Is dat er:** drop-in **ja (1/1)**, kern **nee**
* **Bewijs:** `dongle.json` (region, USA/USA, `enforced: false`)

## Starten

* Met de eigen lader: START
* Zonder enige lader (alleen `wine <exe>`): **START**

**Wat een start zonder TeknoParrot blokkeert:** Als LGI.

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

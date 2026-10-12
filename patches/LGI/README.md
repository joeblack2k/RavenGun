<!-- Gegenereerd door patches/readme-gen.py uit de kaarten in deze map; bewerk de kaart of verwijder deze regel. -->
# LGI

Instellingen: `game.ini`, patches: `patches.ini`.
Bevindingen en meetgeschiedenis: `findings/LGI.md` (op de NAS naast de games, niet in deze repo).

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

`dropin/ravengun-hasp/LGI/dongle.json` beschrijft een **region**-dongle.

| veld | waarde |
|---|---|
| `kind` | `region` |
| `game` | `LGI` |
| `title` | `Let's Go Island: Lost on the Island of Tropics` |
| `platform` | `Sega RingWide` |
| `profile` | `LGI` |
| `emulation_profile` | `SegaJvsLetsGoIsland` |
| `exe` | `LGI_RingW_F_safe.exe` |
| `region` | field = DongleRegion, name = JAPAN, game_name = JPN |
| `pcb` | field = PcbRegion, name = JAPAN, game_name = JPN |
| `io_board` | `837-14572` |
| `fallback` | `DefaultRegion` |
| `enforced` | nee |

**leesroutes die de game zelf gebruikt:** `amDongleGetRegion`, `readRegionEeprom`, `readRegionBuSram`

**API die de dongle moet beantwoorden:** `amDongleOpen`, `amDongleSetupKeychip`, `amDongleGetRegion`, `amDongleGetModelType`, `amDongleGetPlatformId`, `amDongleGetGameId`, `amDongleGetSystemFlag`, `amDongleBillingGetKeychipId`

Uit de beschrijving zelf:

> De game vraagt de regio aan de dongle/keychip en leest hem daarnaast uit de EEPROM en het backup-SRAM; alle drie de leesroutes falen zonder hardware en de game gaat verder met zijn eigen DefaultRegion. De waarden hierboven zijn wat een dongle moet antwoorden, niet wat de game vandaag nodig heeft: enforced = false.

## De patches

* **Nodig:** `drop-in` + `eigen kern`
* **Is dat er:** drop-in **ja (1/1)**, kern **nee**
* **Bewijs:** `dropin.ini` met 1 sectie: `ravengun-hasp/LGI/dongle.json` (region, `DongleRegion = JAPAN`, `PcbRegion = JAPAN`, **`enforced: false`**). `region.py`: *"de waarde is vastgelegd maar niet afgedwongen: de game valt terug op zijn eigen DefaultRegion en start door"*

## Starten

* Met de eigen lader: START
* Zonder enige lader (alleen `wine <exe>`): **START**

**Wat een start zonder TeknoParrot blokkeert:** De gesloten kern — maar de game **speelt al** via de directe start. De regio blokkeert niet.

## Waar de metingen staan

| Kaart | Wat je erin vindt |
|---|---|
| `patches/STAND.md` | volledigheid van deze map (`game.ini`, `[Status]`, drop-in, dongle) |
| `patches/STARTTEST.md` | of deze game met de eigen lader start en blijft draaien |
| `patches/ZONDER-LADER.md` | of hij ook met alleen `wine <exe>` start |
| `patches/PATCHES-PER-GAME.md` | welke patch nodig is en wat een start blokkeert |
| `patches/REGIO-PER-GAME.md` | of hij een regio/printplaat-dongle vraagt |
| `patches/EIGEN-KERN.md` | of onze eigen kern hem herkent, en welk profiel er nog moet komen |

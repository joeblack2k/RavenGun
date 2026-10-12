<!-- Gegenereerd door patches/readme-gen.py uit de kaarten in deze map; bewerk de kaart of verwijder deze regel. -->
# LGS

Instellingen: `game.ini`, patches: `patches.ini`.
Bevindingen en meetgeschiedenis: `findings/LGS.md` (op de NAS naast de games, niet in deze repo).

## In het kort

| | |
|---|---|
| lader | TeknoParrot |
| profiel | `LGS` |
| onze eigen kern herkent hem | — niet in de tabel — |
| dongle | `region` |
| patches.ini | nee |
| drop-in | 1 / 1 |
| `[Status]` | playable |
| start met de lader | START |
| start zonder enige lader | **START** |

## De dongle

`dropin/ravengun-hasp/LGS/dongle.json` beschrijft een **region**-dongle.

| veld | waarde |
|---|---|
| `kind` | `region` |
| `game` | `LGS` |
| `title` | `Let's Go Safari` |
| `platform` | `Sega RingWide` |
| `profile` | `LGS` |
| `emulation_profile` | `LGS` |
| `exe` | `GameSampR_RingWide.exe` |
| `region` | field = DongleRegion, name = USA, game_name = USA |
| `pcb` | field = PcbRegion, name = USA, game_name = USA |
| `io_board` | `837-14572` |
| `fallback` | `geen (de game gaat verder zonder dongle-antwoord)` |
| `enforced` | nee |

**leesroutes die de game zelf gebruikt:** `amDongleGetRegion`

**API die de dongle moet beantwoorden:** `amDongleOpen`, `amDongleOpenEx`, `amDongleSetupKeychip`, `amDongleSendAndRecvEx`, `amDongleGetRegion`, `amDongleGetModelType`, `amDongleGetPlatformId`, `amDongleGetGameId`, `amDongleGetVersion`, `amDongleGetPicVersion`, `amDongleBillingGetKeychipId`

Uit de beschrijving zelf:

> LGS gebruikt de amDongle-bibliotheek rechtstreeks (niet de fwAM-laag van LGI/LGI3D) en leest de regio met amDongleGetRegion uit het keychip-veld keychip.appboot.region. De game is speelbaar met USA/USA; de regiowaarde wordt vandaag nergens beantwoord en de game start ook zonder. enforced = false.

## De patches

* **Nodig:** `drop-in` + `eigen kern`
* **Is dat er:** drop-in **ja (1/1)**, kern **nee**
* **Bewijs:** `dongle.json` (region, USA/USA, `enforced: false`; leest de regio met `amDongleGetRegion` uit `keychip.appboot.region`)

## Starten

* Met de eigen lader: START
* Zonder enige lader (alleen `wine <exe>`): **START**

**Wat een start zonder TeknoParrot blokkeert:** De gesloten kern en de JVS-dienst op poort 40100 (die met `TP_REMOTETHREAD=off` verdwijnt).

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

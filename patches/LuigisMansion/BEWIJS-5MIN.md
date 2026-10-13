# Bewijs: Luigi's Mansion haalt de 5-minutenlat op ONZE eigen kern (op de tv gezien)

Dit is het bewijsdossier dat de eigenaar vroeg, zodat de meting over een maand na te kijken is
**zonder iets opnieuw te draaien**. Alles hieronder komt uit één run (r8) die de eigenaar van begin
tot eind op de tv heeft gezien en zelf heeft bevestigd (2026-10-13).

## De run

| | |
|---|---|
| Datum | 2026-10-13 |
| Kern | **ONZE eigen OpenParrot-kern**, sha256 `fe2ecf8c2c8844bd7abb3af616271a36364028453c2885fc92d61c9ffd155794` (de GESLOTEN TeknoParrot-kern is NIET gebruikt) |
| Drop-in | `dropin/rg-pe-hook.dll` sha256 `93738bf536ba863d1d6cd48e2f374df29c5039e6813498f7e790e67bae91be5b`; `dropin/dongle-haken.ini` sha256 `9d1cc1aae0ee797f0aac467295eb58b22719f1bae5df3743e47f347d9c7e8286` |
| Beeld | **beeldvullend op de tv** (HDMI-A-1, 1920x1080), via geneste gamescope (`TP_GAMESCOPE=1`), NIET headless. Scherm genomen via `scripts/rg-tv-env.sh` (statusbord weggehaald voor de duur van de run). |
| Route | `scripts/launch-direct.py LuigisMansion --run` met onze kern (kern-wissel via `patches/LuigisMansion/b0-run.sh b`), drop-in via de gebruikerslaag. Tijdslimiet 420 s (7 min). |
| Einde | het spel leefde nog toen de limiet van 420 s het afbrak (exit 124). |
| Ruwe uitvoer | `/home/deck/.cache/ravengun-luigi/blok1/r8/` (logboeken, console, hook-log). De schermafdrukken staan óók duurzaam hiernaast in `bewijs-5min/`. |

## Het venster dat telt (attract)

- **Laadscherm** (telt NIET mee): 17:42:27 en 17:42:42 (`shot-02`, `shot-03`).
- **Eerste schone attract-beeld:** 17:42:57 (`shot-04`) — "Please Insert Coin(s)", geen foutmelding.
- **Laatste attract-beeld vóór de afbreking:** 17:49:06 (`shot-28`) — titelscherm.
- **Duur aaneengesloten attract/demo:** **17:42:57 → 17:49:06 = 6 min 09 s** (de eigenaar rekende
  17:42:58 → 17:49:06 = 6 min 08 s). Ruim boven de lat van 5 minuten, met meer dan een minuut over,
  en het spel leefde nog bij de afbreking.
- **Geen `ERROR 0949` / "Keychip Not Found"** in enig beeld van dit venster.
- Eén bijna-zwart overgangsbeeld midden in de attract (`shot-19`, 17:46:48) toont nog wel de
  credit-balk van het spel (het is een overgang in het spel zelf, geen uitval). Eén opname ontbreekt
  (`shot-26`, spectacle leverde geen bestand); het venster is daarmee niet onderbroken (`shot-25` om
  17:48:20 en `shot-27` om 17:48:50 staan er wel).

## Wat er op het scherm stond (schermafdrukken in `bewijs-5min/`)

| # | tijd | inhoud |
|---|---|---|
| 01 | 17:42:11 | bureaublad/statusbord — spel nog niet op het scherm |
| 02 | 17:42:27 | "Loading" (laadscherm, telt niet) |
| 03 | 17:42:42 | "Loading" (laadscherm, telt niet) |
| 04 | 17:42:57 | **eerste attract** — Luigi in de gang, "Please Insert Coin(s)", CREDIT(S) 0 |
| 05 | 17:43:13 | hal met gameplay-clip |
| 06 | 17:43:28 | tutorial "Aim > Strobulb > Aim > Vacuum" |
| 07 | 17:43:43 | "Tactics", "Charge The Strobulb For A Stronger Flash" |
| 08 | 17:43:59 | "Power Surge" |
| 09 | 17:44:14 | "Strobomb" |
| 10 | 17:44:29 | "Continue … Power Boost 15 Sec" |
| 11 | 17:44:45 | "SHLOOP"-effect over een gameplay-clip |
| 12 | 17:45:00 | **titelscherm** "Luigi's Mansion ARCADE", Start: 1 CREDIT(S), Continue: 1 CREDIT(S) |
| 13 | 17:45:16 | titelscherm |
| 14 | 17:45:31 | titelscherm, Luigi loopt uit beeld |
| 15 | 17:45:46 | **Demo Mode** gameplay in de arcadelijst |
| 16 | 17:46:02 | **Demo Mode** gameplay, "COMBO BONUS!" |
| 17 | 17:46:17 | tutorial "[Pull Technique]" |
| 18 | 17:46:32 | "Collect The Gold Bones For A Surprise!" |
| 19 | 17:46:48 | bijna-zwart overgangsbeeld (toont nog de credit-balk) |
| 20 | 17:47:03 | nacht-huisscène met drie gameplay-clips |
| 21 | 17:47:18 | Luigi met Poltergust en een geest (promo-art) |
| 22 | 17:47:34 | zolder-tutorial, "Press The Trigger And Suck!" |
| 23 | 17:47:49 | "Tactics", "You Can Destroy Other Pests" |
| 24 | 17:48:04 | "Power Surge", "This Deals Huge Damage!" (ongeveer het 5-minutenpunt) |
| 25 | 17:48:20 | "Continue"-art met geesten |
| 27 | 17:48:50 | titelscherm |
| 28 | 17:49:06 | **laatste attract-beeld** — titelscherm |
| after-run | 17:49:14 | bureaublad (spel weg, statusbord ligt niet over het scherm) |

(`shot-26` ontbreekt — zie hierboven.)

## Hoe het werkt (kort; het volledige verhaal staat elders)

Per-game drop-in `dropin/rg-pe-hook.dll` (generieke PE-inline-hook) plaatst zich op het moment dat de
pakker `.text` verzegelt en doet dan drie dingen: (1) vangt de EIGEN `ExitProcess` van het spel in zijn
gevirtualiseerde WinMain (terugkeeradres `VACUUM.exe+0xf1374d`) en KEERT TERUG i.p.v. te beëindigen;
(2) schrijft het datapad op RVA `0x3d0c20` (stond leeg → crash bij de eerste shader); (3) vangt de 17
`am`-keychip-functies af, waarbij `amDongleIsAvailableInternal` **1** teruggeeft (anders `ERROR 0949`).
Zie `game.ini` `[Status]`, `B0-VIJFMIN.md` (bovenaan) en `docs/research/luigismansion-exit-trace.md`.

## WAT NOG NIET IS AANGETOOND (open punt, geen voetnoot)

**Er is niets gespeeld met het echte lichtpistool.** "Het speelt" betekent vandaag **"het draait en
demonstreert"**, NIET "het is speelbaar". Concreet is NIET aangetoond: munt inwerpen → richten →
schieten in het spel. De invoerroute staat wel klaar (`[Input] Route = tp-shm`, `Layout = LuigisMansion`;
de keten Move → gedeeld geheugen is eerder los gemeten), maar is in dít spel nog niet in-game bediend,
niet met de Move en niet met een muis. Dat is de volgende stap voor Luigi als echte speelbaarheid
gevraagd wordt.

# RUN 2 voor Halo — de uitkomst

*RavenGun, 2026-10-13. Uitgevoerd van **10:04:20** tot **10:09:40** (300 s, headless, exitcode 143 =
de timer). De bench was vrij bij de voorcontrole om 10:03:59: nul processen van welke partij dan ook
(ook geen `wineserver`), dus er is niemand onderbroken.*

## De uitkomst: (iii) — en dat is nu een harde uitkomst in plaats van een vermoeden

**De tap was aantoonbaar geladen en deed aantoonbaar zijn werk, maar er is geen enkele USB-activiteit.**
Het logboek heeft **15 × `tap start`** en verder **niets**:

```
87339.209022 tap start usbfs=/dev/bus/usb/ sysfs=/sys/bus/usb/devices/ max=50000
87370.243312 tap start usbfs=/dev/bus/usb/ sysfs=/sys/bus/usb/devices/ max=50000
...
87572.932739 tap start usbfs=/dev/bus/usb/ sysfs=/sys/bus/usb/devices/ max=50000
```

| geteld in het logboek | aantal |
|---|---|
| `tap start` | **15** |
| `attr idVendor` / `idProduct` / `busnum` | **0** |
| `APPARAAT`-regels | **0** |
| `descriptors`-openingen | **0** |
| `DIT IS HET FTDI-BORD` | **0** |
| `ctrl`- of `bulk`-opdrachten | **0** |

Dus: de aangescherpte tap (die een opsomming nu WEL ziet, bewezen op een nagebootste boom) zag in
300 s **geen enkele sysfs-attribuutopening en geen enkele descriptorlezing**. De game is nooit bij
`ftdi_usb_find_all()` aangekomen. Uitkomst **(iii)** uit het draaiboek, en met het langere venster
erbij is "het venster is te kort" nu uitgesloten: 300 s verandert niets.

## De levenscheck: deze keer betrouwbaar, en de game LEEDE de hele tijd

```
t=20s   game-pid=780629 threads=17 fmod-mappings=10  gamelog=74 regels  taplog=1 regels
t=40s   game-pid=780629 threads=15 fmod-mappings=10  gamelog=75 regels  taplog=2 regels
...
t=300s  game-pid=780629 threads=15 fmod-mappings=10  gamelog=75 regels  taplog=15 regels
```

Dat is de **echte game**: 17 threads en **10 libfmod-mappings** (die heeft een wrapper niet), en
dezelfde pid van t=20 s tot t=300 s. De autoriteit, het eigen logboek van de game, bevestigt het.
De methode uit §3 van het draaiboek werkte dus; de fout van run 1 (`pgrep -f` op een pad, waardoor ik
een wrapper met 1 thread en 5 fds voor de game aanzag) is hiermee rechtgezet.

## De FileTest: GEVAL B, en het is dezelfde mislukking op dezelfde binary

De nieuwe regels in `gamelog.txt` (de autoriteit), met tijdstempel:

```
Tue Oct 13 10:04:31 2026: -------------------------------------------------------------------------------
Tue Oct 13 10:04:31 2026: Welcome to Halo v03.11
Tue Oct 13 10:04:31 2026: Game Rev: 57529, Library Rev: 1412, Country Code: 48
FileTest Failure: '/pm/g7/halo/game'
```

**De eerste nieuwe regel is 10:04:31, elf seconden na de start van de run (10:04:20)** -- en dat is
dezelfde seconde als `Welcome`, precies zoals in run 1 (daar 08:57:21). Alle vier regels komen binnen
die ene seconde, en daarna komt er **niets meer**: het logboek blijft van t=40 s tot t=300 s op 75
regels staan.

Volgens §4b is dit **geval B**: dezelfde mislukking, even vroeg (binnen dezelfde seconde als het
Welkom), op **dezelfde binary** `/pm/g7/halo/game`. Met de tijdstempel als beslisser, niet met een
indruk. Mijn vier conclusies bij geval B, toegepast:

1. **Mijn extra bestand in de game-map was onschuldig.** Er staat nu geen logboek meer in de wortel
   (gecontroleerd: de bovenlaag is `.cache` + `pmuser`), en de FileTest mislukt identiek. De
   verklaring "mijn logbestand veroorzaakte het" gaat van tafel -- dat was de weerleggingsvoorwaarde
   die ik vooraf had opgeschreven, en die is hiermee vervuld.
2. **Het falen is inherent aan het vervangen beeld.** De manifest-controle vergelijkt `game` met een
   verwachte inhoud, en het opgevulde 32 MiB-beeld (origineel 31.270.111 B) komt daar niet mee
   overeen. In de drop-in of het recept valt hier niets te repareren.
3. **De route naar het FTDI-pad loopt via START, niet via de tap.** Het draaiboek en de
   statusnotitie zeggen het al: *"FileTest van de game zelf; START slaat hem over"*. Met een mislukte
   FileTest komt de game niet verder, hoe lang je ook wacht.
4. **En punt 4 van geval B, met zoveel woorden: de FTDI-attribuutregels zijn er NIET terwijl de
   FileTest faalt.** Dat betekent dat het FTDI-pad **niet** vóór de FileTest komt. De FileTest is dus
   **geen zijspoor maar een voorwaarde**: de opsomming van de apparaatlijst gebeurt pas nadat de game
   zijn eigen controle is gepasseerd. Zonder START is er geen FTDI-pad om te meten.

## Een correctie op mijn eigen verslag van run 1

In run 1 zag ik 8 × `tap start` in 90 s en schreef ik dat het spel zich "acht keer herstartte" na een
mislukte FileTest. Dat was **fout**. Run 2 laat het patroon zien: **15 `tap start`s, maar één
`Welcome`** in het logboek, en één spelproces dat de hele tijd bleef leven. Die `tap start`-regels
komen dus van processen die de preload erven -- `gamescope` en zijn helpers -- niet van herstarts van
de game. Ik had dat in run 1 niet geteld en er een conclusie aan gehangen; hierbij rechtgezet. Het
herstartgedrag dat ik in het draaiboek en in de §4b-conclusies noemde, bestaat in deze metingen niet.

## De tweede observatie die nieuw is

De game **sterft niet** na de mislukte FileTest. Hij blijft leven (17 threads, fmod gemapt, dezelfde
pid) en doet 290 s lang niets meer, tot de timer hem stopt. De eerdere "sterft op 5,96 s" hoort bij de
route met **onze OpenParrot-kern**, niet bij deze native route. Voor de native route is het beeld dus:
starten, de eigen controle niet halen, en dan blijven wachten.

## Wat run 2 expliciet NIET oplevert

* **Geen uitspraak over de FTDI-poort zelf** -- baud, berichten, CRC, of er een bord gevonden wordt.
  De game is nooit bij zijn FTDI-code aangekomen, dus er is niets om mee te praten.
* **Geen uitspraak over de invoer** (START/munt): dat loopt via het RIO2-bord en de shim, niet via
  deze tap. Deze run heeft de shim niet gebruikt.
* **Geen uitspraak over de FileTest zelf als controle-mechanisme.** Vastgesteld is alleen: hij
  mislukt op `/pm/g7/halo/game`, even vroeg, in beide runs, met en zonder mijn bestand in de map. Of
  het een hash-, grootte- of tijdstempelcontrole is, is niet gemeten -- alleen dat de naam van de
  binary genoemd wordt en dat een extra bestand in de wortel er niet toe deed.
* **Geen uitspraak over aim/trekker/geluid**, en niets over de invoerketen.

## Wat de volgende stap dan is

Niet een langer venster (300 s heeft dat uitgesloten) en niet het wantrouwen van de tap (die bewees te
werken op een nagebootste boom en bleef hier stil omdat er niets te zien was). De volgende stap is de
**START-route**: de FileTest overslaan met de schakelaar uit het gedeelde geheugen
(`tools/rt-input-shim`, zoals in `packaging/native-games.json` beschreven), en dán met dezelfde tap
kijken of de apparaatlijst langskomt. Dat is precies het middel dat ik in het draaiboek als
"ingrijpender, tweede keuze" had aangemerkt -- en nu is het de eerste keuze, omdat de meting heeft
uitgewezen dat tijd alleen niet helpt.

## Bewaard

* `patches/Halo/usb-tap-run2.log` -- het logboek van de tap (15 regels, ruw).
* `patches/Halo/gamelog-run2.txt` -- het eigen logboek van de game na de run.
* `patches/Halo/RUN-2-DRAAIBOEK.md` -- de opzet, de vier uitkomsten en de §4b-conclusies, opgeschreven
  vóór de run.
* Opgeruimd: geen game, geen launcher, geen gamescope, geen tap-resten; de bovenlaag van de game-map
  is `.cache` + `pmuser`, zonder logboek.

# Halo: Fireteam Raven — draaiboek voor run 2 (met de aangescherpte tap)

*RavenGun, 2026-10-13. Vervangt run 1, waarvan de uitkomst (iii) was. Bench-vrij voorbereid: dit
document is de opzet, de verwachtingen vooraf en de methode. De run zelf kost 300 s bench.*

## 0. Waarom een run 2

Run 1 gaf uitkomst (iii): de tap was aantoonbaar geladen (8 × `tap start`) maar het spel kwam in 90 s
niet aan zijn FTDI-pad toe, dus over de poort is nog niets gezegd. Daarbij vond ik twee fouten in mijn
eigen opzet. Die zijn nu gerepareerd, en dat is de hele reden voor run 2.

## 1. De twee reparaties

### 1a. De tap ziet de opsomming nu WEL

libusb leest de apparaatlijst uit sysfs via de **attribuutbestanden** (`idVendor`, `idProduct`,
`busnum`, `devnum`) en niet via `descriptors`. De tap logde alleen `descriptors` en liet
attribuutopeningen met opzet ongemoeid -- waardoor een **geslaagde** opsomming zonder bord er precies
zo uitzag als een spel dat zijn FTDI-pad nooit bereikt: `tap start` en verder niets. Een lege log was
dus onuitlegbaar.

Nu wordt elk attribuut gelogd **met zijn waarde**, en elk gevonden apparaat één keer samengevat als
VID:PID, met een expliciete markering als het `0403:6001` is.

Bewezen zonder bord en zonder spel, met een nagemaakte sysfs-boom en een programma dat precies het
leespatroon van libusb doet (`patches/Halo/probe/enum_sim.c`, zie `tap_zelftest.sh`):

```
tap start usbfs=/dev/bus/usb/ sysfs=.../sys/bus/usb/devices/ max=20000
attr idVendor = 046d   (.../2-1/idVendor)
attr idProduct = c52b   (.../2-1/idProduct)
APPARAAT .../2-1 idVendor=046d idProduct=c52b
attr idVendor = 0403   (.../1-1/idVendor)
attr idProduct = 6001   (.../1-1/idProduct)
APPARAAT .../1-1 idVendor=0403 idProduct=6001   <<< DIT IS HET FTDI-BORD 0403:6001
open .../1-1/descriptors fd=5
read fd=5 len=14 data: 12 01 00 02 00 00 00 40 03 03 60 01 00 01
```

En de controle: hetzelfde programma **zonder** de tap levert nul regels in het logboek. De markering
is er met opzet, want die maakt uitkomst (iv) in één oogopslag leesbaar.

### 1b. Het logboek gaat BUITEN de game-map

Run 1 schreef `usb-tap.log` in de wortel van de game (`/pm/g7/halo/`) en de FileTest van de game
faalde op precies die wortel (`FileTest Failure: '/pm/g7/halo/game'`). Dat kan toeval zijn, maar een
manifest-achtige FileTest hoort een extra bestand niet te waarderen.

Nu gaat het logboek naar een **eigen hostmap die schrijfbaar in de sandbox wordt gehangen**, met
`--bind-rw` (dat bestaat in `play-native.sh`):

```bash
mkdir -p ~/.local/share/ravengun/tap
# in de sandbox:  --bind-rw ~/.local/share/ravengun/tap /rg/tap
# en:             --env RG_USB_LOG=/rg/tap/usb-tap.log
```

De game-map is gecontroleerd en schoon: de bovenlaag bevat precies `.cache` en `pmuser`, geen
`usb-tap.log`. Het logboek van run 1 is bewaard als `~/.local/share/ravengun/rg-run1-usb-tap.log`.

## 2. Het commando voor run 2

```bash
cd ~/projects/ravengun
scripts/play-native.sh --game Halo --headless --seconds 300 \
  --bind-ro ~/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so /rg/lib/libusb-tap.so \
  --bind-rw ~/.local/share/ravengun/tap /rg/tap \
  --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
  --env RG_USB_LOG=/rg/tap/usb-tap.log \
  --env RG_USB_RAW=1 --env RG_USB_MAX=50000
```

Daarna lezen: `~/.local/share/ravengun/tap/usb-tap.log` en het eigen logboek van de game
(`native/Halo/0/upper/pmuser/gamelog.txt`).

**Waarom 300 s en niet de FileTest overslaan met START.** Het minst ingrijpende middel eerst: een
langer venster verandert **niets** aan het gedrag van de game -- het laat alleen meer tijd. De
FileTest overslaan met START via de input-shim grijpt in de spelbesturing in en verandert de
toestand waarin we meten; dat doe je pas als blijkt dat tijd niet helpt. Bovendien is run 2 zo
meteen een **schone herhaling** van de vraag of mijn extra bestand in de game-map de FileTest
veroorzaakte: is de FileTest nu wél goed, dan was dat het, en niet de game.

## 3. De levenscheck: `gamelog.txt` is de autoriteit

In run 1 pakte mijn levenscheck (`pgrep -f '/pm/g7/halo/game'`) steeds dezelfde pid met **1 thread, 5
fds en 0 fmod-mappings** -- dat is een wrapper (`bwrap`/gamescope) met hetzelfde pad in zijn
commandoregel, niet de game. Een verkeerde levenscheck is erger dan geen, want hij lijkt een meting.

De methode voor run 2:

1. **De autoriteit is het eigen logboek van de game**, `native/Halo/0/upper/pmuser/gamelog.txt`. Dat
   schrijft de game zelf, dus het is het bewijs dat hij leefde en hoe ver hij kwam:
   * `Welcome to Halo v03.11` + `Game Rev: ...` = de game is gestart en draait;
   * `FileTest Failure: '/pm/g7/halo/game'` = de FileTest is niet goed gegaan (en dat is precies wat
     we dit keer willen weten);
   * de `cmgr`-regels (`cmgr_init`, `cmgr_thread_main: thread begun`) = hij is bij de laag waar het
     FTDI-pad zit.
   Vooraf een kopie van de laatste regels maken, na de run de nieuwe regels erbij: het bestand wordt
   alleen maar aangevuld, dus dat is een betrouwbare tijdlijn.
2. **Als tweede, onafhankelijke signaal**: `pgrep -x game` (exacte procesnaam, niet `-f`) en dan in
   `/proc/<pid>/maps` kijken of **libfmod gemapt** is. Dat kan alleen als de game voorbij zijn lader
   is; een wrapper heeft die mapping niet. Dit is de check die in run 1 ontbrak.
3. **Niet** meer `pgrep -f` op een pad: dat is de fout van run 1.

## 4. De vier uitkomsten, vooraf opgeschreven (met de aangescherpte tap)

| | wat je ziet | betekenis | geslaagde meting? |
|---|---|---|---|
| **i** | geen `tap start`-regel | de tap is **niet geladen**; de proef is mislukt, niet de game. Controleer of `--bind-ro` en `LD_PRELOAD` hetzelfde pad noemen (`/rg/lib/libusb-tap.so`) | **nee** |
| **ii** | `tap start` + `attr idVendor/idProduct/busnum`-regels en `APPARAAT`-regels, en **nergens** `0403:6001` | de verwachte **geslaagde** meting zonder bord: de game heeft de apparaatlijst opgesomd, geen `0403:6001` gezien, dus `ftdi_usb_find_all()` vindt nul kandidaten. Je houdt de lijst met VID/PID's over die de game heeft bekeken | **ja** |
| **iii** | `tap start` en **nog steeds niets** (geen attribuut- en geen descriptorregels) | de game is in 300 s nog niet aan zijn FTDI-pad toegekomen. Dan zijn het venster of de FileTest de volgende verdachte -- en dan is de START-route via de shim de volgende stap, niet het wantrouwen van de tap | **nee, wel informatief** |
| **iv** | regels met `03 04`/`01 60`, of `APPARAAT ... idVendor=0403 idProduct=6001 <<< DIT IS HET FTDI-BORD`, of `/dev/bus/usb`-regels | er **hangt** een bord (of iets dat zich als 0403:6001 voordoet). Dan is dit de meting met bord: logboek bewaren, en de `ctrl`-regels laten zien wat de game vraagt | **ja, en meer** |

Extra te noteren, wat de uitkomst ook is: de **levensduur** van het proces (uit `gamelog.txt` en de
procescontrole), of het entry point netjes teruggezet werd, en of de FileTest nu wél goed ging -- dat
laatste is de schone herhaling van de vraag uit §1b.

## 5. Vooraf, zonder bench

* De tap is gebouwd **en** zijn eigen zelftest is gedraaid (`tap_zelftest.sh`): `tap start`, de
  attribuutregels, twee `APPARAAT`-regels en de FTDI-markering. Zonder die zelftest is een lege log
  later niet te duiden.
* De game-map is schoon (`.cache` + `pmuser`), er staat geen logboek in.
* De NAS is niet aangeraakt; de sandbox houdt hem read-only.
* Er draait niets anders: de bench is één game tegelijk.

## 6. Wat run 2 expliciet **niet** oplevert

* Geen uitspraak over de FTDI-communicatie zelf (baud, berichten, CRC) als er geen bord hangt -- dan
  is er niets om mee te praten.
* Geen uitspraak over de invoer (START/munt): dat gaat via het RIO2-bord en de shim, niet via deze tap.
* Geen commit: het logboek en de uitkomst gaan naar de coördinator, die commit.

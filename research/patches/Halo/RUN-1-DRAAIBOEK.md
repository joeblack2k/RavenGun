# Halo: Fireteam Raven — draaiboek voor run 1 (zonder FTDI-bord)

*RavenGun, 2026-10-12. Dit is één bench-run, ongeveer 2 minuten. Er wordt niets aan de NAS
geschreven behalve wat het spel zelf al schrijft (dat landt in de overlay).*

**Waarom run 1 zonder bord zin heeft.** Het FTDI-pistoolbord is een eigenaarsvraag: zonder een
fysieke `0403:6001`-kaart is er geen bytespoor op de draad te vangen (op de Deck hangt nu geen
enkel `0403:*`-apparaat; `lsusb` toont alleen hubs, een ARES, een KB216, een Bluetooth-radio, een
AURA-LED-controller, de Steam Controller Puck en de **PlayStation Eye `1415:2000`** — dat is de
Move-camera). Maar run 1 is niet leeg: hij bewijst dat het meetinstrument **in het spelproces**
werkt, en hij laat zien **welke USB-apparaten de game heeft bekeken** — libusb leest op Linux de
apparaatlijst en de descriptors uit sysfs, dus ook zonder bord levert dat een spoor met VID/PID
per bekeken apparaat. Dat is precies het stuk dat je niet uit het bestand kunt halen.

---

## 0. Vooraf (kan nu al, kost geen bench)

```bash
# het meetinstrument bouwen en zijn eigen zelftest doen (start niets)
bash ~/projects/ravengun-patches/patches/Halo/probe/build_tap.sh
```
Verwacht: `gcc exitcode 0`, een `tap start ...`-regel, en in de synthetische proef een
`read fd=… len=18 data: 12 01 00 02 00 00 00 40 03 04 01 60 …` (de descriptor met VID `0403`
en PID `6001`). En:

```bash
cd ~/projects/ravengun && scripts/play-native.sh --game Halo --dry-run
```
Verwacht: **exitcode 0**, met in het afgedrukte `bwrap`-commando onder meer
`--setenv LD_PRELOAD /rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so`.

Verder: de bench is één game tegelijk. Draait er iets van de eigenaar, wacht dan. `--headless`
zet de game in een gamescope zonder beeldscherm, dus de tv blijft vrij.

---

## 1. Het commando

```bash
cd ~/projects/ravengun
scripts/play-native.sh --game Halo --headless --seconds 90 \
  --bind-ro ~/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so /rg/lib/libusb-tap.so \
  --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
  --env RG_USB_LOG=/tmp/usb-tap.log \
  --env RG_USB_RAW=1 \
  --env RG_USB_MAX=50000
```

Toelichting per stuk:

* `--headless --seconds 90` — gamescope zonder beeldscherm, na 90 s netjes stoppen. Genoeg: het
  FileTest van de game duurt op de NAS ongeveer 25 minuten, maar de HASP- en FTDI-code loopt al
  vroeg (vóór het FileTest).
* `--bind-ro <bron> /rg/lib/libusb-tap.so` — de tap moet **in** de sandbox staan; buiten de
  sandbox bestaat `/rg/lib` niet.
* `--env LD_PRELOAD=…` — `play-native.sh` zet `LD_PRELOAD` uit het recept en past daarna de
  `--env`-regels toe, dus deze regel **vervangt** het recept. Daarom staan alle drie de
  bibliotheken er opnieuw in: `halo.so` (de dongle), `librg_rt_input.so` (de schakelaars) en de
  tap. Zonder die herhaling verlies je de eerste twee en verandert de proef in iets anders.
* `RG_USB_RAW=1` — volledige buffers in plaats van de eerste 64 bytes. Nodig zodra er een bord
  hangt; zonder bord maakt het niet uit.

**Alternatief als de `--env`-regel lastig is:** zet de tap tijdelijk achteraan de `preload`-regel
van het `Halo`-profiel in `packaging/native-games.json`
(`…/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so:/rg/lib/libusb-tap.so`) en voeg
`"RG_USB_LOG": "/tmp/usb-tap.log"` aan `env` toe. Daarna de regel weer terugzetten.

**Logboek ophalen:**
```bash
wc -l /tmp/usb-tap.log; head -40 /tmp/usb-tap.log
ls -t ~/.local/state/ravengun/logs/native-Halo-*.log | head -1 | xargs grep -aE 'cmgr|FTDI|RIO2|gun|Gun|webcam|Cal' | head -30
```

---

## 2. Wat je verwacht te zien (dit is de maatstaf)

Het logboek begint **altijd** met de startregel:

```
44592.609286 tap start usbfs=/dev/bus/usb/ sysfs=/sys/bus/usb/devices/ max=50000
```

Daarna, in deze run, de **opsomming** — libusb leest de apparaatlijst uit sysfs:

```
… open /sys/bus/usb/devices/1-1/descriptors fd=4
… read fd=4 len=18 data: 12 01 00 02 00 00 00 40 08 1a 02 01 10 00 01 02 03 01
… close fd=4 (/sys/bus/usb/devices/1-1/descriptors)
… open /sys/bus/usb/devices/3-3/descriptors fd=4
… read fd=4 len=18 data: 12 01 00 02 00 00 00 40 03 13 71 35 01 01 01 02 03 01
…
```

In die bytes staat de VID/PID little-endian op offset 8: `03 04` = `0x0403`, `01 60` = `0x6001`.
De apparaten die er nu hangen hebben andere paren (onder meer `1a 08`/`02 01`, `03 13`/`71 35`,
`de 28`/`04 13`).

En dan de **poort van het apparaatverkeer** — bij het FTDI-bord komt daar dit soort regels:

```
… open /dev/bus/usb/003/007 fd=5
… ctrl fd=5 bmRequestType=0x80 bRequest=0x06 wValue=0x0100 wIndex=0x0000 wLength=64
…   -> ret=64 errno=0
…   in fd=5 len=64 data: 12 01 00 02 00 00 00 40 03 04 01 60 …
… bulk fd=5 ep=0x81 len=64 timeout=5000
…   -> ret=64 errno=0
…   in fd=5 len=64 data: …
… urbsub fd=5 type=2 ep=0x02 flags=0x0 len=5
…   out fd=5 len=5 data: …
… urbreap fd=5 type=2 ep=0x81 status=0 actual=5
…   in fd=5 len=5 data: …
```

De `data:`-regels **zijn** de bytes op de draad; dat is het materiaal waarmee een vervanging later
mogelijk wordt.

---

## 3. De uitslag lezen — vier gevallen, en ze betekenen niet hetzelfde

| wat er in `/tmp/usb-tap.log` staat | wat het betekent | geslaagd? |
|---|---|---|
| **geen bestand, of geen `tap start`-regel** | De tap is **niet geladen**. Dit is een fout in de proef, niet in de game: `LD_PRELOAD` is niet aangekomen, of `ld.so` weigerde de tap (hij vraagt `GLIBC_2.38` via `__isoc23_strtol`; de Deck heeft dat, maar een andere machine misschien niet). Controleer of `bash probe/build_tap.sh` hierboven slaagde en of het pad in `--bind-ro` en in `LD_PRELOAD` hetzelfde is (`/rg/lib/libusb-tap.so`). | **nee** |
| **`tap start` + `open …/descriptors` + `read … len=18`-regels, en verder niets** | De tap werkt in het spelproces, de game heeft libusb's apparaatlijst opgesomd, en er is **geen** `0403:6001` bij — dus `ftdi_usb_find_all()` vindt nul kandidaten en de game meldt `Did not find exactly one FTDI chip (found: 0)`. **Dit is de verwachte, geslaagde uitkomst van run 1 zonder bord.** Je houdt er het bewijs aan over dat de keten (spelproces → libusb → sysfs) loopt, plus de lijst met VID/PID's die de game heeft bekeken. | **ja** |
| **`tap start`, maar geen enkele `descriptors`-regel** | De game is in die 90 s **niet aan zijn FTDI-pad toegekomen**. Dat is een echt resultaat: de code die het bord zoekt loopt later (of helemaal niet) in de opstart. Dan is de volgende stap het venster te verlengen (`--seconds 300`) of de FTDI-code een duwtje te geven — niet de tap wantrouwen. | **nee, maar wel informatief** |
| **`tap start` + `descriptors` met `03 04`/`01 60`, of `/dev/bus/usb/…`-regels** | Er **hangt** een `0403:6001`-apparaat aan de Deck. Dan is run 1 in de praktijk run 2: bewaar het logboek en ga naar §4. | **ja, en meer** |

**Wat een `open /dev/bus/usb/…` regel zonder bord zou betekenen:** dat libusb een apparaat heeft
geopend zonder dat er een `0403:6001` in de lijst stond — dus dat de game een ander apparaat
claimt. Dat zou een nieuw feit zijn; bewaar het logboek en meld het.

---

## 4. Als er wél een bord hangt (run 2)

Zelfde commando, geen wijziging. De succescriteria:

1. in de descriptor van het bord staat `03 04` op offset 8 en `01 60` op offset 10;
2. er komen `/dev/bus/usb/…`-regels met `ctrl`, `bulk` of `urbsub`/`urbreap`, en de `data:`-regels
   zijn volledig (`RG_USB_RAW=1`);
3. de game meldt een pistool in plaats van `found: 0` — in het spellogboek de regels
   `cmgr_thread_main: SPAWNING CONNECTION`, `cmgr_thread_conn_read: READ %d PACKETS` en
   `Gun %d Cal: (%f, %f)`;
4. het logboek is **letterlijk terug te spelen**: per regel tijdstip, richting, eindpunt en bytes.

Punt 4 is het eigenlijke doel. Zonder dat logboek is er geen vervanging te bouwen; met dat logboek
wordt de framing, de CRC en de bericht-ID's van `switch`/`led`/`status`/`trig` vastgelegd en kan de
volgende stap (een libftdi-shim in het spelproces, dezelfde techniek als `tools/rt-hasphook`) begin
en eind hebben.

---

## 5. Wat run 1 expliciet **niet** oplevert

* Geen protocol: zonder bord is er geen uitwisseling.
* Geen uitspraak over aim of trekker.
* Geen uitspraak over het RIO2-bord (dat is een ander pad: `/dev/tty` of Ethernet, niet de FTDI).
* Geen antwoord op de START-reproduceerbaarheid; die meet je apart, met de schakelaar-shim.

## 6. Vaste afspraken

* NAS alleen lezen; wat de game schrijft landt in `~/.local/share/ravengun/native/Halo/`.
* Eén game tegelijk op de bench; het scherm van de eigenaar blijft vrij (`--headless`).
* `halo.so` wordt niet gewijzigd of verwijderd.
* Geen commit: het logboek en de uitkomst gaan naar de coördinator, die commit.

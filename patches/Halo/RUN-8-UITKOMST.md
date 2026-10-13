# RUN 8 voor Halo — de trekker en de positie via het virtuele lichtgeweer

*RavenGun, 2026-10-13. Uitgevoerd **11:10:xx - 11:16:xx** (300 s, beeld op de tv, exitcode 0). Geen
crash (`FATAL-regels: 0`). Het lichtgeweer is na de run gedisarmeerd; niets draait nog. Geen commit.*

## 1. Welke launcher, en wat het gereedschap zei (zoals gevraagd)

| | |
|---|---|
| launcher | **`~/projects/ravengun/scripts/play-native.sh`** -- sha256 `0868ffd754b7e8a5` |
| | **niet** de geinstalleerde in `~/.local/opt/ravengun` |
| `launch-direct.py` | sha256 `dcf148c5bcc5f29f`, en `git status` zegt **`M`** (de werkboom-versie met de `--exe`-override van de laderbouwer) |
| de `--exe`-override | **niet gebruikt**; ik heb er niets mee gedaan en hem met rust gelaten |
| `ravengunctl` | `~/projects/ravengun/build/ravengunctl`, sha256 `645a062c04f8be5d` |

**`mice` VOOR de run:** speler 1 = `usb-Valve_Software_Steam_Controller_Puck_FXB9955202923-if02-event-mouse`
(verbonden, 1920 counts per schermbreedte); spelers 2-4: geen muis. (Die koppeling had ik in de
voorbereiding gezet met `mouse-gun --player 1 --mouse <id>`.)
**`status` VOOR:** daemon pid 499954; **Move: niet verbonden**; verbinding disconnected; IO-brug: geen
(invoer via de virtuele apparaten); knoppen: geen, trigger 0.

**`arm`:** `Uitvoer GEWAPEND; apparaat aangemaakt (1209:7202), event /dev/input/event13, abs 0..32767`.
Dat is de voorwaarde: zonder `arm` gaat er niets naar het virtuele lichtgeweer.

## 2. Wat er per stap gebeurde, met wat het lichtgeweer UITZOND

Mijn lezer op `/dev/input/event13` legde vast wat het apparaat werkelijk uitzond -- dat is de meting,
niet de opdracht die ik gaf:

| tijd | wat ik deed | wat het lichtgeweer uitzond | gamelog | scherm |
|---|---|---|---|---|
| 11:11:1x | START-puls 1 | (schakelaar, zie run 7) | 187 regels, `FileTest Reported` + webcam + netwerk | kalibratie |
| **11:11:30** | `test-pattern point 0 0` (linksboven) | **`ABS_X=0`, `ABS_Y=0`** | 187 | `run8-02-linksboven.png` |
| **11:11:34** | `output-button trigger tap` | **`BTN_LEFT=1` om .445, `=0` om .525** | 187 | `run8-03-na-trekker.png` |
| 11:11:39 | trekker 2 | `BTN_LEFT` 1/0 om .917/.997 | 187 | `run8-04-trekker2.png` |
| 11:11:46 | trekker 3 | `BTN_LEFT` 1/0 om .388/.468 | 187 | `run8-05-trekker3.png` |
| 11:11:52 | `point 0.5 0.5` + trekker | `BTN_LEFT` 1/0 om .860/.940 | 187 | -- |
| 11:11:58 | `point 1 1` + trekker | `BTN_LEFT` 1/0 om .945/.025 | 187 | `run8-06-eind.png` |

De lezer zag in totaal **5 trekker-indrukken** (10 `BTN_LEFT`-events: elke indruk een 1 en een 0) en
**positie-events van 0 tot 32767** in beide assen -- dus de loop ging werkelijk naar de uiterste
linkerbovenhoek, en de trekker ging werkelijk in.

## 3. WAT ER OP HET SCHERM GEBEURDE: niets

**Het logboek van de game bleef op 187 regels staan** vanaf 11:11:25 (`Network initialization
completed`) tot het eind van de run: er kwam geen enkele nieuwe regel bij. En de foto's zijn alle zes
~1.467 kB (binnen 0,06 % van elkaar; `run8-04` en `run8-06` zijn zelfs byte-identiek groot) -- dat is
**hetzelfde kalibratiescherm** als in run 5 en run 7:

> **Player 1 gun (left):** Pull gun all the way back and towards the left, then press the trigger. ...
> Press the TEST button to skip calibration for this gun.

Dus: de trekker en de positie zijn **aangekomen op het apparaat** (gemeten) en het scherm **verschoof
niet**. Daarmee is de vraag "lag het aan de trekker" beantwoord: **nee**.

## 4. De conclusie, en wat er nu nog over is

Wat nu **uitgesloten** is met metingen:
* **TEST** (run 7): k=3, k=4 (de juiste index), k=5, en zes herhalingen -- de tafel veranderde, het
  scherm niet;
* **de trekker** (run 8): vijf keer `BTN_LEFT` van het virtuele lichtgeweer -- het scherm niet;
* **de positie** (run 8): `ABS_X`/`ABS_Y` van 0 tot 32767, inclusief de uiterste linkerbovenhoek die het
  scherm vraagt -- het scherm niet.

Wat er dan overblijft, in volgorde van waarschijnlijkheid:
1. **De game ziet ons virtuele lichtgeweer misschien niet als GEWEER.** Het apparaat presenteert zich
   als een muis-achtig evdev-apparaat met `ABS_X`/`ABS_Y` en `BTN_LEFT` (by-id
   `...-event-mouse`), terwijl de game zijn geweer via **SDL-joysticks** zoekt (dat is eerder gemeten:
   *"de SDL-joystick wordt als gun gevonden"*). Dat de events de kernel halen betekent niet dat de game
   ze leest. **Dit is de eerstvolgende meting**: kijkt de game naar `/dev/input/event13` en ziet SDL/udev
   hem als joystick of als muis?
2. **De webcamstap is mislukt** (`Webcam: Could not ID device for player 0`, `All webcams reported
   brightness difference of 0`). Het kalibratiescherm vraagt om de loop naar een positie te trekken --
   dat is een **tracking**-handeling: de game wil het geweer in het camerabeeld ZIEN voordat hij iets
   aanneemt. Zonder camera is er niets te zien, en dan kan het scherm wachten tot er wel een camera is.
3. Iets anders in de kalibratielogica (bijvoorbeeld een volgorde: eerst speler 2, of eerst een
   camera-check die niet doorkomt).

**Wat deze run dus oplevert:** de trekker en de positie zijn nu uitgesloten als verklaring, en dat is
precies de vraag die de vorige run openliet. **Wat deze run niet oplevert:** geen bewijs dat de game de
events ook LEEST (we hebben het apparaat gemeten, niet de game-kant), geen attract- of spelmodus (de
lat is niet gehaald), niets over het FTDI-protocol (de tap zag in deze run niets), en niets over aim of
trekker **binnen de game** -- alleen dat ze op het apparaat aankomen.

## 5. De lat

**Gehaald = 5 minuten attract- of spelmodus.** Deze run blijft op het kalibratiescherm staan en haalt de
lat dus **niet**. Het is de verst mogelijke stand zonder camera en zonder game-zijdige geweerherkenning.

## 6. Bewaard

* `patches/Halo/run8-01-kalibratie.png` t/m `run8-06-eind.png` -- het scherm per stap.
* `patches/Halo/run8-lichtgeweer.txt` -- **wat het virtuele lichtgeweer uitzond** (de meting).
* `patches/Halo/gamelog-run8.txt`, `patches/Halo/usb-tap-run8.log`.
* `patches/Halo/probe/run8_uitvoeren.sh` -- met `arm`, de stappen en de lezer erin.

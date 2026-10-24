# Waarom de game ons geweer niet ziet: de classificatie (GEMETEN)

*RavenGun, 2026-10-13. Bench-vrij uitgezocht na run 8, waarin het virtuele lichtgeweer de trekker en de
posities uitzond en de game er niet naar keek. Geen commit.*

## 1. De meting die de vraag beslist

`udevadm info --query=property --name=/dev/input/event13` op ons virtuele lichtgeweer:

```
DEVNAME=/dev/input/event13
ID_INPUT=1
ID_INPUT_MOUSE=1          <- als MUIS
                          <- en GEEN ID_INPUT_JOYSTICK
```

en uit sysfs:

```
capabilities/abs  = 3      (bits 0 en 1: ABS_X | ABS_Y)
capabilities/key  = 1f0000 0 0 0 0   (de muisknoppen)
capabilities/rel  = 0
properties        = 0      (dus GEEN INPUT_PROP_DIRECT)
```

**Ons geweer is voor udev een muis en geen joystick.** En SDL (de kant waar de game zijn geweer zoekt --
*"de SDL-joystick wordt als gun gevonden"*) bepaalt zijn joysticklijst op Linux via precies die
eigenschap: `ID_INPUT_JOYSTICK`. Zonder die eigenschap komt het apparaat niet in de joysticklijst, en
dan kan de game het niet als geweer vinden -- hoe netjes de `ABS_X`/`ABS_Y` en `BTN_LEFT` ook
aankomen. Dat is de verklaring voor run 8: **het geweer deed alles goed en de game keek er niet naar,
omdat het voor de joystickkant van de game niet bestaat.**

## 2. En waarom het juist een muis IS -- dat is geen fout maar een keuze

Het project heeft dit bewust zo gedaan, en dat staat in de eigen documentatie:

* `docs/architecture.md` (uit de broncode van TeknoParrot): TeknoParrot somt de apparaten op uit
  `/proc/bus/input/devices` en neemt elk apparaat waarvan de regel `H: Handlers=` het woord **`mouse`**
  bevat. Er wordt **niet naar capabilities gekeken**, alleen naar de kernel-handler. Die `mouseN`-handler
  krijgt een apparaat met `BTN_LEFT` plus `ABS_X`/`ABS_Y`.
* `src/tools/vinput/README.md`: *"Voor de lightgun is hij schadelijk: zonder `BTN_LEFT` krijgt het
  apparaat geen `mouseN`-handler en toont TeknoParrot het niet in P1 Light Gun."*
* `docs/decisions.md`: de `VINPUT_JOY`-route is "onnodig door de udev-regel, en zonder `BTN_LEFT` toont
  TeknoParrot het apparaat niet als muis".

Dus: de muis-presentatie is nodig voor de **TeknoParrot-route** (die ons apparaat in "P1 Light Gun"
moet aanbieden), en diezelfde presentatie maakt het onzichtbaar voor de **SDL-joystickroute** van deze
native game. Dat is de kern van de tegenspraak, en het is geen fout van iemand maar een gevolg van twee
verschillende opsommingswijzen.

## 3. Wat er al bestaat om het op te lossen

* **`VINPUT_JOY`** (`src/tools/vinput/vinput.c`, regel 22, 93-102): met die variabele biedt het
  apparaat joystick-knoppen aan (`BTN_TRIGGER`/`THUMB`/`THUMB2` in plaats van `BTN_LEFT/RIGHT/MIDDLE`)
  en extra assen (`ABS_RX`, `ABS_RY`, `ABS_THROTTLE`, `ABS_HAT0X`), zodat udev er
  `ID_INPUT_JOYSTICK` van maakt. **Maar dat is precies wat de lightgun kapotmaakt** (geen `mouseN`
  meer) -- dus dit mag **niet** op het bestaande apparaat.
* **Een tweede apparaat is dus de route**: het bestaande lichtgeweer (1209:7202, muisklasse, voor de
  TeknoParrot/P1-route) laten staan, en daarnaast een **eigen uinput-joystick** aanbieden met `ABS_X`,
  `ABS_Y` en een trekkerknop, zodat udev `ID_INPUT_JOYSTICK` zet en SDL hem ziet.
* Daar is al ervaring mee in het project: `docs/decisions.md` (regel 321) beschrijft de hid-board-route
  als *"een uinput-joystick krijgt vanzelf `uaccess` (udev: `ID_INPUT_JOYSTICK`), Wine maakt er een
  HID-apparaat van (winebus, evdev)"*, en `src/ravengun/iobridge/hid_board.hpp` bouwt precies zo'n
  evdev-joystick via `/dev/uinput`. Die is alleen bedoeld als **invoerbord** (knoppen), niet als geweer
  met aim -- er is dus een werkend voorbeeld van het apparaat, maar niet van de aim-koppeling.

## 4. Wat de webcam NIET is (aantrekkelijke verkeerde verklaring, weggenomen)

In het logboek van **10 oktober** staat dezelfde webcam-mislukking als vandaag:
`Webcam: Could not ID device for player 0` en `All webcams reported brightness difference of 0` -- en
die sessie kwam **47 seconden later** wel in de attract-lus (`Failed to load FMOD sound bank: attract`,
daarna `level1`). **De webcamstap faalt in beide gevallen, dus hij is het verschil niet.** Dat hoort in
het dossier, want het is de verklaring die het meest voor de hand lijkt te liggen en die het niet is.

## 5. De volgende meting, in deze orde

1. **Opent de game ons apparaat wel?** Tijdens de kalibratie in `/proc/<pid>/fd` van het spelproces
   kijken welke `/dev/input/event*` openstaan -- dat is geen tracer (geen ptrace, dus geen risico dat de
   guest eraan sterft, zoals bij een andere game in dit project) en het antwoord zegt of het aan de
   **classificatie** ligt (de game opent event13 wel maar ziet hem niet als joystick) of aan de game.
2. **Daarna een tweede apparaat als joystick** aanbieden (`ABS_X`/`ABS_Y` + trekker, muisgevoed) naast
   het bestaande lichtgeweer -- **niet** het bestaande apparaat veranderen, want dan breekt de
   TeknoParrot-binding van speler 1.
3. **En dan de kalibratie herhalen** met positie en trekker op dat joystickapparaat, foto per stap, met
   dezelfde `gun.txt`-achtige meting van wat het apparaat uitzond.

**Als de joystick-route niet kan zonder iets anders te breken**, dan is dat de conclusie en niet een
stilte: dan is de volgende vraag of de kast hier een **specifiek apparaat** verwacht (naam, VID/PID of
asbereik) dat de game herkent.

## 6. De lat

Onveranderd: **gehaald = 5 minuten attract- of spelmodus.** Run 3, 5, 7 en 8 komen tot het
kalibratiescherm en niet verder; dat is de verst mogelijke stand zonder dat de game het geweer ziet.

# RUN 9 — de game opent GEEN enkel invoerapparaat, en SDL is niet eens geladen

*RavenGun, 2026-10-13. Uitgevoerd 11:19:05 - 11:21:4x (150 s, beeld op de tv). Niets draait; het
lichtgeweer is gedisarmeerend opgeruimd. Geen commit.*

## De meting (pid 859189, tijdens het kalibratiescherm, geen tracer)

    /dev/input/event : 0        <- GEEN enkel invoerapparaat open
    /dev/input/js    : 0
    /dev/hidraw      : 0
    ons eigen event13: 0
    SDL-mappings     : 0        <- SDL is NIET in het proces
    udev-mappings    : 5        <- libudev IS geladen (/usr/lib/libudev.so.1.7.14)
    libinput         : 0

Alleen de preload (librg_rt_input.so), libudev en de logboek-fds zijn open. Geen /dev/input, geen
/dev/hidraw.

## Wat dat betekent: de joystickhypothese staat op losse schroeven

Wij gingen ervan uit dat de game zijn geweer via **SDL-joysticks** zoekt (dat staat in de eerdere
projectnotities). Maar **SDL is niet eens in het proces geladen** in deze native route, en de game houdt
**geen enkel** evdev-apparaat open. Een tweede apparaat als joystick aanbieden zou dus waarschijnlijk
werk zijn dat de game nooit bekijkt -- **tenzij** blijkt dat de game SDL pas later laadt.

Wat de game WEL laadt is **libudev**. En wat de game zelf zegt, wijst een andere kant op:

    Webcam: Could not ID device for player 0 (0, 0, 0, 0)
    Webcam: Aborting auto-id. All webcams reported brightness difference of 0

De game somt **camera's** op (libudev/V4L2) en meet **helderheidsverschil** -- dat is precies het
gedrag van een lichtgeweer met een IR-punt dat optisch gevolgd wordt. De FTDI- en libusb-code zit
**statisch in de game** (ronde 1: geen libftdi*/libusb* in DT_NEEDED), en dat is ook het pad dat
onze USB-tap nooit zag. Kortom: het is goed mogelijk dat dit spel zijn geweer **optisch** volgt en zijn
trekker via de **kastkaart** leest -- en dat het kalibratiescherm daarom wacht op een camera die het
geweer kan ZIEN, niet op een evdev-apparaat dat we kunnen maken.

## En waarom 10 oktober dan wel verder kwam

De projectnotitie zegt het zelf: met de gerepareerde FMOD-stub komt de game **na de
gun-kalibratieschermen (TEST/START)** in de attract-lus. Op 10 oktober is dat gebeurd (de
ttract- en level1-banken) terwijl de camera daar **ook** faalde. De route naar de attract-lus is
dus het **overslaan** van de kalibratie -- en dat is precies wat bij ons niet lukt: onze TEST-indrukken
komen aan in de tafel (run 7, van twee kanten gemeten) en het scherm verspringt niet.

## De volgende vraag, en waarom ik het tweede apparaat nu NIET bouw

Voordat ik een joystickapparaat bouw dat de game misschien nooit bekijkt, is de goedkope vraag:
**wat doet de game met libudev en met de camera** -- en kan de kalibratie op een andere manier
overgeslagen worden (een andere schakelaar, een langere indruk, of een tweede TEST op een moment dat de
game in de kalibratietoestand naar de tafel kijkt)? Dat is dezelfde vraag als bij de START-indruk, waar
herhalen de oplossing was.

**Wat run 9 NIET oplevert:** geen uitspraak over het FTDI-protocol (de tap zag niets), geen attract- of
spelmodus (de lat is niet gehaald), en geen bewijs dat de aim optisch is -- dat is een afleiding uit de
webcamregels en uit het feit dat er geen enkel invoerapparaat openstaat. En het apparaat dat ik in run 8
aanbood is niet kapot: het zond alles uit wat het scherm vraagt; de game keek er alleen niet naar.

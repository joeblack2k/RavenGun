# vinput

Maakt een virtueel evdev-apparaat via /dev/uinput, voor diagnose van TeknoParrot (leest evdev; XTEST-events zijn onzichtbaar).

    make
    (echo "key f2"; sleep 1) | ./vinput kbd      # "RavenGun Virtual Keyboard" 1209:7201
    ./vinput abs --hold 600 < cmds                # "RavenGun Virtual Lightgun" 1209:7202

Commando's (stdin, een per regel): `key NAAM [down|up|tap]`, `abs X Y` (0..65535), `btn left|right|middle [down|up|click]`, `sleep MS`, `quit`.
Het apparaat bestaat zolang het proces draait (stdin open houden, bv. via een FIFO).

**Status:** `vinput kbd` is het virtuele toetsenbord voor Test/Service/Coin/Start. De lightgun-kant (`vinput abs`) is een diagnosetool en is vervangen door de daemon: `ravengund` maakt na `ravengunctl arm` hetzelfde apparaat (zelfde naam, VID/PID 1209:7202) met arm/disarm, knoppen vrijgeven en testpatronen (zie `docs/architecture.md`, sectie "Uitvoer"). Draai `vinput abs` en de daemon niet tegelijk: dezelfde identiteit tweemaal geeft twee apparaten met dezelfde naam en by-id-link.

## Leesbaarheid en classificatie voor TeknoParrot

Een uinput-apparaat krijgt `/dev/input/eventN` als `root:input 0660`. De udev-regel `packaging/udev/70-ravengun.rules` (installeren met `sudo scripts/udev-ravengun.sh --apply`) geeft de ingelogde gebruiker leesrecht (uaccess) voor VID 1209 met PID 7201 en 7202, en maakt de symlinks `/dev/input/ravengun-kbd` en `ravengun-gun` en de by-id-links `/dev/input/by-id/usb-RavenGun_Virtual_Keyboard-event-kbd` en `...Lightgun-event-mouse`. De `VINPUT_JOY`-truc (joystick-capabilities zodat udev een ACL zet) is daardoor niet meer nodig voor leesbaarheid. Voor de lightgun is hij schadelijk: zonder `BTN_LEFT` krijgt het apparaat geen `mouseN`-handler en toont TeknoParrot het niet in "P1 Light Gun".

TeknoParrot toont een apparaat als lightgun/muis alleen als `/proc/bus/input/devices` een `mouse`-handler vermeldt en het apparaat leesbaar is (`EvdevInterop.EnumerateMice`). Een absolute muis met `BTN_LEFT` plus `ABS_X`/`ABS_Y` en zonder `INPUT_PROP_DIRECT` voldoet daaraan. Zie `docs/architecture.md`.

Het evdev-pad (`/dev/input/eventN`) verandert na een herstart en tussen apparaten. TeknoParrot slaat het by-id-pad op als dat bestaat (`Halo.xml` en `InputBindings/Halo.json`); de udev-regel maakt dat pad. TeknoParrot opent het apparaat bij de start van de game en heropent het niet: het apparaat (en dus dit proces) moet bestaan vóór `LAUNCH GAME` en blijven bestaan zolang de game draait.

KWin ziet een absolute muis als pointer en beweegt de desktopcursor. De udev-regel zet daarom `LIBINPUT_IGNORE_DEVICE=1` op de lightgun (7202). Het toetsenbord (7201) heeft die regel niet.

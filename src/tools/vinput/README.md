# vinput

Maakt een virtueel evdev-apparaat via /dev/uinput, voor diagnose van TeknoParrot (leest evdev; XTEST-events zijn onzichtbaar).

    make
    (echo "key f2"; sleep 1) | ./vinput kbd      # "RavenGun Virtual Keyboard" 1209:7201
    ./vinput abs --hold 600 < cmds                # "RavenGun Virtual Lightgun" 1209:7202

Commando's (stdin, een per regel): `key NAAM [down|up|tap]`, `abs X Y` (0..65535), `btn left|right|middle [down|up|click]`, `sleep MS`, `quit`.
Het apparaat bestaat zolang het proces draait (stdin open houden, bv. via een FIFO).

Keuze lightgun: ABS_X/ABS_Y 0..65535 + BTN_LEFT/RIGHT/MIDDLE, geen INPUT_PROP_DIRECT (dat betekent touchscreen en laat de compositor het als touch behandelen). Let op: kwin ziet het als absolute muis en verplaatst de desktopcursor; stuur alleen events als het doelvenster focus heeft.

## Leesbaarheid voor TeknoParrot (belangrijk)

Een uinput-apparaat krijgt `/dev/input/eventN` als `root:input 0660`; gebruiker `deck` zit niet in `input`, en udev zet alleen een uaccess-ACL (70-uaccess.rules) op apparaten met `ID_INPUT_JOYSTICK`. Het TeknoParrot-UI meldt anders "Keyboard(s)/Mouse device(s) not directly readable". Met `VINPUT_JOY=1` krijgt het apparaat extra joystick-capabilities (BTN_TRIGGER/BTN_SOUTH, extra assen) zodat udev het als joystick classificeert en de ACL zet. Voor `abs` vervangt dit BTN_LEFT/RIGHT/MIDDLE door BTN_TRIGGER/THUMB/THUMB2 (BTN_MOUSE-bits zouden de classificatie op "mouse" vastzetten). Een puur muisachtig apparaat (zonder VINPUT_JOY) is voor deck niet leesbaar zonder de udev-regel `setup/install-udev-rules.sh` van TeknoParrot (sudo).

Het evdev-pad (`/dev/input/eventN`) verandert na elke herstart; TeknoParrot slaat dat pad op (zie Halo.xml / InputBindings/Halo.json).

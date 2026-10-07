# vinput

Maakt een virtueel evdev-apparaat via /dev/uinput, voor diagnose van TeknoParrot (leest evdev; XTEST-events zijn onzichtbaar).

    make
    (echo "key f2"; sleep 1) | ./vinput kbd      # "RavenGun Virtual Keyboard" 1209:7201
    ./vinput abs --hold 600 < cmds                # "RavenGun Virtual Lightgun" 1209:7202

Commando's (stdin, een per regel): `key NAAM [down|up|tap]`, `abs X Y` (0..65535), `btn left|right|middle [down|up|click]`, `sleep MS`, `quit`.
Het apparaat bestaat zolang het proces draait (stdin open houden, bv. via een FIFO).

Keuze lightgun: ABS_X/ABS_Y 0..65535 + BTN_LEFT/RIGHT/MIDDLE, geen INPUT_PROP_DIRECT (dat betekent touchscreen en laat de compositor het als touch behandelen). Let op: kwin ziet het als absolute muis en verplaatst de desktopcursor; stuur alleen events als het doelvenster focus heeft.

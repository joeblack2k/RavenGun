#!/bin/bash
# (a) het bewijs van run 8 in het dossier zetten, (b) uitzoeken of het geweer als JOYSTICK kan worden
# aangeboden: wat udev vindt, wat de vinput-tool doet, en of er al een joystick-truc is.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo

echo "############ 1. HET BEWIJS VAN RUN 8 NAAR HET DOSSIER"
RUN8=$(ls -d /tmp/run8-* 2>/dev/null | head -1)
echo "  bron: ${RUN8:-GEEN}"
if [ -n "${RUN8:-}" ]; then
  for f in gun.txt leven tabel.txt play.out exit; do
    [ -f "$RUN8/$f" ] && cp -a "$RUN8/$f" "$P/run8-$f" && printf '  bewaard: patches/Halo/run8-%s\n' "$f"
  done
fi
echo "  --- wat er nu in het dossier staat:"
ls -l "$P"/run8-* 2>/dev/null | sed 's/^/    /'
echo "  (gun.txt = run8-lichtgeweer.txt, de meting van het apparaat)"

echo
echo "############ 2. WAT UDEV VAN HET LICHTGEWEER VINDT (bepaalt of SDL het als joystick ziet)"
command -v udevadm >/dev/null && udevadm info --query=property --name=/dev/input/event13 2>/dev/null \
  | grep -E 'ID_INPUT|ID_VENDOR|ID_MODEL|ID_PATH|NAME' | sed 's/^/  /' || echo "  udevadm niet aanwezig"
echo "  --- de capabilities uit sysfs:"
for c in abs key rel; do
  printf '    %-4s %s\n' "$c" "$(cat /sys/class/input/event13/device/capabilities/$c 2>/dev/null | head -c 60)"
done
echo "  --- heeft het apparaat INPUT_PROP_DIRECT (joystick/direct) of niet?"
cat /sys/class/input/event13/device/properties 2>/dev/null | sed 's/^/    properties: /'
echo "  --- en dezelfde vraag voor de Steampuck-muis (die de game WEL lijkt te gebruiken):"
udevadm info --query=property --name=/dev/input/event7 2>/dev/null | grep -E 'ID_INPUT' | sed 's/^/    /'

echo
echo "############ 3. DE VINPUT-TOOL: hoe wordt het geweer gemaakt, en kan er een joystick bij?"
ls -l "$R/tools/vinput/" 2>/dev/null | sed 's/^/  /'
for f in "$R/tools/vinput/README.md" "$R/tools/vinput/"*.c "$R/tools/vinput/"*.h; do
  [ -f "$f" ] || continue
  echo "  --- $(basename "$f"):"
  grep -nE 'JOY|joystick|ABS_X|ABS_Y|BTN_LEFT|INPUT_PROP|capabilit|uinput|muis|mouse|tweede|device' "$f" 2>/dev/null | head -18 | sed 's/^/    /'
done

echo
echo "############ 4. IS ER AL EEN JOYSTICK-TRUI? (zoeken in de hele repo)"
grep -rn 'VINPUT_JOY\|ID_INPUT_JOYSTICK\|joystick-capabilities\|ABS_HAT\|BTN_TRIGGER' "$R/src" "$R/tools" "$R/docs" "$R/scripts" 2>/dev/null | head -15 | sed 's/^/  /'
echo "  --- en in de architectuur:"
grep -rn -iE 'vinput|lightgun|joystick' "$R/docs/architecture.md" 2>/dev/null | head -12 | sed 's/^/  /'
echo "== done"

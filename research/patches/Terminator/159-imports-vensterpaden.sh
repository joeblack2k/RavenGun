#!/bin/bash
# ts/159-imports-vensterpaden.sh - BENCH-VRIJ. Welke WEGEN naar een venster importeert de gast eigenlijk?
#   Als hij geen SDL importeert, is die weg gratis uitgesloten. En als hij geen enkele XCreateWindow /
#   glXCreateWindow importeert, is ook die weg uitgesloten ZONDER een run.
set -u
GAME="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game"
echo "############ 1. ALLE UND-symbolen die met vensters/tekenen te maken hebben"
readelf -sW --dyn-syms "$GAME" 2>/dev/null | awk '$7=="UND"{print $8}' | sed 's/@.*//' | sort -u > /tmp/imp.txt
for groep in "GLUT:glut" "GLX:glX" "X11:X" "SDL:SDL" "EGL:egl" "GL:gl[A-Z]" "GLEW:glew" "GLFW:glfw" "winsys:CreateWindow"; do
  naam="${groep%%:*}"; pat="${groep##*:}"
  n=$(grep -cE "$pat" /tmp/imp.txt 2>/dev/null || true)
  printf '  %-8s %3s imports\n' "$naam" "${n:-0}"
done
echo
echo "############ 2. de X11-functies die een venster MAKEN of TONEN (de kern van de vraag)"
grep -E '^(XCreateWindow|XCreateSimpleWindow|XMapWindow|XMapRaised|XOpenDisplay|XDefaultScreen|XRootWindow|XStoreName|XInternAtom|XChangeProperty|XSetWMHints|XSetWMNormalHints|XResizeWindow|XMoveResizeWindow)$' /tmp/imp.txt | sed 's/^/  WEL: /' || true
n=$(grep -cE '^(XCreateWindow|XCreateSimpleWindow|XMapWindow|XMapRaised|XOpenDisplay)$' /tmp/imp.txt 2>/dev/null || true)
[ "${n:-0}" -eq 0 ] && echo "  GEEN enkele XCreateWindow/XMapWindow/XOpenDisplay -> die weg is GRATIS uitgesloten" || echo "  ($n van die kernfuncties worden WEL geimporteerd)"
echo
echo "############ 3. en de GLX-kant"
grep -E '^glX' /tmp/imp.txt | sed 's/^/  /' || echo "  geen enkele glX-functie"
echo
echo "############ 4. en SDL / EGL / GLFW"
grep -iE '^(SDL|egl|glfw)' /tmp/imp.txt | sed 's/^/  /' || echo "  GEEN SDL, EGL of GLFW -> die wegen zijn GRATIS uitgesloten"
echo
echo "############ 5. wat de gast WEL importeert aan vensterachtigs (ruimer)"
grep -iE 'window|display|screen|surface|context|visual' /tmp/imp.txt | sed 's/^/  /' || echo "  niets"
echo
echo "############ 6. de volledige lijst GL/GLUT/GLX, zodat niets over het hoofd wordt gezien"
grep -E '^(gl|glut|glX)' /tmp/imp.txt | sed 's/^/  /'

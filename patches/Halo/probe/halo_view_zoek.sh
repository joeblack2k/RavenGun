#!/bin/bash
# Heeft Halo een eigen game.bat (of ander startbestand) in de VIEWS, net als de zeven games die de
# coördinator vond? Alles lezen; niets starten.
set -u
V=~/.local/share/ravengun/runtime/views

echo "############ 1. welke views zijn er?"
ls -1 "$V" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 2. heeft Halo een view, en wat staat erin?"
for naam in Halo "Halo Fireteam" "HaloFireteam"; do
  D="$V/$naam"
  [ -d "$D" ] || continue
  echo "  --- $D:"
  ls -la "$D" | sed 's/^/    /'
  echo "  --- en alles wat op een startbestand lijkt (2 diep):"
  find "$D" -maxdepth 2 \( -iname '*.bat' -o -iname '*.cmd' -o -iname '*.sh' -o -iname '*.ini' -o -iname '*.exe' -o -iname '*.conf' \) 2>/dev/null | head -20 | sed 's/^/    /'
done
[ -d "$V/Halo" ] || echo "  GEEN view met de naam Halo (zie de lijst hierboven)"

echo
echo "############ 3. de zeven games met een game.bat, als patroon"
for g in HOTDSD LGI LGI3D LGS LuigisMansion MedaruNoGunman OG; do
  f="$V/$g/game.bat"
  if [ -f "$f" ]; then
    printf '  %-16s %s bytes  %s\n' "$g" "$(stat -c%s "$f")" "$(head -c 60 "$f" | tr -d '\r\n' | cut -c1-50)"
  else
    printf '  %-16s (geen game.bat op dit pad)\n' "$g"
  fi
done

echo
echo "############ 4. en heeft Halo ergens anders een bat/cmd in de views?"
find "$V" -maxdepth 3 -iname '*halo*' 2>/dev/null | head -20 | sed 's/^/  /'
echo "  --- alle game.bat-bestanden in de views:"
find "$V" -maxdepth 2 -iname 'game.bat' 2>/dev/null | sed 's/^/    /'

echo
echo "############ 5. staat er in de Halo-view iets dat op de NAS-map lijkt (utils e.d.)?"
if [ -d "$V/Halo" ]; then
  find "$V/Halo" -maxdepth 3 2>/dev/null | head -30 | sed 's/^/  /'
fi
echo "== done"

#!/bin/bash
# Controleer het ARTEFACT op de juiste manier: de foutmeldingen en logregels van mijn regelsoorten
# zijn echte teksten in .rodata -- macro's en samengestelde sleutels zijn dat niet.
set -u
SO=~/projects/ravengun/build/librg_rt_input.so
echo "  artefact: $(stat -c '%s bytes, %y' $SO)"
sha256sum $SO | sed 's/^/  sha256 /'
echo "  --- de logregel die de tabel samenvat (bewijst dat de as/teller/zet-takken erin zitten):"
strings -n 8 $SO | grep -iE 'assen|tellers|vaste waarden|schakelaars' | head -4 | sed 's/^/    /'
echo "  --- de foutmeldingen van mijn parser (per regelsoort):"
strings -n 10 $SO | grep -iE 'te veel assen|kan niet geschreven worden|teller\)|zet\)|as \(bron' | head -6 | sed 's/^/    /'
echo "  --- de sleutelwoorden zoals ze in de bron staan (los, niet samengesteld):"
for w in 'soort' 'f32' 'schaal' 'buiten' 'modulo' 'breedte' 'waarde' 'adres'; do
  printf '    %-9s %s\n' "$w" "$(strings -n 3 $SO | grep -cx "$w")"
done

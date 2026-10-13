#!/bin/bash
# De rest van de gebruiksaanwijzing: het uitvoergedeelte (het virtuele lichtgeweer) en de muismodus.
set -u
CTL=~/projects/ravengun/build/ravengunctl

echo "############ 1. het UITVOERGEDEELTE van de hulp (dat werd afgekapt)"
"$CTL" --help 2>&1 | sed -n '/uitvoer (virtueel/,$p' | head -45 | sed 's/^/  /'

echo
echo "############ 2. de volledige status (om de blokken te zien)"
"$CTL" status 2>&1 | head -60 | sed 's/^/  /'

echo
echo "############ 3. probeer de muismodus met de by-id-naam van de puck (relatief, event7)"
MID='usb-Valve_Software_Steam_Controller_Puck_FXB9955202923-if02-event-mouse'
"$CTL" mouse-gun --help 2>&1 | head -15 | sed 's/^/  help: /'
"$CTL" mouse-gun --player 1 --mouse "$MID" 2>&1 | head -10 | sed 's/^/  zetten: /'
sleep 1
echo "  --- de toewijzingen NA:"
"$CTL" mice 2>&1 | sed -n '/Toewijzingen/,$p' | head -8 | sed 's/^/    /'

echo
echo "############ 4. en is de uitvoer gewapend?"
"$CTL" status 2>&1 | grep -iA6 'uitvoer\|armed\|lightgun\|output' | head -25 | sed 's/^/  /'
echo "== done"

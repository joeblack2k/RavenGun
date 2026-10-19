#!/usr/bin/env bash
# (D) Robin Hood: de twee adressen uit de meetdraad (game.exe+0x9e7bc8 en +0x8ff9b) staan in de image
# van de exe maar in GEEN enkele sectie (.text eindigt op 0x5eb35c, .data op 0x68de00, .rsrc begint op
# 0x1ab0000, SizeOfImage = 0x16b1000). Die code wordt dus tijdens het starten aangevuld -- de exe is
# beveiligd/verpakt. In het bestand valt er niets te ontleden; daarom dit script: het laat het spel
# draaien, zoekt de image in /proc/<pid>/maps en dumpt de bytes op die twee plekken, zodat ze met
# objdump als 32-bits code te lezen zijn.
#
# Gebruik: tools/dev/rh-dump-adressen.sh [rva ...]     (standaard 0x9e7bc8 0x8ff9b)
set -uo pipefail
cd /home/deck/projects/ravengun || exit 2
CTL="$HOME/.local/opt/ravengun/bin/ravengunctl"
VIEWMAP="$HOME/.local/share/ravengun/runtime/views/RobinHood/data"
# Niet in /tmp: de uitvoer hoort in de uit-map van de repo (die staat in .gitignore), zodat een dump
# terug te vinden is bij het gereedschap waar hij bij hoort.
UIT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/tools/rg-rhio/out/rh-dump"
mkdir -p "$UIT"
RVAS=("$@")
[ ${#RVAS[@]} -eq 0 ] && RVAS=(0x9e7bc8 0x8ff9b)

echo "== spel starten =="
"$CTL" game start RobinHood >/dev/null 2>&1
sleep 40

# Het GROOTSTE proces met de naam game.exe kiezen: er kan een klein hulp-exe met dezelfde naam staan
# (de view heeft er een van 71 bytes, een hulpje), en dan dump je de verkeerde image (review 14, punt e).
PID=""
BESTE=0
for P in $(ls /proc | grep -E '^[0-9]+$'); do
    N=$(cat "/proc/$P/comm" 2>/dev/null)
    case "$N" in game.exe|Game.exe) ;; *) continue;; esac
    GROOT=$(awk '/game.exe/ {s += $2 - $1} END {print s+0}' "/proc/$P/maps" 2>/dev/null)
    echo "kandidaat pid $P met $(printf '%d' "$GROOT") bytes aan game.exe-image"
    if [ "${GROOT:-0}" -gt "$BESTE" ]; then BESTE=$GROOT; PID=$P; fi
done
echo "gekozen: pid=$PID (image $BESTE bytes)"
[ -n "$PID" ] || { echo "geen game.exe-proces gevonden"; "$CTL" game stop >/dev/null 2>&1; exit 1; }
echo "pid=$PID"

# De image van de exe in de kaart van dit proces: de regel met game.exe en de laagste offset.
REGEL=$(grep -m1 'game.exe' "/proc/$PID/maps" 2>/dev/null)
BASIS=$((0x$(echo "$REGEL" | cut -d- -f1)))
echo "image begint op $REGEL"
echo "basis = $(printf '0x%x' "$BASIS")"

for RVA in "${RVAS[@]}"; do
    ADRES=$((BASIS + RVA))
    echo "== rva $RVA -> $(printf '0x%x' "$ADRES") =="
    if dd if="/proc/$PID/mem" of="$UIT/rva-$RVA.bin" bs=1 skip="$ADRES" count=192 2>/dev/null; then
        echo "eerste 32 bytes:"; xxd -l 32 "$UIT/rva-$RVA.bin" 2>/dev/null | cut -c1-70
        objdump -D -b binary -m i386 --adjust-vma="$ADRES" "$UIT/rva-$RVA.bin" 2>/dev/null | sed -n '8,22p' | cut -c1-90
    else
        echo "kon /proc/$PID/mem op dat adres niet lezen"
    fi
done

echo "== spel stoppen =="
"$CTL" game stop >/dev/null 2>&1

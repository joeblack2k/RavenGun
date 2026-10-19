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
# De uit-map volgt dit script (de repo waar het in staat), dus EERST bepalen en dan pas ergens anders
# heen: met een relatief scriptpad wees hij na een cd naar een andere boom (review 14, ronde 2).
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PROC="${RG_PROC:-/proc}"

# Hoe groot is de game.exe-image in deze kaart, en waar begint hij? Uitvoer: "<bytes> <basis-hex>".
# Alleen afbeeldingen waarvan het PAD eindigt op game.exe (hoofdletters maken niet uit), dus niet
# game.exe.so of een map met die naam. Een regel is "begin-eind perms offset dev inode pad", met begin
# en eind in hex: eerder telde awk "$2 - $1" (de perms min een half gelezen hexgetal), zodat er nooit
# een proces gekozen werd (review 14, ronde 2).
image_van() {   # $1 = maps-bestand
    local bereik perms off dev inode pad kl b e tot=0 basis=""
    while read -r bereik perms off dev inode pad; do
        kl="${pad,,}"
        case "$kl" in */game.exe|game.exe) ;; *) continue;; esac
        b="${bereik%-*}"; e="${bereik#*-}"
        tot=$(( tot + 0x$e - 0x$b ))
        if [ -z "$basis" ] || (( 0x$b < 0x$basis )); then basis="$b"; fi
    done < "$1"
    echo "$tot ${basis:-0}"
}

# Het GROOTSTE proces met de naam game.exe kiezen: er kan een klein hulp-exe met dezelfde naam staan
# (de view heeft er een van 71 bytes, een hulpje), en dan dump je de verkeerde image (review 14, punt e).
# Uitvoer: "<pid> <bytes> <basis-hex>", of niets als er geen game.exe met een image is.
kies_proces() {   # $1 = proc-wortel
    local p n groot basis beste=0 uit=""
    for p in "$1"/[0-9]*; do
        [ -d "$p" ] || continue
        n=$(cat "$p/comm" 2>/dev/null) || continue
        [ "${n,,}" = game.exe ] || continue
        read -r groot basis < <(image_van "$p/maps" 2>/dev/null || echo "0 0")
        echo "kandidaat pid ${p##*/} met ${groot:-0} bytes aan game.exe-image" >&2
        if [ "${groot:-0}" -gt "$beste" ]; then beste=$groot; uit="${p##*/} $groot $basis"; fi
    done
    [ -n "$uit" ] && echo "$uit"
    return 0
}

# Gesourcet (de proef tests/rh_tools_test.sh): alleen de functies, niets starten.
if [ "${BASH_SOURCE[0]}" != "$0" ]; then return 0; fi

CTL="$HOME/.local/opt/ravengun/bin/ravengunctl"
# Niet in /tmp: de uitvoer hoort in de uit-map van de repo (tools/rg-rhio/out/ staat in .gitignore),
# zodat een dump terug te vinden is bij het gereedschap waar hij bij hoort.
UIT="$REPO/tools/rg-rhio/out/rh-dump"
mkdir -p "$UIT"
RVAS=("$@")
[ ${#RVAS[@]} -eq 0 ] && RVAS=(0x9e7bc8 0x8ff9b)

echo "== spel starten =="
"$CTL" game start RobinHood >/dev/null 2>&1
sleep 40

read -r PID BESTE BASISHEX < <(kies_proces "$PROC")
echo "gekozen: pid=${PID:-} (image ${BESTE:-0} bytes)"
[ -n "${PID:-}" ] || { echo "geen game.exe-proces gevonden"; "$CTL" game stop >/dev/null 2>&1; exit 1; }
echo "pid=$PID"

# De image van de exe in de kaart van dit proces: de laagste afbeelding van game.exe.
BASIS=$((0x$BASISHEX))
echo "basis = $(printf '0x%x' "$BASIS")"

for RVA in "${RVAS[@]}"; do
    ADRES=$((BASIS + RVA))
    echo "== rva $RVA -> $(printf '0x%x' "$ADRES") =="
    if dd if="$PROC/$PID/mem" of="$UIT/rva-$RVA.bin" bs=1 skip="$ADRES" count=192 2>/dev/null; then
        echo "eerste 32 bytes:"; xxd -l 32 "$UIT/rva-$RVA.bin" 2>/dev/null | cut -c1-70
        objdump -D -b binary -m i386 --adjust-vma="$ADRES" "$UIT/rva-$RVA.bin" 2>/dev/null | sed -n '8,22p' | cut -c1-90
    else
        echo "kon $PROC/$PID/mem op dat adres niet lezen"
    fi
done

echo "== spel stoppen =="
"$CTL" game stop >/dev/null 2>&1

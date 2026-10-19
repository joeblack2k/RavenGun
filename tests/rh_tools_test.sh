#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Proeven op het meetgereedschap van Robin Hood (review 14, ronde 2), zonder game en zonder daemon:
#
#   1. rh-dump-adressen.sh: de grootte van de game.exe-image uit een (nagemaakte) /proc/<pid>/maps,
#      in hex, alleen voor paden die op game.exe eindigen; en het grootste game.exe-proces wint van een
#      klein hulpje met dezelfde naam;
#   2. tools/rg-rhio/out/ (rh-dump/*.bin, A/B-uitvoer) wordt door git genegeerd;
#   3. ab2.sh: elke start komt in resultaten.txt, ook een gestopte (reden gestopt); alleen een start
#      waarbij de daemon aantoonbaar bezet was heet ongeldig-daemon-bezet en wordt overgedaan; de
#      bovengrens telt ongeldige starts OP RIJ; maak-tabel.py telt ze per variant;
#   4. ab2.sh en mutex-hermeten.sh zetten de drop-in in engine en view bij elk einde terug (ook bij
#      SIGTERM), weigeren als er een game draait (geen DLL-wissel, geen herstart), schrijven niet in
#      /tmp als standaard, en de foto-band (bureaublad = nee) en de rondenummering per variant kloppen.
#
# Alles in een tijdelijke map: een eigen HOME met een nep-ravengunctl, en een eigen PATH met een
# nep-systemctl en nep-sleep, zodat er niets aan de echte daemon, engine of view komt.
set -uo pipefail
ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
command -v python3 >/dev/null 2>&1 || { echo "rh-gereedschap: python3 ontbreekt"; exit 77; }
command -v flock >/dev/null 2>&1 || { echo "rh-gereedschap: flock ontbreekt"; exit 77; }

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fout=0
zeg() { echo "rh-gereedschap: $*"; }
mislukt() { zeg "FOUT: $*"; fout=1; }
gelijk() {   # $1 = wat, $2 = verwacht, $3 = gekregen
    if [ "$2" = "$3" ]; then zeg "goed: $1"; else mislukt "$1: verwacht '$2', kreeg '$3'"; fi
}

# --- 1. rh-dump: image en proceskeuze --------------------------------------------------------------
# shellcheck source=../tools/dev/rh-dump-adressen.sh
. "$ROOT/tools/dev/rh-dump-adressen.sh"
P="$W/proc"
mkdir -p "$P/101" "$P/202" "$P/303" "$P/self"
# 202: het echte spel, image in drie stukken (0x1000 + 0x5eb000 + 0x10c5000 = 0x16b1000), met een
# hoofdletter in het pad en een spatie in de map; plus regels die NIET mogen tellen.
cat > "$P/202/maps" <<'EOF'
00010000-00110000 rw-p 00000000 00:00 0
00400000-00401000 r--p 00000000 00:2a 1234                               /home/deck/views/Robin Hood/data/Game.exe
00401000-009ec000 r-xp 00001000 00:2a 1234                               /home/deck/views/Robin Hood/data/Game.exe
009ec000-01ab1000 rw-p 005ec000 00:2a 1234                               /home/deck/views/Robin Hood/data/Game.exe
7b000000-7b100000 r-xp 00000000 00:2a 99                                 /usr/lib/wine/game.exe.so
7c000000-7c001000 r--p 00000000 00:2a 98                                 /home/deck/game.exe/iets.dll
7d000000-7d010000 r--p 00000000 00:2a 97                                 /home/deck/views/data/notgame.exe
EOF
echo game.exe > "$P/202/comm"
# 101: het hulpje van 71 bytes, ook game.exe, met een kleine image.
printf '00400000-00401000 r--p 00000000 00:2a 55   /home/deck/views/RobinHood/game.exe\n' > "$P/101/maps"
echo game.exe > "$P/101/comm"
# 303: een ander proces.
printf '00400000-7f000000 r--p 00000000 00:2a 56   /x/wineserver\n' > "$P/303/maps"
echo wineserver > "$P/303/comm"

gelijk "image van het spel (bytes en basis)" "$((0x16b1000)) 00400000" "$(image_van "$P/202/maps")"
gelijk "image van het hulpje" "4096 00400000" "$(image_van "$P/101/maps")"
gelijk "geen game.exe in de kaart" "0 0" "$(image_van "$P/303/maps")"
gelijk "het grootste game.exe-proces gekozen" "202 $((0x16b1000)) 00400000" "$(kies_proces "$P" 2>/dev/null)"
rm -rf "$P/202"
gelijk "zonder spel: het hulpje (enig game.exe)" "101 4096 00400000" "$(kies_proces "$P" 2>/dev/null)"
rm -rf "$P/101"
gelijk "geen game.exe: niets gekozen" "" "$(kies_proces "$P" 2>/dev/null)"

# --- 2. de uit-map wordt genegeerd -----------------------------------------------------------------
if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    for f in tools/rg-rhio/out/rh-dump/rva-0x8ff9b.bin tools/rg-rhio/out/ab-rhio/resultaten.txt \
             tools/rg-rhio/out/ab-rhio/OLD-1.png; do
        if git -C "$ROOT" check-ignore -q "$f"; then zeg "goed: genegeerd $f"; else mislukt "$f wordt niet genegeerd"; fi
    done
    if git -C "$ROOT" check-ignore -q tools/dev/ab-rhio/ab2.sh; then mislukt "ab2.sh wordt genegeerd"; fi
else
    zeg "geen git-boom; .gitignore-controle overgeslagen"
fi

# --- de nep-omgeving ---------------------------------------------------------------------------------
H="$W/home"
BIN="$W/bin"
S="$W/staat"
ENG="$H/.local/opt/ravengun/patches/RobinHood/dropin/rg-rhio.dll"
VIEWMAP="$H/.local/share/ravengun/runtime/views/RobinHood/data"
VIEW="$VIEWMAP/rg-rhio.dll"
mkdir -p "$BIN" "$S" "$H/.local/opt/ravengun/bin" "$(dirname "$ENG")" "$VIEWMAP"

# ravengunctl: de toestand staat in $S. "plan" is per start een regel: draait (de game blijft lopen),
# stopt (de game valt na de start om) of bezet (vlak na de herstart van de daemon start een ander een
# game; die loopt drie statusvragen en stopt dan zelf).
cat > "$H/.local/opt/ravengun/bin/ravengunctl" <<EOF
#!/usr/bin/env bash
S="$S"
echo "ctl \$*" >> "\$S/aanroepen"
status() { cat "\$S/status" 2>/dev/null || echo geen; }
case "\$1 \${2:-}" in
  "game status")
    a=\$(cat "\$S/ander" 2>/dev/null || echo 0)
    if [ "\$a" -gt 0 ]; then echo \$((a - 1)) > "\$S/ander"; echo "De game draait."; exit 0; fi
    if [ "\$(status)" = draait ]; then echo "De game draait."; else echo "Er draait geen game."; fi ;;
  "game start")
    a=\$(cat "\$S/ander" 2>/dev/null || echo 0)
    stap=\$(head -1 "\$S/plan"); sed -i 1d "\$S/plan"
    if [ "\$a" -gt 0 ]; then echo "ravengunctl: er draait al een game; stop die eerst" >&2; exit 4; fi
    case "\$stap" in draait) echo draait > "\$S/status";; *) echo geen > "\$S/status";; esac
    echo "De game wordt gestart..." ;;
  "game stop") echo geen > "\$S/status" ;;
  *) : ;;
esac
EOF
# systemctl: herstart alleen in de staat; bij een "bezet"-start begint de game van een ander.
cat > "$BIN/systemctl" <<EOF
#!/usr/bin/env bash
echo "systemctl \$*" >> "$S/aanroepen"
case "\$*" in *restart*) [ "\$(head -1 "$S/plan")" = bezet ] && echo 3 > "$S/ander";; esac
exit 0
EOF
# sleep: meteen terug. Staat er een getal N in $S/breek-af, dan breekt de N-de "sleep 90" (de wachttijd
# van de proef, als de proef-DLL er staat) de proef af met SIGTERM.
cat > "$BIN/sleep" <<EOF
#!/usr/bin/env bash
if [ "\${1:-}" = 90 ] && [ -s "$S/breek-af" ]; then
    n=\$(cat "$S/breek-af")
    if [ "\$n" -le 1 ]; then rm -f "$S/breek-af"; kill -TERM "\$PPID"; else echo \$((n - 1)) > "$S/breek-af"; fi
fi
exit 0
EOF
# De schermfoto: een bestand van de grootte in $S/foto (standaard 300 kB, voorbij BOOTING).
cat > "$BIN/tv-env" <<EOF
#!/usr/bin/env bash
while [ \$# -gt 0 ] && [ "\$1" != -o ]; do shift; done
head -c "\$(cat "$S/foto" 2>/dev/null || echo 300000)" /dev/zero > "\$2"
EOF
chmod +x "$H/.local/opt/ravengun/bin/ravengunctl" "$BIN/systemctl" "$BIN/sleep" "$BIN/tv-env"

# Veiligheid: de proeven mogen alleen de nep-programma's zien.
[ "$(PATH="$BIN:$PATH" command -v systemctl)" = "$BIN/systemctl" ] || { mislukt "nep-systemctl niet eerst in PATH"; exit 1; }

nep() {   # draai een proefscript in de nep-omgeving
    env HOME="$H" PATH="$BIN:$PATH" RG_AB_TVENV="$BIN/tv-env" RG_AB_MUIS=nep-muis "$@"
}
opnieuw() {   # schone staat: originele DLL's, geen game
    rm -rf "$S"/* "$W/uit"
    printf 'ORIGINEEL-ENGINE' > "$ENG"
    printf 'ORIGINEEL-VIEW' > "$VIEW"
    echo geen > "$S/status"
    mkdir -p "$W/uit" "$W/dll"
    printf OLD > "$W/uit/old-rg-rhio.dll"
    printf NEW > "$W/dll/new.dll"
    printf NOSTRIP > "$W/dll/nostrip.dll"
}
teruggezet() {   # $1 = wanneer
    gelijk "$1: engine-DLL teruggezet" ORIGINEEL-ENGINE "$(cat "$ENG" 2>/dev/null)"
    gelijk "$1: view-DLL teruggezet" ORIGINEEL-VIEW "$(cat "$VIEW" 2>/dev/null)"
    [ ! -e "$W/uit/reserve" ] || mislukt "$1: de reservemap staat er nog"
}
AB="$ROOT/tools/dev/ab-rhio/ab2.sh"
MUT="$ROOT/tools/dev/ab-rhio/mutex-hermeten.sh"
ABENV=(RG_AB_UIT="$W/uit" RG_AB_NEW="$W/dll/new.dll" RG_AB_NOSTRIP="$W/dll/nostrip.dll")

# --- 3. ab2.sh: elke start telt, ongeldig alleen bij een bezette daemon ------------------------------
opnieuw
printf '%s\n' draait stopt bezet draait draait draait draait > "$S/plan"
nep env "${ABENV[@]}" DOEL=2 bash "$AB" > "$W/ab.uit" 2>&1
rc=$?
gelijk "ab2 loopt af" 0 "$rc"
R="$W/uit/resultaten.txt"
gelijk "alle zeven starts staan in resultaten" 7 "$(wc -l < "$R")"
gelijk "de gestopte start van NEW staat erin" "NEW 1 nee gestopt" "$(awk '$1=="NEW" && $2==1 {print $1, $2, $6, $9}' "$R")"
gelijk "de bezette start is ongeldig en heeft NOSTRIP niet geteld" "NOSTRIP 1 ongeldig-daemon-bezet" \
    "$(awk '$9=="ongeldig-daemon-bezet" {print $1, $2, $9}' "$R")"
gelijk "geldige starts per variant" "2 2 2" \
    "$(for v in OLD NEW NOSTRIP; do awk -v v=$v '$1==v && $9!="ongeldig-daemon-bezet"' "$R" | wc -l; done | xargs)"
gelijk "het muis-geweer uit RG_AB_MUIS" 7 "$(grep -c 'mouse-gun --player 1 --mouse nep-muis' "$S/aanroepen")"
teruggezet "ab2 na een gewone reeks"
T=$(python3 -I "$ROOT/tools/dev/ab-rhio/maak-tabel.py" "$R")
gelijk "maak-tabel: NEW 2 geldig, 1 draait, 1 gestopt, 0 ongeldig" "| NEW | 2 | 2 | 1 | 1 | 0 |" \
    "$(echo "$T" | sed -n '/geldige starts/,$p' | grep '^| NEW |' | cut -d'|' -f1-7)|"
gelijk "maak-tabel: NOSTRIP 2 geldig en 1 ongeldig" "| NOSTRIP | 2 | 2 | 2 | 0 | 1 |" \
    "$(echo "$T" | sed -n '/geldige starts/,$p' | grep '^| NOSTRIP |' | cut -d'|' -f1-7)|"
# Een oud bestand (acht kolommen) leest nog: de reden volgt uit "draait".
printf 'OLD 1 d00986ec 23751 nee ja 21143 1142\nNEW 1 52193f86 23408 nee nee 2919 1142\n' > "$W/oud.txt"
T=$(python3 -I "$ROOT/tools/dev/ab-rhio/maak-tabel.py" "$W/oud.txt")
gelijk "maak-tabel: oud bestand, NEW gestopt" "| NEW | 1 | 0 | 0 | 1 | 0 |" "$(echo "$T" | sed -n '/geldige starts/,$p' | grep '^| NEW |' | cut -d'|' -f1-7)|"

# De bovengrens telt ongeldige starts OP RIJ: zeven bezette starts verspreid over de reeks (nooit meer
# dan twee op rij) stoppen de reeks niet meer (eerder: na zes in totaal).
opnieuw
{ echo bezet; for _ in 1 2 3 4 5 6; do echo bezet; echo draait; done; } > "$S/plan"
nep env "${ABENV[@]}" DOEL=2 bash "$AB" > "$W/ab2.uit" 2>&1
gelijk "zeven verspreide ongeldige starts: de reeks loopt af" "KLAAR (OLD=2 NEW=2 NOSTRIP=2)" "$(grep '^KLAAR' "$W/ab2.uit")"
gelijk "en ze staan er alle zeven in" 7 "$(grep -c ongeldig-daemon-bezet "$W/uit/resultaten.txt")"
teruggezet "ab2 na verspreide ongeldige starts"
# Zeven op rij stoppen wel.
opnieuw
for _ in 1 2 3 4 5 6 7 8; do echo bezet; done > "$S/plan"
nep env "${ABENV[@]}" DOEL=2 bash "$AB" > "$W/ab3.uit" 2>&1
if grep -q "te veel ongeldige runs op rij" "$W/ab3.uit"; then zeg "goed: zeven ongeldige op rij stoppen"; else mislukt "zeven ongeldige op rij stopten niet"; fi
teruggezet "ab2 na zeven ongeldige op rij"

# --- 4. terugzetten, weigeren, standaard-uitvoer --------------------------------------------------
# SIGTERM midden in een start: de proef-DLL staat dan in engine en view.
opnieuw
printf '%s\n' draait draait draait > "$S/plan"
echo 1 > "$S/breek-af"
nep env "${ABENV[@]}" DOEL=1 bash "$AB" > "$W/ab4.uit" 2>&1
gelijk "ab2 na SIGTERM: exitcode" 143 "$?"
teruggezet "ab2 na SIGTERM"

# Er draait al een game: weigeren, niets wisselen, de daemon niet herstarten.
opnieuw
echo draait > "$S/status"
printf '%s\n' draait > "$S/plan"
nep env "${ABENV[@]}" DOEL=1 bash "$AB" > "$W/ab5.uit" 2>&1
gelijk "ab2 weigert bij een lopende game: exitcode" 3 "$?"
if grep -q "systemctl" "$S/aanroepen" 2>/dev/null; then mislukt "ab2 herstartte de daemon terwijl er een game draaide"; else zeg "goed: geen herstart bij een lopende game"; fi
if grep -q "game start" "$S/aanroepen" 2>/dev/null; then mislukt "ab2 startte toch"; fi
teruggezet "ab2 na weigeren"

# Ontbrekende NEW/NOSTRIP: duidelijke fout, niets aangeraakt.
opnieuw
nep env RG_AB_UIT="$W/uit" bash "$AB" > "$W/ab6.uit" 2>&1
gelijk "ab2 zonder RG_AB_NEW/RG_AB_NOSTRIP: exitcode" 2 "$?"
if grep -q "zet RG_AB_NEW" "$W/ab6.uit" && grep -q "zet RG_AB_NOSTRIP" "$W/ab6.uit"; then zeg "goed: de fout noemt RG_AB_NEW en RG_AB_NOSTRIP"; else mislukt "geen duidelijke fout zonder RG_AB_NEW/RG_AB_NOSTRIP"; fi
gelijk "engine onaangeroerd" ORIGINEEL-ENGINE "$(cat "$ENG")"

# De standaard-uitvoer ligt in de uit-map van de repo, niet in /tmp.
if grep -v '^ *#' "$AB" "$MUT" "$ROOT/tools/dev/ab-rhio/gemeen.sh" "$ROOT/tools/dev/ab-rhio/maak-tabel.py" | grep -q '/tmp'; then
    mislukt "een A/B-script noemt nog /tmp"
else
    zeg "goed: geen /tmp in de A/B-scripts"
fi
gelijk "standaard-uitmap" "$ROOT/tools/rg-rhio/out/ab-rhio" \
    "$(bash -c '. "$1"; echo "$AB_UIT_STANDAARD"' _ "$ROOT/tools/dev/ab-rhio/gemeen.sh")"

# De foto-band: BOOTING en bureaublad tellen niet.
gelijk "foto-band" "geen-foto nee ja nee" \
    "$(bash -c '. "$1"; for s in 0 23408 300000 1460000; do foto_oordeel $s; done' _ "$ROOT/tools/dev/ab-rhio/gemeen.sh" | xargs)"

# mutex-hermeten.sh: per variant doornummeren (ook na een afbreking tussen UIT en AAN), bureaublad = nee,
# marker en DLL's terug.
opnieuw
mkdir -p "$W/uit"
echo "UIT 1 01a873d1 23408 nee ja 2922 0" > "$W/uit/mutex-resultaten.txt"
printf '%s\n' draait draait draait draait > "$S/plan"
echo 1460000 > "$S/foto"
nep env RG_AB_UIT="$W/uit" bash "$MUT" "$W/dll/new.dll" 2 > "$W/mut.uit" 2>&1
gelijk "mutex-hermeten loopt af" 0 "$?"
gelijk "rondes per variant" "UIT1 UIT2 AAN1 UIT3 AAN2" "$(awk '{printf "%s%s ", $1, $2}' "$W/uit/mutex-resultaten.txt" | xargs)"
gelijk "bureaublad-foto telt niet als voorbij BOOTING" "nee nee nee nee" "$(awk 'NR>1 {print $5}' "$W/uit/mutex-resultaten.txt" | xargs)"
[ ! -e "$VIEWMAP/rg-rhio-mutex.aan" ] || mislukt "de mutex-marker staat er nog"
teruggezet "mutex-hermeten"
# En SIGTERM midden in een AAN-start (de tweede wachttijd: eerst UIT, dan AAN): marker weg, DLL's terug.
opnieuw
printf '%s\n' draait draait > "$S/plan"
echo 2 > "$S/breek-af"
nep env RG_AB_UIT="$W/uit" bash "$MUT" "$W/dll/new.dll" 1 > "$W/mut2.uit" 2>&1
gelijk "mutex-hermeten na SIGTERM: exitcode" 143 "$?"
[ ! -e "$VIEWMAP/rg-rhio-mutex.aan" ] || mislukt "de mutex-marker staat er na SIGTERM nog"
teruggezet "mutex-hermeten na SIGTERM"

if [ "$fout" != 0 ]; then zeg "mislukt"; exit 1; fi
zeg "alles goed"

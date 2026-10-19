#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Proeven op de LOPER van [Launch] PrefixSetup (review 13, punt 6). De motor-kant (welke stappen mogen
# en welke niet) staat in tests/test_direct_launch.cpp; hier gaat het om wat rg-direct zelf doet:
#
#   1. een kopieerdoel door een koppeling wordt geweigerd;
#   2. een kopieerdoel op de (nagebootste) NAS wordt geweigerd;
#   3. een mislukte stap stopt de start, de latere stappen lopen niet en de stempel komt er NIET;
#   4. een stap die blijft hangen wordt na de tijd afgebroken;
#   5. de stempel: de tweede keer overslaan, en een ontbrekend doel opnieuw kopieren.
#
# We nemen een ECHT plan (rg-direct --plan MarioKartDX) en vervangen alleen de velden die we willen
# hebben: zo blijft het planschema van de motor de baas en hoeft de proef het niet na te bouwen.
set -uo pipefail
ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
RG="${2:-$ROOT/build/rg-direct}"
cd "$ROOT" || exit 2
[ -x "$RG" ] || { echo "prefixstappen: $RG ontbreekt (bouw eerst)"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "prefixstappen: python3 ontbreekt"; exit 77; }

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fout=0
zeg() { echo "prefixstappen: $*"; }
mislukt() { zeg "FOUT: $*"; fout=1; }

# Een nagebootste NAS: /proc/self/mounts wordt nagebootst met een tmpfs op $W/nas.
NAS="$W/nas"
mkdir -p "$NAS"
MOUNTS="$W/mounts"
# cifs: on_nas() telt alleen netwerkbestandssystemen (cifs/smb3/nfs/...) en paden onder ~/mnt.
printf 'proc /proc proc rw 0 0\n%s %s cifs rw 0 0\n' "$NAS" "$NAS" > "$MOUNTS"

# Een nagebootste wine: doet niets en blijft hangen (voor de time-out) of eindigt met een status.
FAKEWINE="$W/nep-wine"
cat > "$FAKEWINE" <<'SH'
#!/bin/bash
# $1 = het "programma" (een Windows-pad); de rest zijn argumenten.
# De stap krijgt de omgeving uit het PLAN, niet die van de aanroeper: daarom kiest deze nep-wine op
# zijn argument (het "programma").
case "${1:-}" in
    *hang*)    sleep 60;;      # hangende stap
    *mislukt*) exit 3;;        # mislukte stap
esac
exit 0
SH
chmod +x "$FAKEWINE"

PLAN="$W/plan.json"
"$RG" --plan MarioKartDX --patches patches > "$PLAN" 2>/dev/null || { zeg "$RG --plan lukte niet"; exit 77; }

maak_plan() {   # $1 = soort, $2 = doelpad, $3 = van-pad
    python3 - "$PLAN" "$1" "$2" "$3" "$FAKEWINE" "$W" <<'PY'
import json, sys
plan, soort, naar, van, wine, w = sys.argv[1:7]
d = json.load(open(plan))
p = d.get('plan', d)
p['wine'] = wine
p['prefix_setup_mark'] = w + '/prefixstempel.done'
p['view_writable'] = [w + '/schrijfmap']
stappen = []
if soort == 'copy':
    stappen.append({'copy': True, 'tekst': 'copy ' + van + ' ' + naar, 'from': van, 'to': naar, 'cwd': '', 'argv': []})
elif soort == 'twee':
    stappen.append({'copy': True, 'tekst': 'copy een', 'from': van, 'to': naar, 'cwd': '', 'argv': []})
    stappen.append({'copy': True, 'tekst': 'copy twee (mislukt)',
                    'from': w + '/bestaat-niet', 'to': naar + '.twee', 'cwd': '', 'argv': []})
elif soort == 'run':
    stappen.append({'copy': False, 'tekst': 'run ' + naar, 'from': '', 'to': '',
                    'cwd': w + '/schrijfmap', 'argv': [wine, 'Z:\\hang.exe']})
p['prefix_setup'] = stappen
# plan_from_json leest het BESTAND als het plan zelf (niet de wikkel met verdict/input eromheen).
json.dump(p, open(plan, 'w'))
PY
}

mkdir -p "$W/schrijfmap"
printf 'inhoud-van-de-bron\n' > "$W/bron.ini"

# --- 1. doel door een koppeling ---------------------------------------------------------------
# 1a. koppeling naar een bestaande map
mkdir -p "$W/ergens-heen"
ln -sfn "$W/ergens-heen" "$W/schrijfmap/koppeling"
maak_plan copy "$W/schrijfmap/koppeling/doel.ini" "$W/bron.ini"
uit=$("$RG" --alleen-stappen "$PLAN" 2>&1); rc=$?
case "$uit" in *koppeling*) zeg "doel door een koppeling geweigerd (goed)";; *) mislukt "koppeling niet geweigerd: $uit";; esac
[ "$rc" != 0 ] || mislukt "een geweigerde stap gaf toch status 0"
[ ! -e "$W/ergens-heen/doel.ini" ] || mislukt "er is door de koppeling heen geschreven"
# 1b. een koppeling waarvan het doel ontbreekt (on_nas hoort dicht te klappen)
ln -sfn "$W/bestaat-niet" "$W/schrijfmap/losse-koppeling"
maak_plan copy "$W/schrijfmap/losse-koppeling/doel.ini" "$W/bron.ini"
uit=$("$RG" --alleen-stappen "$PLAN" 2>&1); rc=$?
[ "$rc" != 0 ] || mislukt "een losse koppeling werd niet geweigerd: $uit"
zeg "losse koppeling ook geweigerd (goed)"

# --- 2. doel op de nagebootste NAS -------------------------------------------------------------
maak_plan copy "$NAS/doel.ini" "$W/bron.ini"
uit=$(RAVENGUN_MOUNTS="$MOUNTS" "$RG" --alleen-stappen "$PLAN" 2>&1); rc=$?
case "$uit" in *NAS*) zeg "doel op de NAS geweigerd (goed)";; *) mislukt "NAS-doel niet geweigerd: $uit";; esac
[ ! -e "$NAS/doel.ini" ] || mislukt "er is toch op de NAS geschreven"

# --- 3. mislukte stap: latere stap loopt niet, geen stempel -------------------------------------
maak_plan twee "$W/schrijfmap/een.ini" "$W/bron.ini"
uit=$("$RG" --alleen-stappen "$PLAN" 2>&1); rc=$?
[ "$rc" != 0 ] || mislukt "een mislukte stap gaf status 0"
[ -e "$W/schrijfmap/een.ini" ] || mislukt "de eerste stap heeft niet gekopieerd"
[ -e "$W/schrijfmap/een.ini.twee" ] && mislukt "de tweede stap liep toch door"
[ -e "$W/prefixstempel.done" ] && mislukt "de stempel is geschreven ondanks de mislukte stap"
zeg "mislukte stap: gestopt, geen stempel, latere stap niet gelopen (goed)"

# --- 4. hangende stap wordt afgebroken ----------------------------------------------------------
maak_plan run "$W/schrijfmap/nep.exe" ""
begin=$(date +%s)
uit=$(RG_PREFIXSTAP_TIMEOUT=3 "$RG" --alleen-stappen "$PLAN" 2>&1); rc=$?
duur=$(( $(date +%s) - begin ))
[ "$rc" != 0 ] || mislukt "een hangende stap gaf status 0"
[ "$duur" -lt 30 ] || mislukt "de hangende stap duurde $duur s (time-out werkte niet)"
case "$uit" in *"afgebroken"*|*"procesgroep"*) zeg "hangende stap na ${duur}s afgebroken (goed)";; *) mislukt "geen afbreek-regel: $uit";; esac

# --- 5. stempel: tweede keer overslaan, ontbrekend doel opnieuw ---------------------------------
maak_plan copy "$W/schrijfmap/klaar.ini" "$W/bron.ini"
uit=$("$RG" --alleen-stappen "$PLAN" 2>&1); rc=$?
[ "$rc" = 0 ] || mislukt "de gelukkige stap gaf status $rc: $uit"
[ -e "$W/schrijfmap/klaar.ini" ] || mislukt "het doel is niet geschreven"
[ -e "$W/prefixstempel.done" ] || mislukt "de stempel is niet geschreven"
cmp -s "$W/bron.ini" "$W/schrijfmap/klaar.ini" || mislukt "de inhoud klopt niet"
uit=$("$RG" --alleen-stappen "$PLAN" 2>&1); rc=$?
case "$uit" in *"al gedaan"*) zeg "tweede keer overgeslagen via de stempel (goed)";; *) mislukt "geen overslaan-regel: $uit";; esac
rm -f "$W/schrijfmap/klaar.ini"
uit=$("$RG" --alleen-stappen "$PLAN" 2>&1); rc=$?
[ -e "$W/schrijfmap/klaar.ini" ] || mislukt "een ontbrekend doel werd niet opnieuw gekopieerd"
zeg "ontbrekend doel opnieuw gekopieerd ondanks de stempel (goed)"

# --- 6. niets op de NAS ------------------------------------------------------------------------
rest=$(find "$NAS" -type f 2>/dev/null | wc -l)
[ "$rest" = 0 ] || { mislukt "er staan bestanden op de nagebootste NAS"; find "$NAS" -type f | head -3; }

if [ "$fout" != 0 ]; then zeg "mislukt"; exit 1; fi
zeg "alles goed"

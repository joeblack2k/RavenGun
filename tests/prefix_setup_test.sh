#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Proeven op de LOPER van [Launch] PrefixSetup (review 13, punt 6; ronde 2). De motor-kant (welke stappen mogen
# en welke niet, en de controle prefixstappen_problemen zelf) staat in tests/test_direct_launch.cpp; hier gaat het
# om wat rg-direct doet, via --alleen-stappen (dezelfde controle vooraf als --run: controle_voor_start):
#
#   0. een plan als dat van Mario Kart DX (view-wortel vol koppelingen naar de NAS) gaat door de controle en loopt;
#   1. een kopieerdoel door een koppeling wordt geweigerd;
#   2. een kopieerdoel op de (nagebootste) NAS wordt geweigerd, ook een koppelpunt in de view;
#   3. een mislukte stap stopt de start, de latere stappen lopen niet en de stempel komt er NIET; de laatste
#      regels van de stap (de foutmelding) staan in het logboek;
#   4. een stap die blijft hangen wordt na de tijd afgebroken: de hele procesgroep is weg en wineserver -k is
#      aangeroepen met de goede WINEPREFIX (ook via de wrapper wine-prefix);
#   5. de stempel: de tweede keer overslaan, en een ontbrekend doel opnieuw kopieren;
#   6. wat een geslaagde stap laat doorlopen, gaat dood voor de volgende stap;
#   7. een werkmap achter een koppeling weigert, ook als een eerdere stap hem pas zo maakt;
#   8. prefix: met .. of een leidende schuine streep (met de hand bewerkt plan) weigert;
#   9. een schrijfmap die nog een koppeling naar de NAS is weigert (een kale start; de daemon zet hem eerst om);
#  10. een lege koppelpuntentabel (RAVENGUN_MOUNTS) weigert;
#  11. SIGTERM stopt een lopende stap;
#  12. op de NAS is niets veranderd.
#
# Alles in een tijdelijke map met een eigen plan, een nep-wine en een nagebootste NAS (cifs in een eigen
# koppelpuntentabel): de proef hangt niet af van de opstelling van deze machine en slaat nergens over.
set -uo pipefail
ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
RG="${2:-$ROOT/build/rg-direct}"
[ -x "$RG" ] || { echo "prefixstappen: $RG ontbreekt (bouw eerst)"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "prefixstappen: python3 ontbreekt"; exit 77; }

W="$(mktemp -d)"
trap 'pkill -KILL -f "$W/" 2>/dev/null; rm -rf "$W"' EXIT
fout=0
zeg() { echo "prefixstappen: $*"; }
mislukt() { zeg "FOUT: $*"; fout=1; }
leeft() { [ -n "${1:-}" ] && kill -0 "$1" 2>/dev/null; }

# --- de opstelling ------------------------------------------------------------------------------
VIEWS="$W/data/ravengun/runtime/views"
V="$VIEWS/MK"
NAS="$W/nas"
PFX="$W/prefix"
MOUNTS="$W/mounts"
# cifs: on_nas() telt alleen netwerkbestandssystemen (cifs/smb3/nfs/...) en paden onder ~/mnt.
printf 'proc /proc proc rw 0 0\n//nas/emu %s cifs rw 0 0\n' "$NAS" > "$MOUNTS"
mkdir -p "$V/AMCUS" "$NAS/MK/Mucha" "$NAS/MK/AMCUS" "$PFX" "$W/home" "$W/elders"
printf 'MZ' > "$NAS/MK/PreprocessAmAuth.exe"
printf 'MZ' > "$NAS/MK/D3D11Ref.dll"
printf 'nas\n' > "$NAS/MK/Mucha/x.bin"
printf 'nas\n' > "$NAS/MK/AMCUS/AMConfig.ini"
printf 'MZ' > "$V/spel.exe"
# De view-wortel zoals bij Mario Kart DX: vol koppelingen naar de NAS.
for n in PreprocessAmAuth.exe D3D11Ref.dll Mucha; do ln -s "$NAS/MK/$n" "$V/$n"; done
nas_voor=$(cd "$NAS" && find . -type f -print0 | sort -z | xargs -0 sha256sum)

# Een nagebootste wine. De stap krijgt de omgeving uit het PLAN, niet die van de aanroeper: daarom kiest
# deze nep-wine op zijn argument (het "programma") en staan de paden van de proef er letterlijk in.
maak_wine() {   # $1 = map, $2 = logboek van wineserver
    mkdir -p "$1"
    cat > "$1/wine" <<SH
#!/bin/bash
case "\${1:-}" in
    *preprocess*)  mkdir -p "\$WINEPREFIX/drive_c/windows"
                   printf '[AMAuth]\nok=1\n' > "\$WINEPREFIX/drive_c/windows/WritableConfig.ini"
                   echo "preprocess klaar";;
    *regsvr32*)    echo "regsvr32 \$* (\$PWD)" >> "$W/regsvr32.log";;
    *hang*)        sleep 60 & echo \$! > "$W/wees.pid"; sleep 60;;
    *achterblijver*) sleep 60 & echo \$! > "$W/achter.pid"; exit 0;;
    *stempelkoppeling*) rm -rf "\$WINEPREFIX/.ravengun-prefixsetup"
                   ln -s "$NAS/stempels" "\$WINEPREFIX/.ravengun-prefixsetup";;
    *verbuig*)     rm -rf "$V/AMCUS/werk"; ln -s "$W/elders" "$V/AMCUS/werk";;
    *teller*)      echo gelopen >> "$W/gelopen.log";;
    *mislukt*)     for i in \$(seq 1 300); do echo "regel \$i"; done; echo "FOUTMELDING-LAATSTE"; exit 3;;
esac
exit 0
SH
    cat > "$1/wineserver" <<SH
#!/bin/bash
echo "wineserver \$* WINEPREFIX=\${WINEPREFIX:-}" >> "$2"
SH
    chmod +x "$1/wine" "$1/wineserver"
}
maak_wine "$W/bin" "$W/wineserver.log"
maak_wine "$W/bin2" "$W/wineserver2.log"
# De wrapper zoals scripts/wine-prefix.sh: naast hem staat GEEN wineserver.
mkdir -p "$W/wrap"
cat > "$W/wrap/wine-prefix" <<'SH'
#!/usr/bin/env bash
REAL="${RG_WINE:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
export WINEPREFIX="${RG_WINEPREFIX:?RG_WINEPREFIX niet gezet}"
export WINESERVER="$(dirname "$REAL")/wineserver"
exec "$REAL" "$@"
SH
chmod +x "$W/wrap/wine-prefix"

PLAN="$W/plan.json"
STEMPEL="$PFX/.ravengun-prefixsetup/proef.done"
# $1 = de stappen als JSON: {"run": <werkmap in de view>, "prog": <naam>, "args": [...]} of
# {"copy": <doel in de view>, "prefix": <pad in de prefix>} of {"copy": ..., "van": <absoluut pad>}.
# $2 = "wrapper" voor de wrapper wine-prefix.
maak_plan() {
    python3 -I - "$PLAN" "$1" "${2:-}" "$W" "$V" "${3:-$PFX}" "$STEMPEL" <<'PY'
import json, sys
plan, stappen, soort, w, v, pfx, stempel = sys.argv[1:8]
if soort == "leegprefix":
    # Geen prefix in het plan: de motor haalt hem dan uit de omgeving. Voor de stempelcontrole en de
    # prefix:-bron moet dat DEZELFDE bron zijn (review 15, punt 4).
    wine = w + "/bin/wine"
    env = {"PATH": "/usr/bin:/bin", "HOME": w + "/home"}
    if pfx:
        env["WINEPREFIX"] = pfx
    prefix = ""
elif soort == "wrapper":
    wine = w + "/wrap/wine-prefix"
    env = {"PATH": "/usr/bin:/bin", "HOME": w + "/home", "RG_WINE": w + "/bin2/wine", "RG_WINEPREFIX": w + "/prefix2"}
    prefix = w + "/prefix2"
else:
    wine = w + "/bin/wine"
    env = {"PATH": "/usr/bin:/bin", "HOME": w + "/home", "WINEPREFIX": pfx}
    prefix = pfx
uit = []
for s in json.loads(stappen):
    if "run" in s:
        uit.append({"copy": False, "cwd": v + "/" + s["run"], "argv": [wine, "Z:\\" + s["prog"] + ".exe"] + s.get("args", []),
                    "from": "", "from_prefix": False, "from_rel": "", "to": "", "tekst": "run " + s["run"] + " " + s["prog"]})
    elif "prefix" in s:
        uit.append({"copy": True, "cwd": "", "argv": [], "from": prefix + "/" + s["prefix"], "from_prefix": True,
                    "from_rel": s["prefix"], "to": v + "/" + s["copy"], "tekst": "copy prefix:" + s["prefix"] + " " + s["copy"]})
    else:
        uit.append({"copy": True, "cwd": "", "argv": [], "from": s["van"], "from_prefix": False, "from_rel": "",
                    "to": v + "/" + s["copy"], "tekst": "copy " + s["van"] + " " + s["copy"]})
p = {"profile": "MK", "game": v + "/spel.exe", "cwd": v, "wine": wine, "prefix": prefix, "pipehelper": "pipehelper.exe",
     "argv": [wine, "spel.exe"], "env": env, "view_writable": [v + "/AMCUS"],
     # De stempel hoort in DEZELFDE prefix te liggen als waarin de stappen lopen (review 14, punt 3);
     # de wrapper-zaak draait in prefix2 en niet in PFX.
     "prefix_setup_mark": (prefix + "/.ravengun-prefixsetup/proef.done" if soort == "wrapper" else stempel),
     "prefix_setup": uit}
json.dump(p, open(plan, "w"))
PY
}
# rg-direct --alleen-stappen met de omgeving van de proef; extra NAAM=waarde vooraan mag.
stappen() { env HOME="$W/home" XDG_DATA_HOME="$W/data" RAVENGUN_MOUNTS="$MOUNTS" "$@" "$RG" --alleen-stappen "$PLAN" 2>&1; }
MK_STAPPEN='[{"run": ".", "prog": "preprocess", "args": ["-wait"]},
             {"copy": "WritableConfig.ini", "prefix": "drive_c/windows/WritableConfig.ini"},
             {"copy": "AMCUS/WritableConfig.ini", "prefix": "drive_c/windows/WritableConfig.ini"},
             {"run": "AMCUS", "prog": "regsvr32", "args": ["/s", "iauthdll.dll"]}]'

# --- 0. het plan van Mario Kart DX: door de controle en helemaal gelopen (N1) ---------------------
maak_plan "$MK_STAPPEN"
uit=$(stappen); rc=$?
[ "$rc" = 0 ] || mislukt "het MK-plan gaf status $rc: $uit"
for d in "$V/WritableConfig.ini" "$V/AMCUS/WritableConfig.ini"; do
    cmp -s "$PFX/drive_c/windows/WritableConfig.ini" "$d" || mislukt "$d is niet de WritableConfig.ini uit de prefix"
done
grep -q "iauthdll.dll ($V/AMCUS)" "$W/regsvr32.log" 2>/dev/null || mislukt "regsvr32 liep niet in AMCUS"
[ -e "$STEMPEL" ] || mislukt "de stempel is niet geschreven"
[ "$fout" = 0 ] && zeg "MK-plan met koppelingen naar de NAS in de view-wortel: door de controle en gelopen (goed)"
rm -rf "$PFX/.ravengun-prefixsetup" "$V/WritableConfig.ini" "$V/AMCUS/WritableConfig.ini"

printf 'inhoud-van-de-bron\n' > "$V/bron.ini"

# --- 1. doel door een koppeling ---------------------------------------------------------------
mkdir -p "$W/ergens-heen"
ln -sfn "$W/ergens-heen" "$V/AMCUS/koppeling"
maak_plan '[{"copy": "AMCUS/koppeling/doel.ini", "van": "'"$V"'/bron.ini"}]'
uit=$(stappen); rc=$?
case "$uit" in *koppeling*) zeg "doel door een koppeling geweigerd (goed)";; *) mislukt "koppeling niet geweigerd: $uit";; esac
[ "$rc" != 0 ] || mislukt "een geweigerde stap gaf toch status 0"
[ ! -e "$W/ergens-heen/doel.ini" ] || mislukt "er is door de koppeling heen geschreven"
rm -f "$V/AMCUS/koppeling"
# 1b. een koppeling waarvan het doel ontbreekt
ln -sfn "$W/bestaat-niet" "$V/AMCUS/losse-koppeling"
maak_plan '[{"copy": "AMCUS/losse-koppeling/doel.ini", "van": "'"$V"'/bron.ini"}]'
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een losse koppeling werd niet geweigerd: $uit"
[ ! -e "$W/bestaat-niet" ] || mislukt "de losse koppeling is gevolgd"
rm -f "$V/AMCUS/losse-koppeling"
zeg "losse koppeling ook geweigerd (goed)"

# --- 2. doel op de nagebootste NAS -------------------------------------------------------------
maak_plan '[{"copy": "Mucha/doel.ini", "van": "'"$V"'/bron.ini"}]'
uit=$(stappen); rc=$?
case "$uit" in *"Mucha is een koppeling"*) zeg "doel in een NAS-koppeling geweigerd (goed)";; *) mislukt "NAS-doel niet geweigerd: $uit";; esac
[ "$rc" != 0 ] || mislukt "NAS-doel gaf status 0"
[ ! -e "$NAS/MK/Mucha/doel.ini" ] || mislukt "er is toch op de NAS geschreven"
# 2b. een echte map in de view waar de NAS op gekoppeld is
mkdir -p "$V/koppelpunt"
printf 'proc /proc proc rw 0 0\n//nas/emu %s cifs rw 0 0\n//nas/kp %s cifs rw 0 0\n' "$NAS" "$V/koppelpunt" > "$W/mounts-kp"
maak_plan '[{"copy": "koppelpunt/doel.ini", "van": "'"$V"'/bron.ini"}]'
uit=$(stappen RAVENGUN_MOUNTS="$W/mounts-kp"); rc=$?
case "$uit" in *"op de NAS"*) zeg "doel in een koppelpunt van de NAS geweigerd (goed)";; *) mislukt "koppelpunt niet geweigerd: $uit";; esac
[ ! -e "$V/koppelpunt/doel.ini" ] || mislukt "er is in het koppelpunt geschreven"

# --- 3. mislukte stap: latere stap loopt niet, geen stempel, de foutmelding staat er ----------
maak_plan '[{"copy": "AMCUS/een.ini", "van": "'"$V"'/bron.ini"},
            {"copy": "AMCUS/twee.ini", "van": "'"$V"'/bestaat-niet"},
            {"copy": "AMCUS/drie.ini", "van": "'"$V"'/bron.ini"}]'
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een mislukte stap gaf status 0"
[ -e "$V/AMCUS/een.ini" ] || mislukt "de eerste stap heeft niet gekopieerd"
[ -e "$V/AMCUS/drie.ini" ] && mislukt "de derde stap liep toch door"
[ -e "$STEMPEL" ] && mislukt "de stempel is geschreven ondanks de mislukte stap"
maak_plan '[{"run": ".", "prog": "mislukt"}, {"run": ".", "prog": "teller"}]'
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een run-stap met status 3 gaf status 0"
case "$uit" in *FOUTMELDING-LAATSTE*) ;; *) mislukt "de laatste regel van de stap ontbreekt in het logboek (N6)";; esac
[ ! -e "$W/gelopen.log" ] || mislukt "de stap na de mislukte run-stap liep toch"
[ "$fout" = 0 ] && zeg "mislukte stap: gestopt, geen stempel, latere stap niet gelopen, laatste regel in het logboek (goed)"

# --- 4. hangende stap: afgebroken, procesgroep weg, wineserver -k met de goede prefix ---------
maak_plan '[{"run": ".", "prog": "hang"}]'
rm -f "$W/wees.pid" "$W/wineserver.log"
begin=$(date +%s)
uit=$(stappen RG_PREFIXSTAP_TIMEOUT=2); rc=$?
duur=$(( $(date +%s) - begin ))
[ "$rc" != 0 ] || mislukt "een hangende stap gaf status 0"
[ "$duur" -lt 30 ] || mislukt "de hangende stap duurde $duur s (time-out werkte niet)"
case "$uit" in *"afgebroken"*) ;; *) mislukt "geen afbreek-regel: $uit";; esac
wees=$(cat "$W/wees.pid" 2>/dev/null)
[ -n "$wees" ] || mislukt "de nep-wine schreef geen pid van zijn kind"
leeft "$wees" && mislukt "het kind van de stap ($wees) leeft nog na de time-out"
grep -q -- "-k WINEPREFIX=$PFX\$" "$W/wineserver.log" 2>/dev/null || mislukt "wineserver -k niet aangeroepen met WINEPREFIX=$PFX: $(cat "$W/wineserver.log" 2>/dev/null)"
[ "$fout" = 0 ] && zeg "hangende stap na ${duur}s afgebroken, kind weg, wineserver -k met de prefix (goed)"
# 4b. via de wrapper wine-prefix: de wineserver naast RG_WINE, met RG_WINEPREFIX
maak_plan '[{"run": ".", "prog": "hang"}]' wrapper
rm -f "$W/wees.pid" "$W/wineserver.log" "$W/wineserver2.log"
uit=$(stappen RG_PREFIXSTAP_TIMEOUT=2); rc=$?
[ "$rc" != 0 ] || mislukt "wrapper: een hangende stap gaf status 0"
leeft "$(cat "$W/wees.pid" 2>/dev/null)" && mislukt "wrapper: het kind van de stap leeft nog"
grep -q -- "-k WINEPREFIX=$W/prefix2\$" "$W/wineserver2.log" 2>/dev/null || mislukt "wrapper: geen wineserver -k naast RG_WINE met RG_WINEPREFIX: $uit"
[ ! -e "$W/wineserver.log" ] || mislukt "wrapper: de verkeerde wineserver is aangeroepen"
[ "$fout" = 0 ] && zeg "wrapper wine-prefix: wineserver -k naast RG_WINE met RG_WINEPREFIX (goed)"

# --- 5. stempel: tweede keer overslaan, ontbrekend doel opnieuw ---------------------------------
maak_plan '[{"copy": "AMCUS/klaar.ini", "van": "'"$V"'/bron.ini"}]'
uit=$(stappen); rc=$?
[ "$rc" = 0 ] || mislukt "de gelukkige stap gaf status $rc: $uit"
[ -e "$STEMPEL" ] || mislukt "de stempel is niet geschreven"
cmp -s "$V/bron.ini" "$V/AMCUS/klaar.ini" || mislukt "de inhoud klopt niet"
uit=$(stappen); rc=$?
case "$uit" in *"al gedaan"*) zeg "tweede keer overgeslagen via de stempel (goed)";; *) mislukt "geen overslaan-regel: $uit";; esac
rm -f "$V/AMCUS/klaar.ini"
uit=$(stappen); rc=$?
[ -e "$V/AMCUS/klaar.ini" ] || mislukt "een ontbrekend doel werd niet opnieuw gekopieerd"
zeg "ontbrekend doel opnieuw gekopieerd ondanks de stempel (goed)"
rm -rf "$PFX/.ravengun-prefixsetup"

# --- 6. wat een geslaagde stap laat doorlopen, gaat dood voor de volgende stap ------------------
maak_plan '[{"run": ".", "prog": "achterblijver"}, {"copy": "AMCUS/na.ini", "van": "'"$V"'/bron.ini"}]'
rm -f "$W/achter.pid"
uit=$(stappen RG_PREFIXSTAP_NALOOP=1); rc=$?
[ "$rc" = 0 ] || mislukt "achterblijver: status $rc: $uit"
leeft "$(cat "$W/achter.pid" 2>/dev/null)" && mislukt "het achtergebleven proces van de stap leeft nog"
case "$uit" in *"draait nog iets van de stap"*) ;; *) mislukt "geen regel over het achtergebleven proces: $uit";; esac
[ -e "$V/AMCUS/na.ini" ] || mislukt "de stap na de achterblijver liep niet"
[ "$fout" = 0 ] && zeg "achtergebleven proces van een geslaagde stap gestopt voor de volgende stap (goed)"
rm -rf "$PFX/.ravengun-prefixsetup"

# --- 7. werkmap achter een koppeling ------------------------------------------------------------
ln -sfn "$W/elders" "$V/AMCUS/werk"
maak_plan '[{"run": "AMCUS/werk", "prog": "teller"}]'
rm -f "$W/gelopen.log"
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een werkmap achter een koppeling gaf status 0"
[ ! -e "$W/gelopen.log" ] || mislukt "de stap liep toch in een werkmap achter een koppeling"
# 7b. de werkmap is bij de controle vooraf een echte map, maar een eerdere stap maakt er een koppeling van:
# de loper kijkt vlak voor het starten opnieuw.
rm -f "$V/AMCUS/werk"; mkdir -p "$V/AMCUS/werk"
maak_plan '[{"run": ".", "prog": "verbuig"}, {"run": "AMCUS/werk", "prog": "teller"}]'
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een werkmap die onderweg een koppeling werd gaf status 0"
case "$uit" in *"prefixstap: GEWEIGERD"*) ;; *) mislukt "de loper weigerde niet vlak voor het starten: $uit";; esac
[ ! -e "$W/gelopen.log" ] || mislukt "de stap liep toch in de werkmap die een koppeling werd"
[ "$fout" = 0 ] && zeg "werkmap achter een koppeling geweigerd, ook als een eerdere stap hem zo maakt (goed)"
rm -f "$V/AMCUS/werk"

# --- 8. prefix: met .. of een leidende schuine streep ------------------------------------------
for rel in "../../etc/passwd" "/etc/passwd"; do
    maak_plan '[{"copy": "AMCUS/p.ini", "prefix": "'"$rel"'"}]'
    uit=$(stappen); rc=$?
    [ "$rc" != 0 ] || mislukt "prefix:$rel gaf status 0"
    [ ! -e "$V/AMCUS/p.ini" ] || mislukt "prefix:$rel is gekopieerd"
done
zeg "prefix: met .. of / in een bewerkt plan geweigerd (goed)"

# --- 9. schrijfmap nog een koppeling naar de NAS (N2, een kale start) -------------------------
mv "$V/AMCUS" "$W/amcus-echt"
ln -s "$NAS/MK/AMCUS" "$V/AMCUS"
maak_plan "$MK_STAPPEN"
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een schrijfmap die een koppeling naar de NAS is gaf status 0"
case "$uit" in *"weigert"*) zeg "schrijfmap nog een NAS-koppeling: geweigerd voor er iets loopt (goed)";; *) mislukt "geen weigering: $uit";; esac
[ ! -e "$NAS/MK/AMCUS/WritableConfig.ini" ] || mislukt "er is via de schrijfmap op de NAS geschreven"
rm -f "$V/AMCUS"; mv "$W/amcus-echt" "$V/AMCUS"

# --- 10. lege of onleesbare koppelpuntentabel -----------------------------------------------------
: > "$W/mounts-leeg"
maak_plan "$MK_STAPPEN"
for m in "$W/mounts-leeg" "$W/mounts-bestaat-niet"; do
    uit=$(stappen RAVENGUN_MOUNTS="$m"); rc=$?
    [ "$rc" != 0 ] || mislukt "koppelpuntentabel $m gaf status 0"
    case "$uit" in *koppelpuntentabel*) ;; *) mislukt "geen melding over de koppelpuntentabel $m: $uit";; esac
done
[ ! -e "$V/WritableConfig.ini" ] || mislukt "er liep toch een stap zonder koppelpuntentabel"
zeg "lege of onleesbare koppelpuntentabel weigert (goed)"

# --- 11. SIGTERM stopt een lopende stap ---------------------------------------------------------
maak_plan '[{"run": ".", "prog": "hang"}]'
rm -f "$W/wees.pid"
env HOME="$W/home" XDG_DATA_HOME="$W/data" RAVENGUN_MOUNTS="$MOUNTS" "$RG" --alleen-stappen "$PLAN" > "$W/sigterm.log" 2>&1 &
rgpid=$!
for _ in $(seq 1 50); do [ -s "$W/wees.pid" ] && break; sleep 0.1; done
kill -TERM "$rgpid"
for _ in $(seq 1 100); do leeft "$rgpid" || break; sleep 0.1; done
if leeft "$rgpid"; then mislukt "rg-direct --alleen-stappen stopt niet op SIGTERM"; kill -KILL "$rgpid"; fi
wait "$rgpid"; rc=$?
[ "$rc" != 0 ] || mislukt "SIGTERM gaf status 0"
grep -q "stop gevraagd" "$W/sigterm.log" || mislukt "geen regel 'stop gevraagd': $(cat "$W/sigterm.log")"
leeft "$(cat "$W/wees.pid" 2>/dev/null)" && mislukt "het kind van de stap leeft nog na SIGTERM"
[ "$fout" = 0 ] && zeg "SIGTERM stopt de stap en zijn procesgroep (goed)"

# --- 12. niets op de NAS ------------------------------------------------------------------------
nas_na=$(cd "$NAS" && find . -type f -print0 | sort -z | xargs -0 sha256sum)
[ "$nas_voor" = "$nas_na" ] || { mislukt "de nagebootste NAS is veranderd"; diff <(echo "$nas_voor") <(echo "$nas_na") | head -5; }

# --- 9. de stempel van de prefixstappen (review 15, punt 4) -------------------------------------
# 9a. de stempelmap wordt TIJDENS de stappen een koppeling naar de NAS. De controle bij de start ziet
# dat niet (toen was het nog een echte map); de controle vlak voor het schrijven moet hem pakken.
mkdir -p "$NAS/stempels"
rm -rf "$PFX/.ravengun-prefixsetup"
maak_plan '[{"run": ".", "prog": "stempelkoppeling"}, {"copy": "AMCUS/s.ini", "van": "'"$V/bron.ini"'"}]'
uit=$(stappen); rc=$?
[ "$rc" = 0 ] || mislukt "de stappen hoorden te lopen (de koppeling ontstaat pas onderweg): $uit"
[ -e "$V/AMCUS/s.ini" ] || mislukt "de kopieerstap is niet gelopen"
case "$uit" in *"stempel wordt NIET geschreven"*) ;; *) mislukt "de koppeling als stempelmap is niet gemeld: $uit";; esac
[ ! -e "$NAS/stempels/proef.done" ] || mislukt "de stempel is door de koppeling heen op de NAS geschreven"
zeg "stempelmap die onderweg een koppeling naar de NAS wordt: niet geschreven (goed)"

# 9b. een LOSSE koppeling als stempelmap (het doel bestaat niet): on_nas klapt dicht, dus weigeren.
rm -rf "$PFX/.ravengun-prefixsetup"
ln -sfn "$NAS/bestaat-niet" "$PFX/.ravengun-prefixsetup"
maak_plan '[{"copy": "AMCUS/t.ini", "van": "'"$W/bron.ini"'"}]'
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een losse koppeling als stempelmap gaf status 0"
[ ! -e "$V/AMCUS/t.ini" ] || mislukt "de stap liep toch met een losse koppeling als stempelmap"
zeg "losse koppeling als stempelmap: geweigerd (goed)"
rm -rf "$PFX/.ravengun-prefixsetup"

# 9c. leeg plan.prefix: de prefix komt uit de omgeving, en dat moet dezelfde bron zijn als de
# prefix:-bron van de loper. Geldig -> aannemen.
maak_plan '[{"run": ".", "prog": "preprocess"}, {"copy": "AMCUS/u.ini", "prefix": "drive_c/windows/WritableConfig.ini"}]' leegprefix
uit=$(stappen); rc=$?
[ "$rc" = 0 ] || mislukt "een leeg plan.prefix met een geldige WINEPREFIX werd geweigerd: $uit"
[ -e "$V/AMCUS/u.ini" ] || mislukt "de prefix:-bron is met een leeg plan.prefix niet gelezen"
[ -e "$STEMPEL" ] || mislukt "de stempel is met een leeg plan.prefix niet geschreven"
zeg "leeg plan.prefix met een geldige WINEPREFIX: bron gelezen en stempel geschreven (goed)"

# 9d. dezelfde zaak, maar de omgevingsprefix staat op de NAS: weigeren.
rm -f "$STEMPEL"
maak_plan '[{"copy": "AMCUS/v.ini", "van": "'"$W/bron.ini"'"}]' leegprefix "$NAS"
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een stempel in een NAS-prefix werd aangenomen"
zeg "leeg plan.prefix met een NAS-prefix: geweigerd (goed)"

# 9e. en zonder prefix in het plan EN zonder prefix in de omgeving: weigeren.
maak_plan '[{"copy": "AMCUS/w.ini", "van": "'"$W/bron.ini"'"}]' leegprefix ""
uit=$(stappen); rc=$?
[ "$rc" != 0 ] || mislukt "een plan zonder enige prefix werd aangenomen"
zeg "geen prefix in het plan en geen in de omgeving: geweigerd (goed)"

if [ "$fout" != 0 ]; then zeg "mislukt"; exit 1; fi
zeg "alles goed"

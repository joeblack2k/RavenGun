#!/usr/bin/env bash
# scripts/make-portable.sh op een kleine nagemaakte thuismap (geen echte runtime, geen 18 GB): koppelingen naar de runtime en de
# installatie (ook een keten), een koppeling naar buiten (de NAS: blijft zoals hij is), een mode-000-map, een afgebroken eerste
# run, een herhaalde run die User/ laat staan, --reset-user, een mislukte rsync die de oude boom ongemoeid laat, een
# koppeling die uit de map wijst en een koppeling naar ~/.local zonder doel (allebei: exitcode, geen User/). Verder: een keten van
# mapkoppelingen met koppelingen erin, koppelingen naar de wortel van de runtime/installatie/configuratie zelf, een toegestane cache
# (leeg) tegenover een te grote of half te kopiëren map (stop), overlay-whiteouts en -xattrs van de schrijflagen, de bronrechten
# (nooit u+w), een oude User/ die niet gescand of aangepast wordt, de profielen uit git HEAD en de padherschrijving (config + starter).
#
# Gebruik: make_portable_test.sh <repo> [builddir]    (de tijdelijke map komt in builddir, nooit in /tmp)
set -u
REPO="$(cd "$1" && pwd)"
BUILD="$(mkdir -p "${2:-$REPO/build}" && cd "${2:-$REPO/build}" && pwd)"
command -v python3 >/dev/null || { echo "overgeslagen: python3 ontbreekt"; exit 77; }
command -v rsync >/dev/null || { echo "overgeslagen: rsync ontbreekt"; exit 77; }
T="$(mktemp -d "$BUILD/make-portable-test.XXXXXX")" || exit 1
cleanup() { chmod -R u+rwX "$T" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
ok() { echo "ok: $*"; }

# De repo-kopie: het script leidt ROOT af uit zijn eigen plek; met een eigen ROOT ligt de nagemaakte NAS er niet in.
R="$T/repo"; mkdir -p "$R/scripts" "$R/tools/dev"
cp "$REPO/scripts/make-portable.sh" "$REPO/scripts/portable_tools.py" "$R/scripts/"
cp "$REPO/tools/dev/ship-patches.py" "$R/tools/dev/"
SCRIPT="$R/scripts/make-portable.sh"

# --- de nagemaakte thuismap ----------------------------------------------------------------------------------------------------------
H="$T/home"; NAS="$T/nas"
INS="$H/.local/opt/ravengun"; RUN="$H/.local/share/ravengun"; CFG="$H/.config/ravengun"
mkdir -p "$INS/bin" "$INS/lib" "$INS/share/ravengun" "$RUN/runtime" "$RUN/tp.verborgen/sub/dir" "$RUN/tp.verborgen/other" \
         "$H/.local/share/otherdir" "$CFG" "$NAS/game" "$T/fakebin"
printf '#!/bin/sh\n' > "$INS/bin/ravengund"; chmod 755 "$INS/bin/ravengund"
echo v1 > "$INS/bin/ravengun"; chmod 755 "$INS/bin/ravengun"; echo lib > "$INS/lib/libx.so"
echo echt > "$RUN/runtime/real.txt"
echo FOUT > "$RUN/tp.verborgen/file.txt"          # de valkuil: zelfde bestandsnaam, hoger in de boom
echo goed > "$RUN/tp.verborgen/sub/dir/file.txt"
echo ander > "$RUN/tp.verborgen/other/file.txt"
ln -s "$RUN/runtime/real.txt" "$RUN/abs-runtime"
ln -s "$INS/bin/ravengun" "$RUN/abs-install"
ln -s "$RUN/chain2" "$RUN/chain1"                  # een keten van absolute koppelingen binnen de runtime
ln -s "$RUN/runtime/real.txt" "$RUN/chain2"
ln -s "$RUN/tp/sub/dir/file.txt" "$RUN/verborgen-diep"   # tp bestaat alleen als tp.verborgen
ln -s "$RUN/tp/other/file.txt" "$RUN/verborgen-ander"
echo inhoud > "$H/.local/share/f2"
ln -s "$H/.local/share/f2" "$H/.local/share/f1"    # een keten buiten de runtime: wordt de echte inhoud
ln -s "$H/.local/share/f1" "$RUN/ext"
echo binnen > "$H/.local/share/otherdir/file"
ln -s "$RUN/runtime/real.txt" "$H/.local/share/otherdir/inner"
ln -s "$H/.local/share/otherdir" "$RUN/extdir"     # een map uitpakken; de koppeling erin wordt relatief
ln -s "$NAS/game" "$RUN/nas"                       # de spelinstallatie: blijft absoluut
mkdir -p "$RUN/overlay/work/work"; echo x > "$RUN/overlay/work/work/f"; chmod 000 "$RUN/overlay/work/work" "$RUN/overlay/work"
printf '{"games": [{"path": "%s/.local/share/ravengun/foo"}]}\n' "$H" > "$CFG/library.json"
printf '{"cmd": "env X=1 %s/.local/share/ravengun/y --a", "list": ["%s/.config/ravengun/z.json", "/oud/RavenGun/User/config/ravengun/q.json", "PATH=/usr/bin:%s/.local/share/ravengun/bin"]}\n' "$H" "$H" "$H" > "$CFG/config.json"
echo geheim > "$CFG/web-token"
S="$H/.local/share"
# een keten van mapkoppelingen (ketenA -> B -> C) met een relatieve koppeling binnen en buiten de boom en een mapkoppeling erin
mkdir -p "$S/ketenC/sub" "$S/ketenD"
echo c-file > "$S/ketenC/file"; echo los > "$S/otherfile"; echo d1 > "$S/ketenD/d1"
ln -s ../file "$S/ketenC/sub/rel"
ln -s ../../otherfile "$S/ketenC/sub/rel-uit"
ln -s "$S/ketenD" "$S/ketenC/sub/l"
ln -s "$RUN/runtime/real.txt" "$S/ketenD/back"
ln -s "$S/ketenC" "$S/ketenB"; ln -s "$S/ketenB" "$S/ketenA"; ln -s "$S/ketenA" "$RUN/ketendir"
# koppelingen naar de wortel zelf
ln -s "$RUN" "$RUN/self-root"; ln -s "$CFG" "$RUN/cfg-root"; ln -s "$INS" "$RUN/ins-root"
# de toegestane cache: wordt een lege map
mkdir -p "$S/Steam/appcache/sub" "$RUN/Steam"; echo cache > "$S/Steam/appcache/sub/x"; ln -s "$S/Steam/appcache" "$RUN/Steam/appcache"
# alleen-lezen bron: bestand 444 en map 555 met een koppeling erin; nooit u+w op de bron
mkdir -p "$RUN/ro"; echo r > "$RUN/ro/rfile"; chmod 444 "$RUN/ro/rfile"; ln -s "$RUN/runtime/real.txt" "$RUN/ro/lnk"; chmod 555 "$RUN/ro"
# overlay-toestand van een schrijflaag: whiteouts (char-device 0:0) en user.overlay.*-xattrs, zonder rootrechten te maken
setx() { python3 -c 'import os,sys; os.setxattr(sys.argv[1], sys.argv[2], sys.argv[3].encode())' "$@"; }
getx() { python3 -c 'import os,sys; sys.stdout.write(os.getxattr(sys.argv[1], sys.argv[2]).decode())' "$@"; }
WH_OK=0
if mknod "$T/wh-probe" c 0 0 2>/dev/null && [[ "$(stat -c %t:%T "$T/wh-probe")" == 0:0 ]] && setx "$T" user.overlay.probe y 2>/dev/null; then WH_OK=1; fi
rm -f "$T/wh-probe"
UP="$RUN/writelayer/G/mnt/upper"; mkdir -p "$UP/sub" "$RUN/writelayer/G/mnt/work"; echo data > "$UP/sub/file"
if [[ $WH_OK == 1 ]]; then
    mknod "$UP/gone" c 0 0; mknod "$UP/sub/gone2" c 0 0
    setx "$UP/sub" user.overlay.opaque y; setx "$UP" user.overlay.impure y; setx "$UP/sub/file" user.overlay.origin ""
else
    echo "let op: whiteouts (mknod 0:0) of user-xattrs niet mogelijk op deze schijf; die controles worden overgeslagen"
fi
P="$T/patches"; mkdir -p "$P/Game"
echo '[Input]' > "$P/Game/game.ini"; echo dossier > "$P/Game/notes.txt"; echo los > "$P/losbestand.txt"

# Een nep-rsync voor de afbreek- en foutscenario's.
printf '#!/bin/sh\nexit 23\n' > "$T/fakebin/rsync-fout"
printf '#!/bin/sh\nkill -TERM "$PPID"; sleep 1; exit 0\n' > "$T/fakebin/rsync-term"
chmod 755 "$T/fakebin/"*
ONDER() {  # ONDER <nep-rsync> <args>: draai het script met een rsync die faalt of afbreekt
    local fake="$1"; shift
    mkdir -p "$T/pad-$fake"; ln -sf "$T/fakebin/$fake" "$T/pad-$fake/rsync"
    PATH="$T/pad-$fake:$PATH" run "$@"
}
run() {
    HOME="$H" RG_PORTABLE_PATCHES="$P" bash "$SCRIPT" "$@" > "$T/uit.txt" 2>&1
}
# De mode-000-controles zijn zinloos als root (die negeert de rechten).
[[ "$(id -u)" == 0 ]] && echo "let op: als root zijn de mode-000-controles niet scherp"

# --- 1. te weinig ruimte: stopt voor er iets staat -------------------------------------------------------------------------------------
(cd "$T" && RG_PORTABLE_EXTRA_FREE=999999999999999 run out) && fail "te weinig ruimte: het script slaagde"
[[ -e "$T/out/User" || -e "$T/out/User.new" ]] && fail "te weinig ruimte: User/ of User.new staat er"
grep -q "te weinig ruimte" "$T/uit.txt" || fail "te weinig ruimte: geen melding ($(cat "$T/uit.txt"))"

# --- 2. afgebroken eerste run (SIGTERM tijdens rsync): geen User en geen User.new ----------------------------------------------------
(cd "$T" && ONDER rsync-term out) && fail "afgebroken run: het script slaagde"
[[ -e "$T/out/User" || -e "$T/out/User.new" ]] && fail "afgebroken run: User/ of User.new is achtergebleven"

# --- 3. mislukte rsync bij de eerste run: exitcode, geen User ------------------------------------------------------------------------
(cd "$T" && ONDER rsync-fout out) && fail "rsync-fout: het script slaagde"
[[ -e "$T/out/User" || -e "$T/out/User.new" ]] && fail "rsync-fout: User/ of User.new is achtergebleven"

# --- 4. de goede eerste run, met een relatief DOEL -----------------------------------------------------------------------------------
(cd "$T" && run out) || fail "eerste run mislukt: $(cat "$T/uit.txt")"
O="$T/out"; U="$O/User/share/ravengun"
[[ -x "$O/bin/ravengund" && -f "$O/lib/libx.so" && -x "$O/ravengun.sh" ]] || fail "de applicatie staat niet in de map"
[[ -f "$O/share/ravengun/patches/Game/game.ini" ]] || fail "de levering ontbreekt in share/ravengun/patches"
[[ -e "$O/share/ravengun/patches/Game/notes.txt" || -e "$O/share/ravengun/patches/losbestand.txt" ]] && fail "dossier of los bestand uit de werkmap is meegekomen"
[[ -e "$O/User.new" || -e "$O/User.old" ]] && fail "User.new of User.old is blijven staan"
[[ "$(cat "$U/runtime/real.txt" 2>/dev/null)" == echt ]] || fail "de runtime is niet gekopieerd"
[[ -f "$U/overlay/work/work/f" ]] || fail "de mode-000-map is niet mee gekopieerd"
first_out="$(cat "$T/uit.txt")"
[[ "$(find "$O" -lname "$H/.local/*" | wc -l)" == 0 ]] || fail "er wijzen nog koppelingen naar ~/.local: $(find "$O" -lname "$H/.local/*")"
# Elke koppeling is relatief en blijft in de map, behalve de NAS-koppeling.
n=0
while IFS= read -r l; do
    t="$(readlink "$l")"
    [[ "$l" == "$U/nas" ]] && continue
    n=$((n + 1))
    [[ "$t" == /* ]] && fail "absolute koppeling in de map: $l -> $t"
    r="$(readlink -f "$l")"
    [[ "$r" == "$O" || "$r" == "$O"/* ]] || fail "koppeling wijst uit de map: $l -> $t"
done < <(find "$O" -type l)
(( n > 3 )) || fail "te weinig koppelingen gecontroleerd ($n)"
[[ "$(readlink "$U/nas")" == "$NAS/game" ]] || fail "de NAS-koppeling is aangepast: $(readlink "$U/nas")"
[[ "$(readlink -f "$U/abs-runtime")" == "$U/runtime/real.txt" ]] || fail "abs-runtime wijst niet naar de kopie"
[[ "$(readlink -f "$U/abs-install")" == "$O/bin/ravengun" ]] || fail "abs-install wijst niet naar bin/ravengun"
[[ "$(readlink -f "$U/chain1")" == "$U/runtime/real.txt" ]] || fail "de keten chain1 -> chain2 -> real.txt klopt niet: $(readlink -f "$U/chain1")"
[[ "$(cat "$U/verborgen-diep")" == goed ]] || fail ".verborgen-terugval pakt niet het volledige subpad: $(cat "$U/verborgen-diep" 2>&1)"
[[ "$(cat "$U/verborgen-ander")" == ander ]] || fail ".verborgen-terugval (ander subpad): $(cat "$U/verborgen-ander" 2>&1)"
[[ ! -L "$U/ext" && "$(cat "$U/ext" 2>/dev/null)" == inhoud ]] || fail "de keten buiten de runtime is niet uitgepakt (-L)"
[[ -d "$U/extdir" && ! -L "$U/extdir" && "$(readlink -f "$U/extdir/inner")" == "$U/runtime/real.txt" ]] || fail "extdir is niet uitgepakt of de koppeling erin is niet relatief"
[[ -f "$O/User/config/ravengun/config.json" && ! -e "$O/User/config/ravengun/web-token" ]] || fail "config ontbreekt of web-token is meegekomen"
grep -q "\"$O/User/share/ravengun/foo\"" "$O/User/config/ravengun/library.json" || fail "library.json heeft geen absoluut pad onder $O: $(cat "$O/User/config/ravengun/library.json")"
# M1: de keten van mapkoppelingen is in één keer echte inhoud, ook de koppelingen erin
[[ -d "$U/ketendir" && ! -L "$U/ketendir" && "$(cat "$U/ketendir/file")" == c-file ]] || fail "ketendir is geen echte map met de inhoud van de keten"
[[ -L "$U/ketendir/sub/rel" && "$(readlink "$U/ketendir/sub/rel")" == ../file && "$(cat "$U/ketendir/sub/rel")" == c-file ]] || fail "de relatieve koppeling binnen de uitgepakte boom is veranderd"
[[ ! -L "$U/ketendir/sub/rel-uit" && "$(cat "$U/ketendir/sub/rel-uit" 2>&1)" == los ]] || fail "de relatieve koppeling naar buiten de boom is niet de inhoud van het origineel geworden"
[[ -d "$U/ketendir/sub/l" && ! -L "$U/ketendir/sub/l" && "$(cat "$U/ketendir/sub/l/d1")" == d1 ]] || fail "de mapkoppeling in de uitgepakte boom is niet ook uitgepakt"
[[ "$(readlink -f "$U/ketendir/sub/l/back")" == "$U/runtime/real.txt" ]] || fail "de koppeling twee niveaus diep in de keten wijst niet naar de kopie van de runtime"
# M2: koppelingen naar de wortels zelf, en alleen een toegestane cache wordt leeg
[[ "$(readlink -f "$U/self-root")" == "$U" ]] || fail "een koppeling naar de runtime zelf is niet herschreven: $(readlink "$U/self-root")"
[[ "$(readlink -f "$U/cfg-root")" == "$O/User/config/ravengun" ]] || fail "een koppeling naar de configuratiemap zelf is niet herschreven: $(readlink "$U/cfg-root")"
[[ "$(readlink -f "$U/ins-root")" == "$O" ]] || fail "een koppeling naar de installatie zelf is niet herschreven: $(readlink "$U/ins-root")"
[[ -d "$U/Steam/appcache" && ! -L "$U/Steam/appcache" && -z "$(ls -A "$U/Steam/appcache")" ]] || fail "de toegestane appcache is geen lege map geworden"
grep -q "toegestaan" <<< "$first_out" || fail "het leegmaken van de appcache is niet gemeld"
# M4: de bron krijgt nooit een schrijfbit erbij
if [[ "$(id -u)" != 0 ]]; then
    [[ "$(stat -c %a "$RUN/ro")" == 555 && "$(stat -c %a "$RUN/ro/rfile")" == 444 ]] || fail "de bronrechten zijn aangepast: $(stat -c %a "$RUN/ro") $(stat -c %a "$RUN/ro/rfile")"
    [[ ! -w "$RUN/overlay/work" && ! -w "$RUN/overlay/work/work" ]] || fail "de bron-overlaymap heeft een schrijfbit gekregen"
    [[ "$(stat -c %a "$U/ro")" == 555 && "$(readlink -f "$U/ro/lnk")" == "$U/runtime/real.txt" ]] || fail "de koppeling in de alleen-lezen map is niet herschreven, of de map is niet weer 555: $(stat -c %a "$U/ro")"
fi
# M6: whiteouts en overlay-xattrs zijn mee
if [[ $WH_OK == 1 ]]; then
    WU="$U/writelayer/G/mnt/upper"
    for w in gone sub/gone2; do
        [[ "$(stat -c '%F %t:%T' "$WU/$w" 2>/dev/null)" == "character special file 0:0" ]] || fail "whiteout $w is niet mee (zonder rootrechten slaat rsync die over)"
    done
    [[ "$(getx "$WU/sub" user.overlay.opaque 2>&1)" == y && "$(getx "$WU" user.overlay.impure 2>&1)" == y ]] || fail "user.overlay.*-xattrs op mappen zijn niet mee"
    getx "$WU/sub/file" user.overlay.origin >/dev/null 2>&1 || fail "user.overlay.origin op een bestand is niet mee"
    grep -q "2 whiteouts" <<< "$first_out" || fail "de whiteout-controle meldt niets: $first_out"
    grep -q "skipping non-regular" <<< "$first_out" && fail "rsync meldt overgeslagen bijzondere bestanden"
fi
# M7: padherschrijving behoudt wat ervoor staat, kent de config-markering en werkt ook na een verhuizing (starter)
cfgjson() { python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d[sys.argv[2]] if sys.argv[2]=="cmd" else "|".join(d["list"]))' "$@"; }
[[ "$(cfgjson "$O/User/config/ravengun/config.json" cmd)" == "env X=1 $O/User/share/ravengun/y --a" ]] || fail "prefix voor het pad verloren of pad niet herschreven: $(cfgjson "$O/User/config/ravengun/config.json" cmd)"
[[ "$(cfgjson "$O/User/config/ravengun/config.json" list)" == "$O/User/config/ravengun/z.json|$O/User/config/ravengun/q.json|PATH=/usr/bin:$O/User/share/ravengun/bin" ]] || fail "config-paden fout: $(cfgjson "$O/User/config/ravengun/config.json" list)"
mv "$O" "$T/verhuisd"
bash "$T/verhuisd/ravengun.sh" app >/dev/null 2>&1
[[ "$(cfgjson "$T/verhuisd/User/config/ravengun/config.json" cmd)" == "env X=1 $T/verhuisd/User/share/ravengun/y --a" ]] || fail "de starter zet de paden na een verhuizing niet om: $(cfgjson "$T/verhuisd/User/config/ravengun/config.json" cmd)"
[[ "$(cfgjson "$T/verhuisd/User/config/ravengun/config.json" list)" == "$T/verhuisd/User/config/ravengun/z.json|$T/verhuisd/User/config/ravengun/q.json|PATH=/usr/bin:$T/verhuisd/User/share/ravengun/bin" ]] || fail "starter: config-paden na verhuizing fout"
mv "$T/verhuisd" "$O"
ok "eerste run"

# --- 5. nogmaals: User/ blijft staan, de applicatie wordt ververst; een game-overlay (mode 000) hindert niet -------------------------
echo MARK > "$U/MARK"
mkdir -p "$U/gamewerk/work/work"; echo x > "$U/gamewerk/work/work/f"; chmod 000 "$U/gamewerk/work/work" "$U/gamewerk/work"
ln -s "$RUN/runtime/real.txt" "$U/oud-abs"        # een absolute koppeling en een dode in de bestaande User/: ongemoeid, geen fout
ln -s "$H/.local/share/bestaat-niet" "$U/oud-dood"
echo v2 > "$INS/bin/ravengun"
run "$O" || fail "tweede run mislukt: $(cat "$T/uit.txt")"
[[ "$(cat "$U/MARK" 2>/dev/null)" == MARK ]] || fail "de tweede run heeft User/ aangeraakt"
grep -q "blijft staan" "$T/uit.txt" || fail "de tweede run meldt niet dat User/ blijft staan"
[[ "$(cat "$O/bin/ravengun")" == v2 ]] || fail "bin/ is niet ververst"
[[ "$(readlink "$U/oud-abs")" == "$RUN/runtime/real.txt" && "$(readlink "$U/oud-dood")" == "$H/.local/share/bestaat-niet" ]] || fail "de bestaande User/ is gescand en aangepast"
[[ "$(stat -c %a "$U/gamewerk/work")" == 0 ]] || [[ "$(id -u)" == 0 ]] || fail "de game-overlaymap in de bestaande User/ kreeg andere rechten: $(stat -c %a "$U/gamewerk/work")"
ok "herhaalde run"

# --- 6. --reset-user met een mislukte rsync: de oude boom blijft heel ------------------------------------------------------------------
ONDER rsync-fout --reset-user "$O" && fail "reset met rsync-fout: het script slaagde"
[[ "$(cat "$U/MARK" 2>/dev/null)" == MARK && -e "$U/gamewerk" ]] || fail "reset met rsync-fout: de oude User/ is aangetast"
[[ -e "$O/User.new" ]] && fail "reset met rsync-fout: User.new is blijven staan"
[[ "$(readlink "$U/oud-abs")" == "$RUN/runtime/real.txt" ]] || fail "reset met rsync-fout: een koppeling in de oude User/ is aangepast"
ok "rsync-fout laat de oude boom staan"

# --- 7. --reset-user: alles opnieuw, ook met een mode-000-map in de oude User/ -------------------------------------------------------
run --reset-user "$O" || fail "reset mislukte: $(cat "$T/uit.txt")"
[[ -e "$U/MARK" || -e "$U/gamewerk" ]] && fail "reset: de oude User/ is niet vervangen"
[[ "$(cat "$U/runtime/real.txt" 2>/dev/null)" == echt && -d "$O/User/state" && -d "$O/User/cache" ]] || fail "reset: de nieuwe User/ is niet compleet"
[[ -e "$O/User.new" || -e "$O/User.old" ]] && fail "reset: User.new of User.old is blijven staan"
[[ ! -e "$U/oud-dood" && ! -L "$U/oud-dood" ]] || fail "reset: de oude User/ is niet vervangen"
ok "--reset-user"

# --- 8. een relatieve koppeling die uit de map wijst: exitcode, geen User/ -----------------------------------------------------------
ln -s ../../../../nas/game "$RUN/ontsnapt"
run "$T/out2" && fail "ontsnapte koppeling: het script slaagde"
grep -q "ontsnapt" "$T/uit.txt" || fail "ontsnapte koppeling: niet gemeld ($(cat "$T/uit.txt"))"
[[ -e "$T/out2/User" || -e "$T/out2/User.new" ]] && fail "ontsnapte koppeling: User/ of User.new staat er"
rm -f "$RUN/ontsnapt"

# --- 9. een koppeling naar ~/.local zonder doel kan niet herschreven worden: exitcode ---------------------------------------------
ln -s "$H/.local/share/bestaat-niet" "$RUN/dood"
run "$T/out3" && fail "dode koppeling naar ~/.local: het script slaagde"
grep -q "dood" "$T/uit.txt" || fail "dode koppeling: niet gemeld"
[[ -e "$T/out3/User" || -e "$T/out3/User.new" ]] && fail "dode koppeling: User/ of User.new staat er"
rm -f "$RUN/dood"

# --- 10. een map die niet op de toegestane lijst staat en te groot is: stop met een melding (niet stil leeg) ---------------------------
mkdir -p "$S/grootdir"; head -c 200000 /dev/zero > "$S/grootdir/blob"; ln -s "$S/grootdir" "$RUN/groot"
(cd "$T" && RG_PORTABLE_MAX_EXTRACT=100000 run "$T/out4") && fail "te grote mapkoppeling: het script slaagde"
grep -q "te groot" "$T/uit.txt" || fail "te grote mapkoppeling: geen melding ($(cat "$T/uit.txt"))"
[[ -e "$T/out4/User" || -e "$T/out4/User.new" ]] && fail "te grote mapkoppeling: User/ of User.new staat er"
# met de map op de toegestane lijst mag hij leeg worden
(cd "$T" && RG_PORTABLE_MAX_EXTRACT=100000 RG_PORTABLE_EMPTY_OK="$S/grootdir" run "$T/out5") || fail "toegestane grote map: het script faalde: $(cat "$T/uit.txt")"
[[ -d "$T/out5/User/share/ravengun/groot" && -z "$(ls -A "$T/out5/User/share/ravengun/groot")" ]] || fail "toegestane grote map is niet leeg"
rm -f "$RUN/groot"

# --- 11. een map die half te kopiëren is (onleesbaar bestand): stop, geen lege of halve map ------------------------------------------
if [[ "$(id -u)" != 0 ]]; then
    mkdir -p "$S/halfdir"; echo ok > "$S/halfdir/ok"; echo geheim > "$S/halfdir/geheim"; chmod 000 "$S/halfdir/geheim"
    ln -s "$S/halfdir" "$RUN/half"
    run "$T/out6" && fail "half te kopiëren map: het script slaagde"
    grep -q "mislukt" "$T/uit.txt" || fail "half te kopiëren map: geen melding ($(cat "$T/uit.txt"))"
    [[ -e "$T/out6/User" || -e "$T/out6/User.new" ]] && fail "half te kopiëren map: User/ of User.new staat er"
    rm -f "$RUN/half"; chmod 644 "$S/halfdir/geheim"
fi

# --- 12. overlay: een whiteout die niet te reproduceren is, of xattrs die ontbreken, geven een foutcode -------------------------------
if [[ $WH_OK == 1 ]]; then
    TOOL="$R/scripts/portable_tools.py"
    mkdir -p "$T/wf/src" "$T/wf/dst1" "$T/wf/dst2"
    mknod "$T/wf/src/w" c 0 0; echo gewoon > "$T/wf/dst1/w"
    python3 -I "$TOOL" whiteouts "$T/wf/src" "$T/wf/dst1" > /dev/null 2> "$T/wf/err1" && fail "whiteout die als gewoon bestand bestaat: geen foutcode"
    grep -q "geen whiteout" "$T/wf/err1" || fail "whiteout-fout niet gemeld: $(cat "$T/wf/err1")"
    python3 -I "$TOOL" whiteouts "$T/wf/src" "$T/wf/dst2" > /dev/null 2>&1 || fail "whiteout in een lege doelmap had moeten lukken"
    [[ "$(stat -c %t:%T "$T/wf/dst2/w")" == 0:0 ]] || fail "whiteout is niet aangemaakt"
    mkdir -p "$T/wf/xs" "$T/wf/xd"; setx "$T/wf/xs" user.overlay.opaque y
    python3 -I "$TOOL" whiteouts "$T/wf/xs" "$T/wf/xd" > /dev/null 2> "$T/wf/err2" && fail "ontbrekende overlay-xattr: geen foutcode"
    grep -q "xattrs" "$T/wf/err2" || fail "ontbrekende overlay-xattr niet gemeld"
fi

# --- 13. de profielen komen uit git HEAD, niet uit de werkmap -----------------------------------------------------------------------
if command -v git >/dev/null; then
    G="$T/gitrepo"; mkdir -p "$G/scripts" "$G/tools/dev" "$G/patches/GameG"
    cp "$REPO/scripts/make-portable.sh" "$REPO/scripts/portable_tools.py" "$G/scripts/"; cp "$REPO/tools/dev/ship-patches.py" "$G/tools/dev/"
    printf '[Input]\nversie = gecommit\n' > "$G/patches/GameG/game.ini"
    git -C "$G" init -q && git -C "$G" add -A && git -C "$G" -c user.email=t@t -c user.name=t commit -qm profiel
    printf '[Input]\nversie = onaf\n' > "$G/patches/GameG/game.ini"
    mkdir -p "$G/patches/GameG/dropin"; echo los > "$G/patches/GameG/dropin/los.dll"; echo nieuw > "$G/patches/GameG/native.json"
    (cd "$T" && HOME="$H" bash "$G/scripts/make-portable.sh" outgit > "$T/uit.txt" 2>&1) || fail "run met git-profielen mislukte: $(cat "$T/uit.txt")"
    grep -q "gecommit" "$T/outgit/share/ravengun/patches/GameG/game.ini" 2>/dev/null || fail "het gecommitte profiel is niet geleverd"
    [[ -e "$T/outgit/share/ravengun/patches/GameG/dropin/los.dll" || -e "$T/outgit/share/ravengun/patches/GameG/native.json" ]] && fail "een ongevolgd bestand uit de werkmap is meegekomen"
    grep -q "onaf" "$T/outgit/share/ravengun/patches/GameG/game.ini" && fail "een niet-gecommitte wijziging is meegekomen"
    [[ -e "$T/outgit/.patches-export" ]] && fail "de tijdelijke patches-export is blijven staan"
    grep -q "niet-gecommitte" "$T/uit.txt" || fail "geen waarschuwing over niet-gecommitte wijzigingen"
else
    echo "overgeslagen: git ontbreekt (profielen uit HEAD)"
fi
ok "controle op koppelingen"

if [[ $FAILS -eq 0 ]]; then echo "make_portable_test: alles goed"; exit 0; fi
echo "make_portable_test: $FAILS fout(en)"; exit 1

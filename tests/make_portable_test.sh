#!/usr/bin/env bash
# scripts/make-portable.sh op een kleine nagemaakte thuismap (geen echte runtime, geen 18 GB): koppelingen naar de runtime en de
# installatie (ook een keten), een koppeling naar buiten (de NAS: blijft zoals hij is), een mode-000-map, een afgebroken eerste
# run, een herhaalde run die User/ laat staan, --reset-user, een mislukte rsync die de oude boom ongemoeid laat, een
# koppeling die uit de map wijst en een koppeling naar ~/.local zonder doel (allebei: exitcode, geen User/).
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
cp "$REPO/scripts/make-portable.sh" "$R/scripts/"
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
echo '{}' > "$CFG/config.json"; echo geheim > "$CFG/web-token"
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
[[ "$(find "$O" -lname "$H/.local/*" | wc -l)" == 0 ]] || fail "er wijzen nog koppelingen naar ~/.local: $(find "$O" -lname "$H/.local/*")"
# Elke koppeling is relatief en blijft in de map, behalve de NAS-koppeling.
n=0
while IFS= read -r l; do
    t="$(readlink "$l")"
    [[ "$l" == "$U/nas" ]] && continue
    n=$((n + 1))
    [[ "$t" == /* ]] && fail "absolute koppeling in de map: $l -> $t"
    [[ "$(readlink -f "$l")" == "$O"/* ]] || fail "koppeling wijst uit de map: $l -> $t"
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
ok "eerste run"

# --- 5. nogmaals: User/ blijft staan, de applicatie wordt ververst; een game-overlay (mode 000) hindert niet -------------------------
echo MARK > "$U/MARK"
mkdir -p "$U/gamewerk/work/work"; echo x > "$U/gamewerk/work/work/f"; chmod 000 "$U/gamewerk/work/work" "$U/gamewerk/work"
echo v2 > "$INS/bin/ravengun"
run "$O" || fail "tweede run mislukt: $(cat "$T/uit.txt")"
[[ "$(cat "$U/MARK" 2>/dev/null)" == MARK ]] || fail "de tweede run heeft User/ aangeraakt"
grep -q "blijft staan" "$T/uit.txt" || fail "de tweede run meldt niet dat User/ blijft staan"
[[ "$(cat "$O/bin/ravengun")" == v2 ]] || fail "bin/ is niet ververst"
ok "herhaalde run"

# --- 6. --reset-user met een mislukte rsync: de oude boom blijft heel ------------------------------------------------------------------
ONDER rsync-fout --reset-user "$O" && fail "reset met rsync-fout: het script slaagde"
[[ "$(cat "$U/MARK" 2>/dev/null)" == MARK && -e "$U/gamewerk" ]] || fail "reset met rsync-fout: de oude User/ is aangetast"
[[ -e "$O/User.new" ]] && fail "reset met rsync-fout: User.new is blijven staan"
ok "rsync-fout laat de oude boom staan"

# --- 7. --reset-user: alles opnieuw, ook met een mode-000-map in de oude User/ -------------------------------------------------------
run --reset-user "$O" || fail "reset mislukte: $(cat "$T/uit.txt")"
[[ -e "$U/MARK" || -e "$U/gamewerk" ]] && fail "reset: de oude User/ is niet vervangen"
[[ "$(cat "$U/runtime/real.txt" 2>/dev/null)" == echt && -d "$O/User/state" && -d "$O/User/cache" ]] || fail "reset: de nieuwe User/ is niet compleet"
[[ -e "$O/User.new" || -e "$O/User.old" ]] && fail "reset: User.new of User.old is blijven staan"
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
ok "controle op koppelingen"

if [[ $FAILS -eq 0 ]]; then echo "make_portable_test: alles goed"; exit 0; fi
echo "make_portable_test: $FAILS fout(en)"; exit 1

#!/usr/bin/env bash
# scripts/make-portable.sh op een kleine nagemaakte thuismap (geen echte runtime, geen 18 GB): koppelingen naar de runtime en de
# installatie (ook een keten), een koppeling naar buiten (de NAS: blijft zoals hij is), een mode-000-map, een afgebroken eerste
# run, een herhaalde run die User/ laat staan, --reset-user, een mislukte rsync die de oude boom ongemoeid laat, een
# koppeling die uit de map wijst en een koppeling naar ~/.local zonder doel (allebei: exitcode, geen User/). Verder: een keten van
# mapkoppelingen met koppelingen erin, koppelingen naar de wortel van de runtime/installatie/configuratie zelf, een toegestane cache
# (leeg) tegenover een te grote of half te kopiëren map (stop), overlay-whiteouts en -xattrs van de schrijflagen, de bronrechten
# (nooit u+w), een oude User/ die niet gescand of aangepast wordt, de profielen uit git HEAD en de padherschrijving (config + starter).
# Ronde 3: wrappers die koppelingen naar repo-scripts zijn (nooit erdoorheen schrijven), de doelmapbewaking, share/ via share.new,
# verborgen mappen in de runtime, de bronrechten precies terug (000 blijft 000), een relatieve koppeling naar wat overgeslagen is,
# ~/.steam als weg naar ~/.local, een te groot bestand, geen grootte bij een toegestane cache, overlays alleen waar ze leven,
# een bron die na de rsync verandert (een draaiende game), en de wissel bij --reset-user: niet als User/ in gebruik is, User.old
# blijft tot de volgende geslaagde run, en een mislukte tweede mv zet de oude User/ terug.
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
# F1: wrappers. wine-prefix is een koppeling naar een script in de repo (zoals live naar ~/projects/ravengun/scripts), elders een
# koppeling naar een script buiten alles; echt is een gewoon bestand. Geen van de scripts buiten de map mag veranderen.
OLDLINE='RUNTIME="${RUNTIME:-$HOME/.local/share/ravengun}"'
mkdir -p "$RUN/wrappers" "$T/elders"
printf '#!/bin/sh\n%s\n' "$OLDLINE" > "$R/scripts/wine-prefix.sh"; cp "$R/scripts/wine-prefix.sh" "$T/wine-prefix.orig"
printf '#!/bin/sh\n%s\n' "$OLDLINE" > "$T/elders/wrap.sh"; cp "$T/elders/wrap.sh" "$T/wrap.orig"
ln -s "$R/scripts/wine-prefix.sh" "$RUN/wrappers/wine-prefix"
ln -s "$T/elders/wrap.sh" "$RUN/wrappers/elders"
printf '#!/bin/sh\n%s\n' "$OLDLINE" > "$RUN/wrappers/echt"; chmod 750 "$RUN/wrappers/echt"
# F10: een verborgen map in de runtime gaat mee
mkdir -p "$RUN/.verborgen-map"; echo dot > "$RUN/.verborgen-map/f"
# F9: ~/.steam/root is (zoals op de Deck) een koppeling naar ~/.local/share/Steam; via die weg naar een bestand en de appcache
mkdir -p "$H/.steam"; ln -s "$H/.local/share/Steam/" "$H/.steam/root"; echo cfg > "$S/Steam/steam.cfg"
ln -s "$H/.steam/root/steam.cfg" "$RUN/via-steam"; ln -s "$H/.steam/root/appcache" "$RUN/appcache-via-steam"
# F3: een overlay onder native/<Game>/<n>/upper (gevonden zonder de hele boom te lopen) en een char-device 0:0 buiten elke
# overlay (diep in een prefix-achtige boom): die laatste hoort niet bij de overlay-toestand en wordt niet nagemaakt.
mkdir -p "$RUN/native/G/0/upper/d" "$RUN/native/G/0/work" "$RUN/pfx/a/b/c"
if [[ $WH_OK == 1 ]]; then
    mknod "$RUN/native/G/0/upper/d/wn" c 0 0; setx "$RUN/native/G/0/upper/d" user.overlay.opaque y
    mknod "$RUN/pfx/a/b/c/geen-overlay" c 0 0
fi
P="$T/patches"; mkdir -p "$P/Game"
echo '[Input]' > "$P/Game/game.ini"; echo dossier > "$P/Game/notes.txt"; echo los > "$P/losbestand.txt"

# Een nep-rsync voor de afbreek- en foutscenario's.
printf '#!/bin/sh\nexit 23\n' > "$T/fakebin/rsync-fout"
printf '#!/bin/sh\nkill -TERM "$PPID"; sleep 1; exit 0\n' > "$T/fakebin/rsync-term"
# Een rsync die echt kopieert en daarna, zoals een draaiende game, in de bron van de schrijflaag een nieuwe map met een whiteout
# en een overlay-xattr maakt (na de rsync, dus niet in de kopie).
REAL_RSYNC="$(command -v rsync)"; REAL_MV="$(command -v mv)"
cat > "$T/fakebin/rsync-spel" <<NEP
#!/bin/sh
"$REAL_RSYNC" "\$@"; rc=\$?
for a in "\$@"; do
    case "\$a" in
        */writelayer) mkdir -p "\$a/G/mnt/upper/later"; mknod "\$a/G/mnt/upper/later/w" c 0 0
                      python3 -c 'import os,sys; os.setxattr(sys.argv[1], "user.overlay.opaque", b"y")' "\$a/G/mnt/upper/later" ;;
    esac
done
exit \$rc
NEP
# Een mv die faalt als hij User.new op de plek van User/ wil zetten (de tweede mv van de wissel).
cat > "$T/fakebin/mv-fout" <<NEP
#!/bin/sh
for a in "\$@"; do last="\$a"; done
case "\$*" in *User.new*) [ "\${last##*/}" = User ] && exit 1 ;; esac
exec "$REAL_MV" "\$@"
NEP
chmod 755 "$T/fakebin/"*
ONDER() {  # ONDER <nep-programma> <args>: draai het script met een rsync (of mv) die faalt, afbreekt of de bron verandert
    local fake="$1" naam=rsync; shift
    [[ "$fake" == mv-* ]] && naam=mv
    mkdir -p "$T/pad-$fake"; ln -sf "$T/fakebin/$fake" "$T/pad-$fake/$naam"
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
    [[ "$l" == "$U/nas" || "$l" == "$U/wrappers/elders" ]] && continue   # absoluut naar buiten: blijft staan
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
    [[ "$(stat -c %a "$RUN/overlay/work")" == 0 ]] || fail "F11: de mode-000-werkmap in de bron is niet terug op 000: $(stat -c %a "$RUN/overlay/work")"
    chmod 100 "$RUN/overlay/work"     # alleen om de binnenste te kunnen bekijken, daarna weer 000
    [[ "$(stat -c %a "$RUN/overlay/work/work")" == 0 ]] || fail "F11: de binnenste mode-000-werkmap is niet terug op 000: $(stat -c %a "$RUN/overlay/work/work")"
    chmod 000 "$RUN/overlay/work"
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
# F1: de wrappers zijn in de map herschreven, de scripts buiten de map (repo en elders) zijn byte voor byte gelijk
cmp -s "$R/scripts/wine-prefix.sh" "$T/wine-prefix.orig" || fail "F1: het repo-script achter de wrapper-koppeling is herschreven"
cmp -s "$T/elders/wrap.sh" "$T/wrap.orig" || fail "F1: het script buiten de map achter een wrapper-koppeling is herschreven"
WR="$U/wrappers"
[[ -f "$WR/wine-prefix" && ! -L "$WR/wine-prefix" ]] || fail "F1: wrappers/wine-prefix is geen echt bestand in de map"
if grep -qF '$HOME/.local/share/ravengun' "$WR/wine-prefix" || ! grep -qF 'BASH_SOURCE' "$WR/wine-prefix"; then fail "F1: wrappers/wine-prefix in de map is niet herschreven: $(cat "$WR/wine-prefix")"; fi
if grep -qF '$HOME/.local/share/ravengun' "$WR/echt" || [[ "$(stat -c %a "$WR/echt")" != 750 ]]; then fail "F1: wrappers/echt niet herschreven of rechten kwijt"; fi
[[ "$(readlink "$WR/elders")" == "$T/elders/wrap.sh" ]] || fail "F1: de koppeling naar buiten is veranderd: $(readlink "$WR/elders")"
grep -q "wijst uit de map" <<< "$first_out" || fail "F1: de wrapper die uit de map wijst is niet gemeld"
# F10: verborgen mappen gaan mee
[[ "$(cat "$U/.verborgen-map/f" 2>/dev/null)" == dot ]] || fail "F10: een verborgen map in de runtime is niet gekopieerd"
# F9: via ~/.steam: een bestand wordt uitgepakt, de appcache (toegestaan) wordt leeg
[[ -f "$U/via-steam" && ! -L "$U/via-steam" && "$(cat "$U/via-steam")" == cfg ]] || fail "F9: een koppeling via ~/.steam is niet uitgepakt: $(readlink "$U/via-steam")"
[[ -d "$U/appcache-via-steam" && ! -L "$U/appcache-via-steam" && -z "$(ls -A "$U/appcache-via-steam")" ]] || fail "F9: de appcache via ~/.steam is geen lege map"
# F8: bij een toegestane cache geen grootte (dat zou de hele appcache doorlopen)
grep "toegestaan" <<< "$first_out" | grep -q "MB" && fail "F8: bij de toegestane cache wordt nog een grootte gemeld"
# F3: native/<Game>/<n>/upper is mee; een char-device buiten de overlays niet
if [[ $WH_OK == 1 ]]; then
    [[ "$(stat -c '%F %t:%T' "$U/native/G/0/upper/d/wn" 2>/dev/null)" == "character special file 0:0" ]] || fail "F3: de whiteout onder native/G/0/upper is niet mee"
    [[ "$(getx "$U/native/G/0/upper/d" user.overlay.opaque 2>&1)" == y ]] || fail "F3: de xattr onder native/G/0/upper is niet mee"
    [[ ! -e "$U/pfx/a/b/c/geen-overlay" ]] || fail "F3: een char-device buiten de overlays is als whiteout nagemaakt (de hele boom gescand)"
fi
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
[[ -e "$O/User.new" ]] && fail "reset: User.new is blijven staan"
[[ ! -e "$U/oud-dood" && ! -L "$U/oud-dood" ]] || fail "reset: de oude User/ is niet vervangen"
# F4: de oude User/ blijft als User.old staan (een game die er nog vanuit draait houdt zijn schrijflaag), en dat wordt gemeld
[[ "$(cat "$O/User.old/share/ravengun/MARK" 2>/dev/null)" == MARK ]] || fail "F4: de oude User/ staat niet als User.old"
grep -q "User.old" "$T/uit.txt" || fail "F4: niet gemeld dat de oude User/ als User.old blijft"
# ... tot de volgende geslaagde run
run "$O" || fail "run na de reset mislukte: $(cat "$T/uit.txt")"
[[ -e "$O/User.old" ]] && fail "F4: User.old is na de volgende geslaagde run niet opgeruimd"
ok "--reset-user"

# --- 7b. F4: geen wissel zolang een proces User/ gebruikt (werkmap, open bestand, mount); User/ blijft heel -------------------
echo MARK2 > "$U/MARK2"
busy_case() {  # busy_case <naam>: het proces draait al
    local naam="$1"
    run --reset-user "$O" && fail "F4 ($naam): de wissel ging door terwijl User/ in gebruik is"
    grep -q "in gebruik" "$T/uit.txt" || fail "F4 ($naam): geen melding 'in gebruik': $(cat "$T/uit.txt")"
    [[ "$(cat "$U/MARK2" 2>/dev/null)" == MARK2 && ! -e "$O/User.new" ]] || fail "F4 ($naam): User/ is aangetast of User.new bleef staan"
}
(cd "$O/User/share" && exec sleep 60) & BPID=$!
busy_case werkmap; kill "$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
(exec 9<"$U/runtime/real.txt"; cd /; exec sleep 60) & BPID=$!
busy_case "open bestand"; kill "$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
mkdir -p "$T/bindpunt"
if command -v bwrap >/dev/null && bwrap --dev-bind / / --ro-bind "$U/runtime" "$T/bindpunt" true 2>/dev/null; then
    (cd / && exec bwrap --dev-bind / / --ro-bind "$U/runtime" "$T/bindpunt" --chdir / --die-with-parent sleep 60) & BPID=$!
    for i in $(seq 40); do grep -qs " $T/bindpunt " /proc/[0-9]*/mountinfo && break; sleep 0.05; done
    busy_case "bind-mount in een sandbox"; kill "$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
    for i in $(seq 40); do grep -qs " $T/bindpunt " /proc/[0-9]*/mountinfo || break; sleep 0.05; done
else
    echo "let op: bwrap ontbreekt of werkt niet; de mount-variant van de gebruikscontrole is overgeslagen"
fi
ok "geen wissel bij een User/ in gebruik"

# --- 7c. F4: mislukt de tweede mv van de wissel, dan staat de oude User/ weer op zijn plek -----------------------------------
ONDER mv-fout --reset-user "$O" && fail "F4: de wissel met een falende mv slaagde"
[[ "$(cat "$U/MARK2" 2>/dev/null)" == MARK2 ]] || fail "F4: na een mislukte tweede mv is User/ er niet meer (of niet de oude): $(ls "$O")"
[[ -e "$O/User.new" ]] && fail "F4: User.new is na de mislukte wissel blijven staan"
grep -q "teruggezet" "$T/uit.txt" || fail "F4: het terugzetten is niet gemeld: $(cat "$T/uit.txt")"
ok "mislukte wissel zet User/ terug"

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

# --- 14. F9: een relatieve koppeling naar wat niet meegaat (bench-captures) faalt ---------------------------------------------------
mkdir -p "$RUN/bench-captures"; echo png > "$RUN/bench-captures/shot.png"; ln -s ../bench-captures/shot.png "$RUN/runtime/naar-bench"
run "$T/out7" && fail "F9: een relatieve koppeling naar het overgeslagen bench-captures werd geaccepteerd"
grep -q "naar-bench" "$T/uit.txt" || fail "F9: de koppeling naar bench-captures is niet gemeld ($(cat "$T/uit.txt"))"
[[ -e "$T/out7/User" ]] && fail "F9: User/ staat er toch"
rm -f "$RUN/runtime/naar-bench"

# --- 15. F8: een bestandskoppeling naar een te groot bestand: stop -----------------------------------------------------------------
head -c 200000 /dev/zero > "$S/grootbestand"; ln -s "$S/grootbestand" "$RUN/groot-f"
(cd "$T" && RG_PORTABLE_MAX_EXTRACT=100000 run "$T/out8") && fail "F8: een te groot bestand werd uitgepakt"
grep -q "groot-f.*te groot" "$T/uit.txt" || fail "F8: het te grote bestand is niet gemeld ($(cat "$T/uit.txt"))"
rm -f "$RUN/groot-f"

# --- 16. F7: de doelmap mag geen bron (of ouder ervan) zijn en er niet in liggen; een vreemde volle map ook niet -------------------
for d in "$H/.local" "$INS" "$H" "$RUN/sub" "$CFG" "$R" "$INS/.."; do
    run "$d" && fail "F7: doelmap $d werd geaccepteerd"
    grep -q "doelmap" "$T/uit.txt" || fail "F7: doelmap $d: geen melding ($(cat "$T/uit.txt"))"
done
[[ -x "$INS/bin/ravengund" && -f "$INS/lib/libx.so" && "$(cat "$RUN/runtime/real.txt")" == echt && -f "$CFG/library.json" ]] || fail "F7: een bron is aangetast"
[[ -e "$H/.local/bin" || -e "$RUN/sub" ]] && fail "F7: er is toch iets in de bron gemaakt"
mkdir -p "$T/vol"; echo eigen > "$T/vol/eigen.txt"
run "$T/vol" && fail "F7: een volle map zonder ravengun.sh werd geaccepteerd"
[[ "$(cat "$T/vol/eigen.txt")" == eigen && ! -e "$T/vol/bin" ]] || fail "F7: de volle vreemde map is aangeraakt"
run "$R/dist/RavenGun" || fail "F7: <repo>/dist/RavenGun moet mogen: $(cat "$T/uit.txt")"
# share/ via share.new: een mislukte kopie van share/ laat de vorige share/ heel
if [[ "$(id -u)" != 0 ]]; then
    echo x > "$INS/share/ravengun/onleesbaar"; chmod 000 "$INS/share/ravengun/onleesbaar"
    run "$O" && fail "F7: run met een onleesbaar bestand in share/ slaagde"
    [[ -f "$O/share/ravengun/patches/Game/game.ini" && ! -e "$O/share.new" ]] || fail "F7: na een mislukte kopie van share/ is de map zonder (volledige) share/"
    chmod 644 "$INS/share/ravengun/onleesbaar"; rm -f "$INS/share/ravengun/onleesbaar"
fi
ok "doelmapbewaking"

# --- 17. F3: de bron verandert na de rsync (een draaiende game): melden, niet afbreken ---------------------------------------------
if [[ $WH_OK == 1 ]]; then
    ONDER rsync-spel "$T/out9" || fail "F3: een overlay die na de rsync veranderde brak de run af: $(cat "$T/uit.txt")"
    grep -q "niet vergeleken" "$T/uit.txt" || fail "F3: de verandering na de rsync is niet gemeld"
    rm -rf "$RUN/writelayer/G/mnt/upper/later"
    # rechtstreeks: een xattr-verschil op een pad in beide bomen is een fout, bij rsync-code 24 een waarschuwing
    TOOL="$R/scripts/portable_tools.py"
    mkdir -p "$T/wg/s/d" "$T/wg/k/d"; setx "$T/wg/s/d" user.overlay.opaque y
    python3 -I "$TOOL" whiteouts "$T/wg/s" "$T/wg/k" >/dev/null 2>&1 && fail "F3: een xattr-verschil zonder rsync-code 24 gaf geen foutcode"
    python3 -I "$TOOL" whiteouts --rsync-rc 24 "$T/wg/s" "$T/wg/k" >/dev/null 2>"$T/wg/err" || fail "F3: een xattr-verschil bij rsync-code 24 brak af"
    grep -q "let op" "$T/wg/err" || fail "F3: het verschil bij code 24 is niet gemeld"
    # alleen in de bron: een whiteout in een map die de kopie niet heeft, en een xattr-drager die ontbreekt
    mkdir -p "$T/wg/s2/nieuw" "$T/wg/s2/ook" "$T/wg/k2"; mknod "$T/wg/s2/nieuw/w" c 0 0; setx "$T/wg/s2/ook" user.overlay.opaque y
    python3 -I "$TOOL" whiteouts "$T/wg/s2" "$T/wg/k2" >/dev/null 2>&1 || fail "F3: wat alleen in de bron staat brak de controle af"
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

#!/usr/bin/env bash
# Indelingstest van het releasearchief: tools/dev/release-assemble.sh op een nagemaakte installatieboom met nagemaakte onderdelen en
# een nagemaakte patches-map (met dossierbestanden), plus controles op make-release.sh. Geen containers, geen netwerk.
# Regels: GEEN install.sh (de app installeert), wel RavenGun.desktop naast bin/ravengun, README.md, LICENSE, bin/, lib/, share/;
# elk "bundled" onderdeel van packaging/components.json staat in share/ravengun/components/<id>/ met de bestanden die de
# catalogus verwacht (detect); in share/ravengun/patches alleen de levering, geen bewijs.
#
# Gebruik: release_layout_test.sh <repo>        (de echte patches-map wordt extra gecontroleerd als <repo>/patches bestaat)
set -u
REPO="$(cd "$1" && pwd)"
T="$(mktemp -d /tmp/rg-release-layout-XXXXXX)"; trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
command -v python3 >/dev/null || { echo "overgeslagen: python3 ontbreekt"; exit 77; }

# --- statische controles ----------------------------------------------------------------------------------------------------------
[[ ! -e $REPO/packaging/release/install.sh ]] || fail "packaging/release/install.sh bestaat nog"
[[ -f $REPO/packaging/release/RavenGun.desktop ]] || fail "packaging/release/RavenGun.desktop ontbreekt"
grep -qx 'Exec=.*bin/ravengun.*%k' "$REPO/packaging/release/RavenGun.desktop" || fail "het startbestand start bin/ravengun niet (relatief aan %k)"
grep -q '^Terminal=false' "$REPO/packaging/release/RavenGun.desktop" || fail "het startbestand moet zonder terminal starten"
grep -n 'install\.sh' "$REPO/tools/dev/make-release.sh" "$REPO/README.md" | grep -v 'install-user\.sh' && fail "make-release.sh of README.md noemt nog install.sh"

# --- nagemaakte invoer -------------------------------------------------------------------------------------------------------------
S="$T/stage/ravengun-0.0.0-x86_64-steamos"; mkdir -p "$S/bin" "$S/lib" "$S/share/ravengun"
for b in ravengun ravengund ravengunctl; do printf '#!/bin/sh\n' > "$S/bin/$b"; chmod 755 "$S/bin/$b"; done
echo lib > "$S/lib/libx.so"; echo MIT > "$S/share/ravengun/LICENSE"

python3 -I - "$REPO/packaging/components.json" "$T/comp" <<'EOF' || exit 1
import json, os, sys
cat = json.load(open(sys.argv[1]))["components"]
out = sys.argv[2]
for c in cat:
    if c["kind"] != "bundled":
        continue
    dest = c["dest"]
    for d in c["detect"]:
        assert d.startswith(dest + "/"), (c["id"], d)
        rel = d[len(dest) + 1:]
        base = os.path.join(out, c["id"])
        if rel == "current":                       # een versiemap met current -> map
            os.makedirs(os.path.join(base, "rev1"), exist_ok=True)
            open(os.path.join(base, "rev1", "x"), "w").write("x")
            if not os.path.islink(os.path.join(base, "current")):
                os.symlink("rev1", os.path.join(base, "current"))
        else:
            os.makedirs(os.path.dirname(os.path.join(base, rel)), exist_ok=True)
            open(os.path.join(base, rel), "w").write("x")
EOF

# Patches: levering en dossier door elkaar (zoals de patches-repo).
P="$T/patches"
mkdir -p "$P/GameA/dropin" "$P/GameA/data/x" "$P/GameA/seed" "$P/GameA/logs-run1" "$P/GameA/bewijs-1" "$P/_defaults" "$P/GameB"
for f in GameA/game.ini GameA/patches.ini GameA/native.json GameA/options.json GameA/unity.json GameA/rio-invoer.ini GameA/README.md GameA/dropin/dropin.ini \
         GameA/dropin/lib.so GameA/data/x/y.rules GameA/seed/CFG.dat _defaults/OpenParrot.patches.ini GameB/game.ini; do echo x > "$P/$f"; done
mkdir -p "$P/GameA/dropin/ravengun-hasp/memory" "$P/GameA/data/shaders" "$P/GameA/dropin/SAVE"
for f in GameA/NOTES.md GameA/shot.png GameA/run.log GameA/meet.sh GameA/meet.py GameA/uitvoer.txt GameA/logs-run1/a.log GameA/bewijs-1/b.png TOP-LEVEL.md probe.py; do echo x > "$P/$f"; done
for f in GameA/dropin/ravengun-hasp/memory/dump GameA/dropin/ravengun-hasp/dongle.json GameA/data/shaders/game.shader GameA/dropin/SAVE/io.sav; do echo private > "$P/$f"; done

"$REPO/tools/dev/release-assemble.sh" "$S" "$T/comp" "$P" > "$T/assemble.log" 2>&1 || { cat "$T/assemble.log" >&2; fail "release-assemble.sh faalde"; }

# --- indeling van het archief -------------------------------------------------------------------------------------------------------
[[ -z "$(find "$S" -name 'install*.sh' -o -name 'install' | grep .)" ]] || fail "er zit een installatiescript in het archief: $(find "$S" -name 'install*.sh')"
TOP="$(cd "$S" && ls -A | LC_ALL=C sort | tr '\n' ' ')"
[[ $TOP == "LICENSE README.md RavenGun.desktop bin lib patches share " ]] || fail "bovenste niveau klopt niet: $TOP"
[[ -x $S/RavenGun.desktop ]] || fail "RavenGun.desktop is niet uitvoerbaar (Dolphin start hem anders niet)"
[[ -x $S/bin/ravengun ]] || fail "bin/ravengun ontbreekt"
cmp -s "$REPO/README.md" "$S/README.md" || fail "README.md is niet die van de repo"
grep -q 'Exec=' "$S/RavenGun.desktop" || fail "RavenGun.desktop heeft geen Exec"

# Het startbestand doet echt iets: Exec uitvoeren met %k = pad van het bestand start <archief>/bin/ravengun (ook via een koppeling).
cat > "$S/bin/ravengun" <<EOF
#!/bin/sh
echo started-from \$0 > "$T/started"
EOF
EXEC="$(sed -n 's/^Exec=//p' "$S/RavenGun.desktop")"
ln -s "$S/RavenGun.desktop" "$T/koppeling.desktop"
for k in "$S/RavenGun.desktop" "$T/koppeling.desktop"; do
  rm -f "$T/started"
  ( cd / && sh -c "${EXEC//%k/$k}" ) || fail "het startbestand faalde ($k)"
  grep -qx "started-from $S/bin/ravengun" "$T/started" || fail "het startbestand startte niet $S/bin/ravengun ($k): $(cat "$T/started" 2>/dev/null)"
done

# --- onderdelen: exact wat de catalogus verwacht -----------------------------------------------------------------------------------
python3 -I - "$REPO/packaging/components.json" "$S/share/ravengun/components" <<'EOF' || exit 1
import json, os, sys
cat = json.load(open(sys.argv[1]))["components"]
root = sys.argv[2]
n = 0
for c in cat:
    if c["kind"] != "bundled":
        continue
    for d in c["detect"]:
        rel = d[len(c["dest"]) + 1:]
        p = os.path.join(root, c["id"], rel)
        if not os.path.lexists(p):
            sys.exit("FAIL: onderdeel %s: %s ontbreekt in share/ravengun/components/%s/" % (c["id"], rel, c["id"]))
        if rel == "current" and not os.path.isdir(p):
            sys.exit("FAIL: onderdeel %s: current wijst nergens heen" % c["id"])
    n += 1
if n < 5:
    sys.exit("FAIL: te weinig bundled onderdelen in de catalogus (%d)" % n)
EOF
# Elk bundled onderdeel heeft een producent in make-release.sh (lindbergh-loader alleen met --with-lindbergh).
for id in $(python3 -I -c 'import json,sys; print(" ".join(c["id"] for c in json.load(open(sys.argv[1]))["components"] if c["kind"]=="bundled"))' "$REPO/packaging/components.json"); do
  case $id in
    openparrot) pat='build-openparrot.sh' ;; jvsbridge) pat='build-jvsbridge.sh' ;; rg-pe-hook) pat='build-pe-hook.sh' ;;
    hasp) pat='build-hasp.sh' ;; memory) pat='build-memory.sh' ;; lindbergh-loader) pat='build-lindbergh-loader.sh' ;;
    *) fail "bundled onderdeel $id heeft geen producent in make-release.sh (catalogus en bouwscript lopen uiteen)" ;;
  esac
  grep -q "$pat" "$REPO/tools/dev/make-release.sh" || fail "make-release.sh bouwt $id niet ($pat)"
done
# Niets wat niet van ons is, hoort in de catalogus als bundled.
for id in teknoparrot-runtime demulshooter wine; do
  python3 -I -c 'import json,sys; sys.exit(0 if any(c["id"]==sys.argv[2] and c["kind"]=="bundled" for c in json.load(open(sys.argv[1]))["components"]) else 1)' "$REPO/packaging/components.json" $id && fail "$id mag niet in het archief"
done
[[ ! -e $S/share/ravengun/components/hasp/Halo\ Fireteam ]] || fail "dongle-gegevens in het archief"

# --- patches: alleen de levering ---------------------------------------------------------------------------------------------------
D="$S/patches"
[[ -d $D && ! -L $D ]] || fail "patches/ moet een echte map naast bin/ zijn"
[[ "$(readlink "$S/share/ravengun/patches")" == ../../patches ]] || fail "relatieve compatibiliteitskoppeling ontbreekt"
for f in GameA/game.ini GameA/patches.ini GameA/native.json GameA/options.json GameA/unity.json GameA/rio-invoer.ini GameA/README.md GameA/dropin/dropin.ini GameA/dropin/lib.so \
         GameA/data/x/y.rules GameA/seed/CFG.dat _defaults/OpenParrot.patches.ini GameB/game.ini; do
  [[ -f $D/$f ]] || fail "patches: $f ontbreekt in het archief"
done
for f in GameA/NOTES.md GameA/shot.png GameA/run.log GameA/meet.sh GameA/meet.py GameA/uitvoer.txt GameA/logs-run1 GameA/bewijs-1 TOP-LEVEL.md probe.py; do
  [[ ! -e $D/$f ]] || fail "patches: dossierbestand $f zit in het archief"
done
# De repository is privé: dongle- en HASP-bestanden horen juist WEL in het archief.
for f in GameA/dropin/ravengun-hasp/memory/dump GameA/dropin/ravengun-hasp/dongle.json GameA/data/shaders/game.shader GameA/dropin/SAVE/io.sav; do
  [[ -e $D/$f ]] || fail "patches: privébestand $f ontbreekt in het archief"
done
[[ -z "$(find "$D" -type d -empty)" ]] || fail "patches: lege mappen"

# De echte patches-map (alleen als hij er is): precies de levering, en niets uit het dossier.
if [[ -d "$REPO/patches" ]]; then
  R="$T/real"; mkdir -p "$T/stage2"
  python3 -I "$REPO/tools/dev/ship-patches.py" "$REPO/patches" "$R" > /dev/null || fail "ship-patches.py faalde op de echte patches"
  VREEMD="$(cd "$R" && find . -type f | sed 's|^\./||' | grep -Ev '^[^/]+/([^/]+\.ini|native\.json|unity\.json|options\.json|dongle\.json|README\.md|heap\.tsv)$|^[^/]+/(dropin|data|seed|private)/' || true)"
  [[ -z $VREEMD ]] || { echo "$VREEMD" | head >&2; fail "echte patches: iets buiten de levering meegekomen"; }
  BEWIJS="$(cd "$R" && find . -type f \( -name '*.png' -o -name '*.log' -o -name '*.md' ! -name README.md \) | grep -v '/dropin/\|/data/\|/seed/' || true)"
  [[ -z $BEWIJS ]] || { echo "$BEWIJS" | head >&2; fail "echte patches: bewijsbestanden meegekomen"; }
  [[ -f $R/LGI/game.ini ]] || fail "echte patches: LGI/game.ini ontbreekt"
  NODIG="$(python3 -I -c 'import importlib.util,os,sys; s=importlib.util.spec_from_file_location("ship",sys.argv[1]); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); root=sys.argv[2]; print(sum(bool(m.LEVERING.match(r)) for dp,_,fs in os.walk(root) for f in fs for r in [os.path.relpath(os.path.join(dp,f),root)]))' "$REPO/tools/dev/ship-patches.py" "$REPO/patches")"
  GEKOPIEERD="$(cd "$R" && find . -type f | wc -l)"
  [[ $NODIG -eq $GEKOPIEERD ]] || fail "echte patches: $GEKOPIEERD bestanden gekopieerd, $NODIG verwacht"
  echo "(echte patches: $GEKOPIEERD bestanden in de levering)"
fi

echo "releaseindelingstest: alles in orde"
exit 0

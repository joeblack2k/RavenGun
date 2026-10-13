#!/usr/bin/env bash
# Installatietest (A9): installeert vanuit de repo in een tijdelijke prefix met een tijdelijke HOME (ook alle XDG-paden), zet de
# installatie nog eens door naar een tweede prefix (de route van het releasearchief), en controleert dat engine en app vanuit de
# geïnstalleerde kopie werken, zonder de repo; daarna de uninstall. Raakt de echte installatie, de echte units en de draaiende
# engine NIET: HOME en alle XDG-mappen zijn tijdelijk, systemctl en pkexec zijn namaak-programma's die alleen loggen.
#
# Gebruik: install_test.sh <repo> <build-map> <app-build-map>        (exit 77 = overgeslagen: build of app ontbreekt)
set -u
REPO="$(cd "$1" && pwd)"; BUILD="$(cd "$2" && pwd)"; APPBUILD="$3"
[[ -x $BUILD/ravengund && -x $BUILD/ravengunctl ]] || { echo "overgeslagen: ravengund/ravengunctl niet gebouwd"; exit 77; }
[[ -x $APPBUILD/ravengun ]] || { echo "overgeslagen: de app is niet gebouwd ($APPBUILD/ravengun; scripts/build-app.sh)"; exit 77; }
APPBUILD="$(cd "$APPBUILD" && pwd)"
command -v cmake >/dev/null || { echo "overgeslagen: cmake ontbreekt"; exit 77; }

T="$(mktemp -d /tmp/rg-install-XXXXXX)"
ENGINE_PID=""
fail() { echo "FAIL: $*" >&2; [[ -z $ENGINE_PID ]] || kill "$ENGINE_PID" 2>/dev/null; sed -n '1,40p' "$T/engine.log" 2>/dev/null | sed 's/^/  engine: /' >&2; rm -rf "$T"; exit 1; }
trap '[[ -z $ENGINE_PID ]] || kill "$ENGINE_PID" 2>/dev/null; rm -rf "$T"' EXIT

H="$T/home"; mkdir -p "$H" "$T/run" "$T/bin" "$H/Desktop"; chmod 700 "$T/run"
cat > "$T/bin/systemctl-stub" <<EOF
#!/bin/sh
echo "\$@" >> "$T/systemctl.log"
case "\$*" in *is-active*) exit 3;; esac
exit 0
EOF
cat > "$T/bin/pkexec-stub" <<EOF
#!/bin/sh
echo "\$@" >> "$T/pkexec.log"
exit 0
EOF
chmod +x "$T/bin/systemctl-stub" "$T/bin/pkexec-stub"

# Schone omgeving: alleen wat nodig is, alles naar de tijdelijke map.
E=(env -i PATH=/usr/bin:/bin HOME="$H" XDG_CONFIG_HOME="$H/.config" XDG_DATA_HOME="$H/.local/share" XDG_STATE_HOME="$H/.local/state"
   XDG_RUNTIME_DIR="$T/run" RAVENGUN_SYSTEMCTL="$T/bin/systemctl-stub" RAVENGUN_PKEXEC="$T/bin/pkexec-stub")
P="$H/.local/opt/ravengun"; D="$P/share/ravengun"

# --- 1. Installeren vanuit de repo --------------------------------------------------------------------------------------------
cd "$T" || exit 1
"${E[@]}" "$REPO/scripts/install-user.sh" --no-start --build-dir "$BUILD" --app-build-dir "$APPBUILD" > "$T/install.log" 2>&1 || { cat "$T/install.log" >&2; fail "install-user.sh (repo) faalde"; }
for f in bin/ravengund bin/ravengunctl bin/ravengun share/ravengun/scripts/play-halo.sh share/ravengun/scripts/install-user.sh \
         share/ravengun/scripts/uninstall-user.sh share/ravengun/scripts/rg-paths.sh share/ravengun/patches/LGI/game.ini share/ravengun/patches/_defaults/OpenParrot.patches.ini \
         share/ravengun/packaging/systemd/ravengund.service.in share/ravengun/packaging/udev/70-ravengun.rules \
         share/ravengun/docs/user-guide.md share/ravengun/LICENSE share/ravengun/THIRD_PARTY.md share/ravengun/VERSION; do
  [[ -e $P/$f ]] || fail "ontbreekt in de installatie: $f"
done
[[ ! -e $D/scripts/build-app.sh && ! -e $D/scripts/make-release.sh ]] || fail "ontwikkelscripts horen niet in de installatie"
[[ ! -e $D/packaging/containers/qt-app ]] || fail "packaging/containers/qt-app hoort niet in de installatie"
ls "$P"/lib/libpsmoveapi.so >/dev/null 2>&1 && HAVE_PSM=1 || HAVE_PSM=0
if [[ -e $BUILD/librg_comm_shim.so ]]; then [[ -e $P/lib/librg_comm_shim.so && -e $P/lib/librg_rt_input.so ]] || fail "de LD_PRELOAD-hulpen ontbreken in lib/"; fi
ls "$H"/.local/opt/ 2>/dev/null | grep -q '\.new\.\|\.old\.' && fail "tijdelijke mappen achtergebleven"

# Units en snelkoppelingen wijzen naar de installatie, niet naar de repo.
U="$H/.config/systemd/user"
grep -qx "ExecStart=$P/bin/ravengund" "$U/ravengund.service" || fail "ravengund.service wijst niet naar $P/bin/ravengund"
grep -qx "ExecStart=$D/scripts/oled-saver.sh --loop" "$U/ravengun-oled-saver.service" || fail "oled-saver-unit wijst niet naar de installatie"
grep -qx "Exec=$P/bin/ravengun" "$H/.local/share/applications/ravengun-qt.desktop" || fail "menu-snelkoppeling wijst niet naar de installatie"
grep -qx "Exec=$P/bin/ravengun" "$H/Desktop/ravengun-qt.desktop" || fail "bureaubladsnelkoppeling ontbreekt of wijst elders heen"
[[ "$(readlink "$H/.local/bin/ravengun")" == "$P/bin/ravengun" ]] || fail "koppeling ~/.local/bin/ravengun ontbreekt"
if grep -rIl "$REPO" "$U" "$H/.local/share/applications" "$H/Desktop" "$P" 2>/dev/null | grep -q .; then
  grep -rIl "$REPO" "$U" "$H/.local/share/applications" "$H/Desktop" "$P" >&2; fail "de installatie noemt het repopad $REPO"
fi
# De patches per game: in de installatie staat ALLEEN wat de lader, de daemon en de app lezen (<Game>/*.ini,
# <Game>/dongle.json, <Game>/README.md en <Game>/dropin/). De patches-repo is ook het dossier (logboeken van benchruns,
# metingen, meetscripts, notities, foto's); dat is bewijs en gaat niet mee. De controle op het repopad hierboven blijft
# over de HELE installatie gaan, dus ook over deze bestanden: een game.ini of een drop-in die een ontwikkelpad noemt
# is een echt lek. Deze controle hieronder zorgt dat het dossier die test niet meer kan breken, en dat er niets
# verdwijnt wat de lader nodig heeft.
if [[ -d "$REPO/patches" ]]; then
  SRC="$(cd "$REPO/patches" && pwd -P)"
  [[ -d "$D/patches" ]] || fail "de patches per game zijn niet geinstalleerd"
  VREEMD="$(cd "$D/patches" && find . -type f | sed 's|^\./||' | grep -Ev '^[^/]+/([^/]+\.ini|dongle\.json|README\.md)$|^[^/]+/dropin/' || true)"
  [[ -z "$VREEMD" ]] || { echo "$VREEMD" | head -20 >&2; fail "in de installatie staat iets uit de patches-repo dat de lader niet leest (dossier hoort niet mee)"; }
  LEEG="$(find "$D/patches" -type d -empty)"
  [[ -z "$LEEG" ]] || { echo "$LEEG" | head >&2; fail "lege mappen in de geinstalleerde patches"; }
  # Niets kwijt: elk bestand dat de lader leest staat er, met dezelfde inhoud.
  NODIG="$(cd "$SRC" && find . -type f | sed 's|^\./||' | grep -E '^[^/]+/([^/]+\.ini|dongle\.json|README\.md)$|^[^/]+/dropin/' | sort)"
  [[ -n "$NODIG" ]] || fail "de patches-map bevat geen enkele game.ini"
  while IFS= read -r f; do
    cmp -s "$SRC/$f" "$D/patches/$f" || fail "ontbreekt of wijkt af in de installatie: patches/$f"
  done <<< "$NODIG"
  [[ "$(cd "$D/patches" && find . -type f | wc -l)" -eq "$(wc -l <<< "$NODIG")" ]] || fail "het aantal geinstalleerde patchbestanden klopt niet"
fi
grep -q "enable ravengund.service" "$T/systemctl.log" || fail "de engine-service is niet geactiveerd (systemctl-log)"
grep -q "now" "$T/systemctl.log" && fail "met --no-start mag de service niet gestart worden"

# De bibliotheken komen uit de installatie (rpath $ORIGIN/../lib).
if [[ $HAVE_PSM -eq 1 ]]; then
  PSM="$(ldd "$P/bin/ravengund" 2>&1 | awk '/libpsmoveapi.so =>/ {print $3}')"
  [[ -n $PSM && "$(readlink -f "$PSM")" == "$(readlink -f "$P/lib/libpsmoveapi.so")" ]] || fail "ravengund laadt libpsmoveapi niet uit $P/lib (ldd: $PSM)"
fi
ldd "$P/bin/ravengun" 2>&1 | grep -q 'not found' && fail "de app mist bibliotheken"

# --- 2. De app en de engine vanuit de installatie, zonder de repo ---------------------------------------------------------------
cd / || exit 1
OUT="$("${E[@]}" QT_QPA_PLATFORM=offscreen "$P/bin/ravengun" --print-paths 2>&1)" || fail "ravengun --print-paths faalde: $OUT"
echo "$OUT" | grep -qx "data-dir: $D" || fail "de app vindt de datamap niet in de installatie: $OUT"
echo "$OUT" | grep -qx "engine: $P/bin/ravengund" || fail "de app vindt ravengund niet naast zich: $OUT"
OUT="$("${E[@]}" QT_QPA_PLATFORM=offscreen RAVENGUN_DATA_DIR="$REPO" "$P/bin/ravengun" --print-paths 2>&1)"
echo "$OUT" | grep -qx "data-dir: $REPO" || fail "RAVENGUN_DATA_DIR gaat niet voor: $OUT"

PORT="$(python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')"
"${E[@]}" "$P/bin/ravengund" --no-hid --no-camera --no-bias-store --web-port "$PORT" --controllers-fake "$REPO/tests/fake_controllers.json" \
  --tp-dir "$T/tp" --games-dry-run > "$T/engine.log" 2>&1 &
ENGINE_PID=$!
for _ in $(seq 1 60); do [[ -S $T/run/ravengun/ravengund.sock ]] && break; sleep 0.1; done
[[ -S $T/run/ravengun/ravengund.sock ]] || fail "de engine uit de installatie start niet"
grep -q "datamap: $D (installatie" "$T/engine.log" || fail "de engine meldt niet dat hij de datamap uit de installatie gebruikt"
"${E[@]}" "$P/bin/ravengunctl" status --json | grep -q '"ok":true' || fail "ravengunctl (geïnstalleerd) kan de engine niet bereiken"
# De engine las zijn packaging-bestanden uit de installatie: een waarschuwing over ontbrekende bestanden mag niet voorkomen.
grep -q "waarschuwing" "$T/engine.log" && fail "de engine geeft een waarschuwing: $(grep waarschuwing "$T/engine.log" | head -3)"

mkdir -p "$T/shots" "$T/steam"
"${E[@]}" QT_QPA_PLATFORM=offscreen RAVENGUN_NO_GAMEPAD=1 RAVENGUN_STEAM_ROOT="$T/steam" RAVENGUN_SYSTEMCTL=/bin/false RAVENGUN_PKEXEC=/bin/false \
  "$P/bin/ravengun" --screenshot-dir "$T/shots" --no-scan > "$T/app.log" 2>&1 || { cat "$T/app.log" >&2; fail "de app (offscreen, --screenshot-dir) faalde"; }
NSHOT="$(ls "$T/shots"/*.png 2>/dev/null | wc -l)"
[[ $NSHOT -ge 10 ]] || fail "te weinig schermafdrukken van de app ($NSHOT)"
kill "$ENGINE_PID"; wait "$ENGINE_PID" 2>/dev/null; ENGINE_PID=""

# --- 3. Opnieuw installeren (bijwerken) en doorzetten naar een tweede prefix (de route van het releasearchief) --------------------
"${E[@]}" "$REPO/scripts/install-user.sh" --no-start --build-dir "$BUILD" --app-build-dir "$APPBUILD" > "$T/install2.log" 2>&1 || { cat "$T/install2.log" >&2; fail "tweede installatie (bijwerken) faalde"; }
P2="$H/.local/opt/ravengun2"
"${E[@]}" "$D/scripts/install-user.sh" --prefix "$P2" --no-start > "$T/install3.log" 2>&1 || { cat "$T/install3.log" >&2; fail "installatie vanuit een geïnstalleerde boom faalde"; }
[[ -x $P2/bin/ravengund && -f $P2/share/ravengun/scripts/play-halo.sh ]] || fail "de gekopieerde installatie is onvolledig"
grep -qx "ExecStart=$P2/bin/ravengund" "$U/ravengund.service" || fail "de unit wijst na de tweede installatie niet naar $P2"
OUT="$("${E[@]}" QT_QPA_PLATFORM=offscreen "$P2/bin/ravengun" --print-paths 2>&1)"
echo "$OUT" | grep -qx "data-dir: $P2/share/ravengun" || fail "de tweede installatie vindt zijn datamap niet: $OUT"
# Een bestaande map die niet van ons is, blijft ongemoeid.
mkdir -p "$H/andermans" && echo x > "$H/andermans/bestand"
"${E[@]}" "$D/scripts/install-user.sh" --prefix "$H/andermans" --no-start >/dev/null 2>&1 && fail "install-user.sh overschreef een vreemde map"
[[ -f $H/andermans/bestand ]] || fail "een vreemde map is aangetast"
"${E[@]}" "$D/scripts/install-user.sh" --prefix /usr/local --no-start >/dev/null 2>&1 && fail "install-user.sh accepteerde /usr/local"

# --- 4. Verwijderen --------------------------------------------------------------------------------------------------------------
mkdir -p "$H/.config/ravengun" "$H/.local/state/ravengun" "$H/.local/share/ravengun/runtime"
echo '{}' > "$H/.config/ravengun/config.json"; echo x > "$H/.local/share/ravengun/runtime/keep"
: > "$T/pkexec.log"
"${E[@]}" "$P2/share/ravengun/scripts/uninstall-user.sh" --yes > "$T/uninstall.log" 2>&1 || { cat "$T/uninstall.log" >&2; fail "uninstall-user.sh faalde"; }
[[ ! -e $P2 ]] || fail "de installatiemap is niet verwijderd"
[[ ! -e $U/ravengund.service && ! -e $U/ravengun-oled-saver.service ]] || fail "de units zijn niet verwijderd"
[[ ! -e $H/.local/share/applications/ravengun-qt.desktop && ! -e $H/Desktop/ravengun-qt.desktop ]] || fail "de snelkoppelingen zijn niet verwijderd"
[[ ! -L $H/.local/bin/ravengun ]] || fail "de koppeling in ~/.local/bin staat er nog"
[[ -f $H/.config/ravengun/config.json ]] || fail "de instellingen zijn verwijderd terwijl ze bewaard moesten blijven"
[[ -f $H/.local/share/ravengun/runtime/keep ]] || fail "de gebruikersgegevens (runtime) zijn aangetast"
grep -q "disable --now ravengund.service" "$T/systemctl.log" || fail "de service is niet gestopt (systemctl-log)"
# Root-onderdelen: alleen als ze op deze machine staan (dan vraagt de uninstall via pkexec om --rollback met het script uit de installatie).
if [[ -e /etc/udev/rules.d/70-ravengun.rules || -e /etc/udev/rules.d/71-ravengun-uhid.rules || -e /etc/udev/rules.d/72-ravengun-dongle.rules || -e /opt/ravengun/bin/ravengun-bt-helper ]]; then
  grep -qx "$P2/share/ravengun/scripts/install-permissions.sh --rollback" "$T/pkexec.log" || fail "de uninstall riep pkexec niet aan met --rollback uit de installatie: $(cat "$T/pkexec.log")"
else
  echo "(geen systeemonderdelen op deze machine: pkexec-controle overgeslagen)"
fi
# Nogmaals, met --remove-config en --keep-root, op de eerste installatie.
: > "$T/pkexec.log"
"${E[@]}" "$D/scripts/uninstall-user.sh" --yes --remove-config --keep-root > "$T/uninstall2.log" 2>&1 || { cat "$T/uninstall2.log" >&2; fail "uninstall-user.sh --remove-config faalde"; }
[[ ! -e $P && ! -e $H/.config/ravengun && ! -e $H/.local/state/ravengun ]] || fail "--remove-config liet bestanden staan"
[[ ! -s $T/pkexec.log ]] || fail "--keep-root mag pkexec niet aanroepen"
[[ -f $H/.local/share/ravengun/runtime/keep ]] || fail "de gebruikersgegevens (runtime) zijn aangetast door --remove-config"

echo "installatietest: alles in orde"
exit 0

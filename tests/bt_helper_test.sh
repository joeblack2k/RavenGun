#!/bin/sh
# Invoercontrole van scripts/ravengun-bt-helper zonder root: ongeldige invoer moet exit 2 geven (vóór de rootcontrole),
# geldige invoer exit 3 ("moet als root"), en er mag niets op het systeem gebeuren. Plus de sudoers-regel van
# scripts/install-bt-helper.sh (visudo -cf, als visudo er is) en --check. Draait nooit iets met sudo.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
H="$ROOT/scripts/ravengun-bt-helper"
I="$ROOT/scripts/install-bt-helper.sh"
fails=0
n=0
expect() {  # $1 = verwachte exitcode, daarna de argumenten
    want=$1; shift
    n=$((n + 1))
    env -i /bin/bash -p "$H" "$@" >/dev/null 2>&1
    got=$?
    if [ "$got" != "$want" ]; then echo "FAIL: ravengun-bt-helper $* -> $got (verwacht $want)"; fails=$((fails + 1)); fi
}
if [ "$(id -u)" = 0 ]; then echo "deze test draait niet als root"; exit 77; fi
bash -n "$H" || { echo "FAIL: syntaxfout in de hulp"; exit 1; }
bash -n "$I" || { echo "FAIL: syntaxfout in het installatiescript"; exit 1; }

# Geldig (zonder root): exit 3.
expect 3 register 00:07:04:8C:70:AD move
expect 3 register 00:07:04:8C:70:AD move2
expect 3 register 00:06:F7:0E:C1:93 navigation
expect 3 unregister 00:07:04:8C:70:AD
# Verkeerde actie of aantal argumenten.
expect 2
expect 2 register
expect 2 register 00:07:04:8C:70:AD
expect 2 register 00:07:04:8C:70:AD move extra
expect 2 unregister 00:07:04:8C:70:AD move
expect 2 install 00:07:04:8C:70:AD move
expect 2 --help 00:07:04:8C:70:AD
expect 2 REGISTER 00:07:04:8C:70:AD move
# Ongeldige adressen (ook pogingen tot injectie en paden).
for a in 00:07:04:8c:70:ad 00:07:04:8C:70 00:07:04:8C:70:AD:11 00-07-04-8C-70-AD "00:07:04:8C:70:AD " " 00:07:04:8C:70:AD" \
         "00:07:04:8C:70:AD;id" '$(id)' "../../etc/passwd" "00:07:04:8C:70:AG" 00:00:00:00:00:00 FF:FF:FF:FF:FF:FF \
         "00:07:04:8C:70:AD
rm" ""; do
    expect 2 register "$a" move
    expect 2 unregister "$a"
done
# Ongeldige typen.
for t in "" dualshock MOVE "move;id" "move " navigation2 "../x"; do expect 2 register 00:07:04:8C:70:AD "$t"; done
# Geen omgevingsvariabelen: een PATH of BASH_ENV van buiten verandert niets aan de uitkomst.
n=$((n + 1))
env -i PATH=/nonexistent BASH_ENV=/nonexistent/evil /bin/bash -p "$H" register 00:07:04:8C:70:AD move >/dev/null 2>&1
[ $? = 3 ] || { echo "FAIL: omgeving beïnvloedt de hulp"; fails=$((fails + 1)); }

# Sudoers-regel: alleen het ene pad, NOPASSWD, gecontroleerd met visudo (als het er is; als gewone gebruiker kan visudo -cf).
TMP=$(mktemp)
"$I" --print-sudoers deck >"$TMP" || { echo "FAIL: --print-sudoers"; fails=$((fails + 1)); }
n=$((n + 1))
grep -q '^deck ALL=(root) NOPASSWD: /opt/ravengun/bin/ravengun-bt-helper register \*, /opt/ravengun/bin/ravengun-bt-helper unregister \*$' "$TMP" ||
    { echo "FAIL: sudoers-regel klopt niet"; fails=$((fails + 1)); }
n=$((n + 1))
[ "$(grep -c NOPASSWD "$TMP")" = 1 ] || { echo "FAIL: meer dan één NOPASSWD-regel"; fails=$((fails + 1)); }
if command -v visudo >/dev/null 2>&1; then
    n=$((n + 1))
    visudo -cf "$TMP" >/dev/null 2>&1 || { echo "FAIL: visudo keurt de regel af"; fails=$((fails + 1)); }
fi
rm -f "$TMP"
n=$((n + 1))
"$I" --print-sudoers 'root;id' >/dev/null 2>&1 && { echo "FAIL: ongeldige gebruikersnaam geaccepteerd"; fails=$((fails + 1)); }
# --apply zonder root weigert.
n=$((n + 1))
"$I" --apply >/dev/null 2>&1 && { echo "FAIL: --apply zonder root"; fails=$((fails + 1)); }

echo "$n controles, $fails mislukt"
[ "$fails" = 0 ]

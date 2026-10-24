#!/usr/bin/env bash
# luigi-com1-run.sh -- COM1 van Luigi's Mansion Arcade meten.
#
# Zet een pty op dosdevices/com1 van de Wine-prefix van het spel, laat een programma
# (standaard SendFlash.exe, optioneel VACUUM.exe) onder Wine draaien, en logt elke byte
# die over de poort gaat met een tijdstempel.
#
# WAT DIT ANTWOORDT
#   * Wat stuurt de game (of SendFlash) werkelijk naar COM1, en in welke volgorde?
#   * Wacht hij op antwoord voordat hij verdergaat, of schrijft hij blind?
#   * Is COM1 werkelijk de reden dat de game stopt, of alleen een detail?
# Wat het NIET is: een manier om een controle te omzeilen. We geven de game een echte
# seriele poort in plaats van een poort die ACCESS_DENIED geeft, en kijken wat hij doet.
#
# TWEE TRAPPEN
#   1. SendFlash.exe COM1 LMAIO.bin   (geen game, geen scherm, geen bench nodig)
#      Dit is het gereedschap uit game.bat. Het opent COM1, zet de DCB, wist de buffer,
#      en schrijft de firmware. Je krijgt het flitsprotocol in beeld zonder de game.
#   2. VACUUM.exe                     (de game; alleen als de bench vrij is)
#      Let op: met patch 0005 BLokkeert onze kern COM-namen, dus dan komt de game nooit
#      bij de pty. Wil je dit meten, bouw de kern dan zonder 0005 (zie README.md van deze
#      map) of gebruik de runtime-lader met een core zonder die patch.
#
# Alleen-lezen op de NAS. Dit script raakt alleen de prefix, de pty en /tmp aan.
set -u

HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${RG_WINEPREFIX:-$HOME/.local/share/ravengun/prefixes/shared/wine}"
WINEBIN="${RG_WINE_BIN:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin}"
WINESERVER="$WINEBIN/wineserver"
WINE="$WINEBIN/wine"
VIEW="${RG_VIEW:-$HOME/.local/share/ravengun/runtime/views/LuigisMansion}"
GST="${RG_GST:-$HOME/.local/share/ravengun/tools/gst-extra/plugins}"
SHIM="${RG_SHIM:-$HOME/.local/share/ravengun/runtime/lib/librg_comm_shim.so}"
LOGDIR="${RG_LOGDIR:-/tmp}"
LINK="${RG_COM_LINK:-$HOME/.local/share/ravengun/run/luigi-com1}"
TRAP="${1:-sendflash}"

STAMP=$(date +%Y%m%d-%H%M%S)
LOG="$LOGDIR/luigi-com1-$TRAP-$STAMP.log"
STDERR="$LOGDIR/luigi-com1-$TRAP-$STAMP.stderr.log"

zeg() { printf '\033[1m==\033[0m %s\n' "$*"; }
fout() { printf '\033[31m!!\033[0m %s\n' "$*" >&2; }

[[ -d "$PREFIX" ]] || { fout "prefix bestaat niet: $PREFIX"; exit 1; }
[[ -x "$WINE" ]]   || { fout "wine bestaat niet: $WINE"; exit 1; }
[[ -d "$VIEW" ]]   || { fout "view bestaat niet: $VIEW"; exit 1; }

dd="$PREFIX/dosdevices"
[[ -d "$dd" ]] || { fout "geen $dd"; exit 1; }

# --- 1. wineserver voorverwarmen ------------------------------------------------------
# Wine zet bij het starten van wineserver/wineboot ALLE dosdevices/com1..com32 opnieuw op
# /dev/ttyS*. Een link die we daarvoor maken is daarna weg. Dus eerst de wineserver van
# deze prefix starten en wineboot laten uitlopen, dan pas com1 omzetten. (Zelfde reden en
# zelfde aanpak als scripts/play-halo.sh, jvs_prewarm_prefix.)
zeg "wineserver voorverwarmen in $PREFIX (anders gooit Wine com1 terug naar /dev/ttyS0)"
WINEPREFIX="$PREFIX" WINEDEBUG=-all "$WINESERVER" -p 2>/dev/null || true
WINEPREFIX="$PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" timeout 120 \
  "$WINE" cmd /c exit >/dev/null 2>&1 || true
sleep 1

ORIG=""
[[ -L "$dd/com1" ]] && ORIG=$(readlink "$dd/com1")
zeg "huidige dosdevices/com1 -> ${ORIG:-<niets>}"

opruimen() {
  [[ -n "${PTYPID:-}" ]] && kill "$PTYPID" 2>/dev/null
  sleep 0.3
  # com1 terugzetten zoals het was
  if [[ -L "$dd/com1" && "$(readlink "$dd/com1")" == "$LINK" ]]; then
    if [[ -n "$ORIG" ]]; then ln -sfn "$ORIG" "$dd/com1"; else rm -f "$dd/com1"; fi
  fi
  [[ -n "${PTYPID:-}" ]] && wait "$PTYPID" 2>/dev/null
}
trap opruimen EXIT

# --- 2. pty-logger starten ------------------------------------------------------------
rm -f "$LINK"
zeg "pty starten (log: $LOG)"
ANTWOORD=()
[[ -n "${RG_COM_REPLY_HEX:-}" ]] && ANTWOORD+=(--reply-hex "$RG_COM_REPLY_HEX")
[[ "${RG_COM_ECHO:-0}" == 1 ]] && ANTWOORD+=(--echo)
[[ -n "${RG_COM_REPLY_ONCE:-}" ]] && ANTWOORD+=(--reply-once)
python3 "$HIER/luigi-com1-pty.py" --link "$LINK" --log "$LOG" \
  --max-seconds "${RG_COM_MAX:-180}" --banner "trap=$TRAP prefix=$PREFIX" \
  "${ANTWOORD[@]}" >"$LOGDIR/luigi-com1-pty-$STAMP.out" 2>&1 &
PTYPID=$!

for _ in $(seq 1 50); do [[ -L "$LINK" ]] && break; sleep 0.1; done
[[ -L "$LINK" ]] || { fout "de pty is niet opgekomen; zie $LOGDIR/luigi-com1-pty-$STAMP.out"; exit 1; }
SLAVE=$(readlink "$LINK")
zeg "pty-slaaf $SLAVE  ->  $dd/com1"

# --- 3. com1 omzetten -----------------------------------------------------------------
ln -sfn "$LINK" "$dd/com1"
[[ "$(readlink "$dd/com1")" == "$LINK" ]] || { fout "kon com1 niet omzetten"; exit 1; }

# --- 4. het programma draaien ---------------------------------------------------------
# Bewust GEEN TP_*-variabelen: die veranderen de route van de lader, en dat is een andere
# meting. Wel de comm-shim, want een pty kent TIOCMGET niet en GetCommModemStatus hoort
# niet te falen op iets anders dan het bord.
OMGEVING=(WINEPREFIX="$PREFIX" WINEDEBUG="${RG_WINEDEBUG:--all}")
[[ -r "$SHIM" ]] && OMGEVING+=(LD_PRELOAD="$SHIM") \
  || zeg "let op: $SHIM ontbreekt; GetCommModemStatus faalt dan op de pty (onschuldig voor deze meting)"

case "$TRAP" in
  sendflash)
    [[ -f "$VIEW/SendFlash.exe" ]] || { fout "SendFlash.exe niet in $VIEW"; exit 1; }
    [[ -f "$VIEW/LMAIO.bin" ]]     || { fout "LMAIO.bin niet in $VIEW"; exit 1; }
    zeg "SendFlash.exe COM1 LMAIO.bin  (werkmap $VIEW)"
    ( cd "$VIEW" && env "${OMGEVING[@]}" timeout "${RG_RUN_MAX:-180}" \
        "$WINE" SendFlash.exe COM1 LMAIO.bin ) >"$STDERR" 2>&1
    RC=$?
    ;;
  game)
    [[ -f "$VIEW/exe/x64/VACUUM.exe" ]] || { fout "VACUUM.exe niet in $VIEW/exe/x64"; exit 1; }
    OMGEVING+=(GST_PLUGIN_PATH="$GST")
    zeg "VACUUM.exe  (werkmap $VIEW/exe/x64; GST_PLUGIN_PATH=$GST)"
    ( cd "$VIEW/exe/x64" && env "${OMGEVING[@]}" timeout "${RG_RUN_MAX:-180}" \
        "$WINE" VACUUM.exe ) >"$STDERR" 2>&1
    RC=$?
    ;;
  *)
    fout "onbekende trap '$TRAP' (sendflash|game)"; exit 2 ;;
esac
zeg "klaar, exitcode $RC (stderr: $STDERR)"

# --- 5. rustig uitlezen ---------------------------------------------------------------
sleep 1
kill "$PTYPID" 2>/dev/null; wait "$PTYPID" 2>/dev/null || true; PTYPID=""

zeg "samenvatting"
python3 - "$LOG" <<'PY'
import sys, collections
pad = sys.argv[1]
rx = tx = 0
regels = []
for r in open(pad, encoding="utf-8", errors="replace"):
    if r.startswith("#") or r.startswith("=="):
        continue
    d = r.split("\t")
    if len(d) < 4:
        continue
    if d[2].startswith("<<<"):
        rx += len(bytes.fromhex(d[3])) if d[3].strip() else 0
        regels.append(("SPEL", d[0], d[3].strip()))
    elif d[2].startswith(">>>"):
        tx += len(bytes.fromhex(d[3])) if d[3].strip() else 0
        regels.append(("WIJ ", d[0], d[3].strip()))
print("  van het spel: %d B in %d blokken" % (rx, sum(1 for k,_,_ in regels if k=="SPEL")))
print("  van ons:      %d B" % tx)
if not regels:
    print("  GEEN VERKEER OP COM1. Dat is zelf een uitkomst: het programma raakte de poort niet aan.")
else:
    print("  eerste 20 blokken:")
    for k, t, h in regels[:20]:
        hh = h if len(h) <= 96 else h[:96] + " ..."
        print("    %s %s  %s" % (t, k, hh))
PY
zeg "volledig log: $LOG"
zeg "stderr van het programma: $STDERR"


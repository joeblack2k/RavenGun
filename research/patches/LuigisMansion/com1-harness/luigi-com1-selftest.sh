#!/usr/bin/env bash
# Zelftest van de pty-logger: geen Wine, geen prefix, geen game. Bewijst dat de pty, de
# link en het loggen werken, en dat een antwoord terugkomt bij de schrijver.
set -u
D=$(mktemp -d)
H="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINK="$D/com1"; LOG="$D/log.txt"
echo "testmap: $D"

python3 "$H/luigi-com1-pty.py" --link "$LINK" --log "$LOG" --max-seconds 15 \
  --reply-hex "06 41 43 4b" >"$D/pty.out" 2>&1 &
P=$!
for _ in $(seq 1 50); do [[ -L "$LINK" ]] && break; sleep 0.1; done
[[ -L "$LINK" ]] || { echo "FOUT: geen link"; cat "$D/pty.out"; exit 1; }
echo "link $LINK -> $(readlink "$LINK")"

python3 - "$LINK" <<'PY'
import os, sys, time
p = sys.argv[1]
fd = os.open(p, os.O_RDWR | os.O_NOCTTY)
os.write(fd, bytes.fromhex("aa 55 01 02 03"))      # 'firmware'
time.sleep(0.4)
os.write(fd, bytes.fromhex("10 20"))               # nog een blok
time.sleep(0.4)
os.set_blocking(fd, False)
try:
    antwoord = os.read(fd, 64)
except BlockingIOError:
    antwoord = b""
print("schrijver ontving terug:", antwoord.hex(" "), "(%d B)" % len(antwoord))
os.close(fd)
PY

sleep 0.5
kill $P 2>/dev/null; wait $P 2>/dev/null
echo "--- log ---"; cat "$LOG"
echo "--- controle ---"
grep -q "aa 55 01 02 03" "$LOG" && echo "OK  heenweg gelogd" || { echo "FOUT heenweg"; exit 1; }
grep -q "10 20" "$LOG" && echo "OK  tweede blok gelogd" || { echo "FOUT tweede blok"; exit 1; }
grep -q ">>> wij antwoorden" "$LOG" && echo "OK  antwoord verstuurd" || { echo "FOUT antwoord"; exit 1; }
rm -rf "$D"
echo "ZELFTEST GESLAAGD"


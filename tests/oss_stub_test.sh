#!/usr/bin/env bash
# librg-oss-stub.so in afspeelmodus (RG_OSS_PLAY=1, tools/rg-oss-stub/): bouwt de shim en tools/rg-oss-stub/oss_probe.c (32-bit,
# gedraagt zich als de OSS-code van Target: Terror Gold en Jurassic Park) en meet:
#  - de pacing: het proefprogramma schrijft een fragment zodra GETOSPACE er een vrij meldt; over 3 s moet dat ongeveer
#    rate*2 = 44100 bytes/s zijn (grenzen 36000..54000: het eerste vulwerk van de ring telt mee). Zonder pacing (oude
#    /dev/null-stub) zou het duizenden keren sneller zijn.
#  - dit gebeurt met een onbereikbare PulseAudio-server (de virtuele klok), zodat de test niets afspeelt en overal draait.
#  - een mixer-ioctl op de DSP-fd mag de afspeelinstellingen niet veranderen (het spel doet SetMixer op de DSP-fd).
# Skip (77) zonder 32-bits C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -m32 -shared -fPIC -O2 -Wall -Wextra -Werror -o "$T/stub.so" "$ROOT/tools/rg-oss-stub/rg_oss_stub.c" -ldl -lpthread 2>"$T/cc.err" \
  || { grep -q -E "stubs-32|cannot find|-lgcc|32-bit" "$T/cc.err" && { echo "overgeslagen: geen 32-bits bouw"; exit 77; }; cat "$T/cc.err"; echo "FAIL: de shim bouwt niet"; exit 1; }
"$CC" -m32 -O2 -Wall -o "$T/probe" "$ROOT/tools/rg-oss-stub/oss_probe.c" -lm 2>"$T/cc2.err" \
  || { cat "$T/cc2.err"; echo "FAIL: het proefprogramma bouwt niet"; exit 1; }
OUT=$(env -u PULSE_SERVER PULSE_SERVER=unix:/nonexistent/native XDG_RUNTIME_DIR=/nonexistent LD_PRELOAD="$T/stub.so" RG_OSS_PLAY=1 "$T/probe" 3 0 2>&1) \
  || { echo "$OUT"; echo "FAIL: het proefprogramma faalt"; exit 1; }
echo "$OUT"
BPS=$(echo "$OUT" | sed -n 's/.*= \([0-9]*\) B\/s.*/\1/p')
[[ -n "$BPS" && "$BPS" -ge 36000 && "$BPS" -le 54000 ]] || { echo "FAIL: pacing $BPS B/s buiten 36000..54000"; exit 1; }
echo "OK: pacing $BPS B/s"

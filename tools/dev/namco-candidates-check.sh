#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Controleert de kandidaten van docs/research/namco/patches/<Game>/ (TC5, HOTDSD) zonder TeknoParrot: legt ze over de echte patches-map
# (alleen lezen, in een tijdelijke kopie) en laat rg-direct een plan maken met een TeknoParrot-map die niet bestaat. Start niets.
#
# Gebruik: tools/dev/namco-candidates-check.sh [patches-map]   (standard: patches/ in this repo)
# Vereist: build/rg-direct (cmake -S . -B build && cmake --build build -j) en de gebouwde DLL's in de dropin-mappen
# (tools/dev/build-winusb.sh, tools/dev/build-allsio.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BASE="${1:-$ROOT/patches}"
[[ -x "$ROOT/build/rg-direct" ]] || { echo "build/rg-direct ontbreekt" >&2; exit 2; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cp -r "$BASE/." "$TMP/"
cp -r "$ROOT/docs/research/namco/patches/." "$TMP/"
export RAVENGUN_TP_DIR=/nonexistent
rc=0
for g in TC5 HOTDSD; do
  "$ROOT/build/rg-direct" --plan "$g" --patches "$TMP" > "$TMP/plan-$g.json" || true
  python3 -I - "$TMP/plan-$g.json" "$g" <<'EOF' || rc=1
import json, sys
d = json.load(open(sys.argv[1]))
g = sys.argv[2]
p = d.get("plan") or {}
ok = d.get("verdict") == "ok" and p.get("core_source") == "ravengun" and not p.get("tp") and not p.get("tp_files")
print(f"{g}: verdict={d.get('verdict')} core={p.get('core_source')} lader={p.get('loader')}")
for f in p.get("dropin_files", []):
    print("  drop-in:", f["name"], "->", f["dest"])
env = p.get("launch_env", {})
for k in ("WINEDLLOVERRIDES", "TP_PRELOAD_DLL", "TP_REMOTETHREAD"):
    if k in env: print(f"  {k} = {env[k]}")
print("  starts:", [s["name"] for s in p.get("starts", [])])
sys.exit(0 if ok else 1)
EOF
done
"$ROOT/build/rg-direct" --tp-refs --patches "$TMP" --expect-free TC5,HOTDSD | grep -E "^  (TC5|HOTDSD) " || rc=1
exit $rc

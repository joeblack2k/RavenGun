#!/usr/bin/env bash
# Zet een 32-bits GStreamer klaar voor Windows-games onder Wine die films afspelen via DirectShow/Media Foundation
# ([Launch] Gst32 = true, docs/gst32.md). SteamOS heeft geen lib32-gstreamer; Proton van Steam wel. Dit script maakt
#   ~/.local/share/ravengun/gst32/proton   -> files/lib/i386-linux-gnu van de nieuwste Proton met libgstlibav (GE-Proton eerst)
#   ~/.local/share/ravengun/gst32/plugins/ -> links naar de plug-ins, zonder protonmediaconverter (die wil Proton's eigen omgeving)
#   ~/.local/share/ravengun/gst32/lib/     -> links naar wat die plug-ins verder vragen en alleen in de Steam Linux Runtime (sniper)
#                                             staat (bv. libvpx.so.6); nooit de glibc van die runtime
# Niets wordt gekopieerd of gedownload; het script leest alleen wat Steam al heeft. Opnieuw draaien na een Proton-update.
#
#   scripts/setup-gst32.sh [DOEL]
set -euo pipefail
DEST="${1:-${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/gst32}"
STEAM="$HOME/.local/share/Steam"
pick=""
for d in $(ls -d "$STEAM"/compatibilitytools.d/GE-Proton*/files/lib/i386-linux-gnu 2>/dev/null | sort -V -r) \
         "$STEAM/steamapps/common/Proton - Experimental/files/lib/i386-linux-gnu" \
         $(ls -d "$STEAM"/steamapps/common/Proton\ */files/lib/i386-linux-gnu 2>/dev/null | sort -V -r | tr ' ' '\001'); do
  d="${d//$'\001'/ }"
  [[ -f "$d/gstreamer-1.0/libgstlibav.so" && -f "$d/libgstreamer-1.0.so.0" ]] && { pick="$d"; break; }
done
[[ -n "$pick" ]] || { echo "geen Proton met een 32-bits GStreamer (libgstlibav) gevonden; installeer Proton in Steam" >&2; exit 1; }
sniper=$(ls -d "$STEAM"/steamapps/common/SteamLinuxRuntime_sniper/sniper_platform_*/files/lib/i386-linux-gnu 2>/dev/null | sort -V | tail -1 || true)
rm -rf "$DEST.nieuw"; mkdir -p "$DEST.nieuw/plugins" "$DEST.nieuw/lib"
ln -s "$pick" "$DEST.nieuw/proton"
for f in "$pick"/gstreamer-1.0/*.so; do
  case "$(basename "$f")" in *protonmediaconverter*) continue ;; esac
  ln -s "$f" "$DEST.nieuw/plugins/"
done
# Wat de plug-ins vragen en niet in Proton, /usr/lib32 of al in lib/ staat: uit sniper (op SONAME), behalve glibc.
python3 -I - "$pick" "$DEST.nieuw/lib" "$sniper" <<'PY'
import os, subprocess, sys
proton, libdir, sniper = sys.argv[1], sys.argv[2], sys.argv[3]
glibc = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "ld-linux.so.2", "libresolv.so.2"}
def needed(p):
    out = subprocess.run(["objdump", "-p", p], capture_output=True, text=True).stdout
    return [l.split()[1] for l in out.splitlines() if l.strip().startswith("NEEDED")]
def soname(p):
    out = subprocess.run(["objdump", "-p", p], capture_output=True, text=True).stdout
    for l in out.splitlines():
        if l.strip().startswith("SONAME"): return l.split()[1]
by_soname = {}
if sniper and os.path.isdir(sniper):
    for n in os.listdir(sniper):
        p = os.path.join(sniper, n)
        if ".so" in n and os.path.isfile(p) and not os.path.islink(p):
            s = soname(p)
            if s: by_soname.setdefault(s, p)
def find(lib):
    for d in (proton, "/usr/lib32", libdir):
        if os.path.exists(os.path.join(d, lib)): return os.path.join(d, lib)
todo = [os.path.join(proton, "gstreamer-1.0", n) for n in os.listdir(os.path.join(proton, "gstreamer-1.0")) if n.endswith(".so")]
seen, linked, missing = set(), [], []
while todo:
    for lib in needed(todo.pop()):
        if lib in seen or lib in glibc: continue
        seen.add(lib)
        f = find(lib)
        if f: todo.append(f); continue
        src = by_soname.get(lib)
        if not src: missing.append(lib); continue
        os.symlink(src, os.path.join(libdir, lib)); linked.append(lib); todo.append(src)
print("uit sniper:", " ".join(sorted(linked)) or "-")
if missing: print("ontbreekt (die plug-ins laden niet):", " ".join(sorted(missing)))
PY
{ echo "proton: $pick"; echo "sniper: ${sniper:--}"; echo "gemaakt: $(date -u +%Y-%m-%dT%H:%M:%SZ)"; } > "$DEST.nieuw/BRON.txt"
rm -rf "$DEST"; mv "$DEST.nieuw" "$DEST"
echo "klaar: $DEST (proton: $pick)"

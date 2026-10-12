#!/bin/bash
# Dry-run evidence for the PROPOSED Halo recipe, in a scratch tree.
# Nothing in ~/projects/ravengun/packaging/ is written; no game is started.
set -u
REPO=~/projects/ravengun
W=/tmp/rg-proposed
rm -rf "$W"; mkdir -p "$W/scripts" "$W/packaging"
cp "$REPO/scripts/play-native.sh" "$REPO/scripts/rg-paths.sh" "$W/scripts/"
ln -s "$REPO/build" "$W/build"

echo "############ 1. the proposed JSON = the committed file + exactly these fields"
python3 - "$REPO/packaging/native-games.json" "$W/packaging/native-games.json" <<'PY'
import json, sys, collections
src, dst = sys.argv[1], sys.argv[2]
d = json.load(open(src), object_pairs_hook=collections.OrderedDict)
h = d["games"]["Halo"]
h["libdirs"].append(collections.OrderedDict([
    ("src", "@data/hasp"),
    ("dst", "/rg/hasp"),
    ("note", "de HASP-emulator + de donglegegevens (geinstalleerd); de inline-hook laadt de emulator "
             "hiervandaan met dlopen(RTLD_LOCAL), NIET met LD_PRELOAD"),
]))
h["libs"].append("/home/deck/projects/ravengun-patches/patches/Halo/dropin/libravengun-hasphook-x64.so")
h["env"] = collections.OrderedDict([
    ("RAVENGUN_HASP_DIR", "/rg/hasp"),
    ("RAVENGUN_HASP_GAME", "Halo Fireteam"),
    ("RAVENGUN_HASP_ELF_LIB", "/rg/hasp/ravengun-hasp-x64.so"),
    ("RAVENGUN_HASP_ELF_GAME", "/pm/g7/halo/game"),
    ("RAVENGUN_HASP_ELF_ADRESLIJST",
     "/home/deck/projects/ravengun-patches/patches/Halo/hasp-haken.ini"),
    ("RAVENGUN_HASP_ELF_LOG", "/pm/g7/halo/hasphook.log"),
    ("RAVENGUN_HASP_VERBOSE", "1"),
])
h["extra_ro"].append("/home/deck/projects/ravengun-patches/patches/Halo/hasp-haken.ini")
# DE VERWISSELING: de hook in preload in plaats van halo.so
h["preload"] = "/rg/lib/librg_rt_input.so:/rg/lib/libravengun-hasphook-x64.so"
json.dump(d, open(dst, "w"), indent=2, ensure_ascii=False)
open(dst, "a").write("\n")
print("  geschreven:", dst)
PY
diff <(python3 -c "import json,sys;print(json.dumps(json.load(open(sys.argv[1]))['games']['Halo'],indent=1,ensure_ascii=False,sort_keys=True))" "$REPO/packaging/native-games.json") \
     <(python3 -c "import json,sys;print(json.dumps(json.load(open(sys.argv[1]))['games']['Halo'],indent=1,ensure_ascii=False,sort_keys=True))" "$W/packaging/native-games.json") \
  | sed 's/^/  /'

echo
echo "############ 2. play-native.sh --dry-run against the proposed recipe"
( cd "$W" && scripts/play-native.sh --game Halo --dry-run ) >/tmp/rg-proposed.cmd 2>/tmp/rg-proposed.err
echo "exitcode $?"
echo "stderr:"; sed 's/^/  /' /tmp/rg-proposed.err
echo "the parts that matter:"
tr ' ' '\n' < /tmp/rg-proposed.cmd \
  | grep -nE '^--(ro-bind|ro-bind-try)|^/rg/|^--setenv$|LD_PRELOAD|LD_LIBRARY_PATH|RAVENGUN_HASP|^/pm/g7/halo/game$|libravengun-hasphook|libusb' \
  | head -40 | sed 's/^/  /'
echo "full LD_PRELOAD and LD_LIBRARY_PATH as passed:"
tr ' ' '\n' < /tmp/rg-proposed.cmd | grep -A1 -E '^LD_PRELOAD$|^LD_LIBRARY_PATH$' | sed 's/^/  /'
echo "the RAVENGUN_HASP_* values as passed:"
tr ' ' '\n' < /tmp/rg-proposed.cmd | grep -A1 -E '^RAVENGUN_HASP' | sed 's/^/  /'

echo
echo "############ 3. the three-library chain under /bin/ls (all load, nothing collides)"
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
D=~/projects/ravengun-patches/patches/Halo/dropin
DATA=~/.local/share/ravengun
E=( "RAVENGUN_HASP_ELF_GAME=$G/game" "RAVENGUN_HASP_GAME=Halo" "RAVENGUN_HASP_DIR=$DATA/hasp"
    "RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp-x64.so" "RAVENGUN_HASP_VERBOSE=1"
    "RAVENGUN_HASP_ELF_UIT=1" )
for pre in "$REPO/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so" \
           "$G/halo.so:$REPO/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so" \
           "$D/ravengun-hasp-x64.so:$REPO/build/librg_rt_input.so:$D/libravengun-hasphook-x64.so"; do
  rm -f /tmp/hh-x.log
  env "${E[@]}" "RAVENGUN_HASP_ELF_LOG=/tmp/hh-x.log" "LD_PRELOAD=$pre" /bin/ls /dev/null >/dev/null 2>&1
  printf '  exitcode %s  | %s\n' "$?" "$(echo "$pre" | tr ':' '\n' | xargs -n1 basename | tr '\n' '+')"
  grep -c 'symbol lookup error\|cannot open shared' /tmp/hh-x.log 2>/dev/null | sed 's/^/    botsingen: /'
done

echo
echo "############ 4. ldd: the game's DT_NEEDED closure (the loader proof)"
LD_LIBRARY_PATH="$DATA/runtime/lib-fmodstub" ldd "$G/game" 2>&1 | grep -c 'not found' | sed 's/^/  not found: /'
echo "== done"

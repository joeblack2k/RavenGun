#!/bin/sh
# Draait IN de build-container (packaging/containers/openparrot/Containerfile), gestart door scripts/build-openparrot.sh.
# Geen netwerk; /src is de (gepatchte) OpenParrot-bron, alleen-lezen; /out is de uitvoermap.
# Alleen de loaders en de kern; premake en Visual Studio worden niet gebruikt (zie third_party/openparrot/README.md).
# Doelen via $TARGETS (spatiegescheiden): loader32 loader64 core32 core64 (standaard alle vier) en op verzoek exittrace64.
set -eu
TARGETS="${TARGETS:-loader32 loader64 core32 core64}"
OBJ=/tmp/obj
SHIM=/tmp/shim
mkdir -p "$OBJ" "$SHIM" /out/OpenParrotWin32 /out/OpenParrotx64

. /third_party/shim.sh   # maakt $SHIM (aliassen voor hoofdletter- en backslash-includes)

CXXFLAGS_COMMON="-std=gnu++17 -O2 -DNDEBUG -DUNICODE -D_UNICODE -I$SHIM -w"

build_loader() {  # arch(i686|x86_64) uitvoernaam uitvoermap
  cd /src/OpenParrotLoader/src
  "$1-w64-mingw32-g++" $CXXFLAGS_COMMON -O0 -s -municode -static -Wl,--subsystem,console -Wl,--no-insert-timestamp \
    -o "/out/$3/$2" ./*.cpp -lntdll -lshlwapi -ladvapi32 -lversion
}

# De kern (OpenParrot/premake5.lua): src/**.cpp, MinHook en udis86 als statische bibliotheken, voor x64 ook win64init.asm
# (MASM; hier de GNU-vertaling in third_party/openparrot/win64init.S). DirectX: de MinGW-w64-headers en importbibliotheken
# in plaats van de meegeleverde Microsoft DirectX SDK (deps/inc/DirectXSDK); alias hieronder.
build_core() {  # arch uitvoernaam uitvoermap
  arch="$1"; name="$2"; dest="$3"
  cc="$arch-w64-mingw32-gcc"; ld="$arch-w64-mingw32-g++"
  w="$OBJ/$arch"; rm -rf "$w"; mkdir -p "$w/c" "$w/cpp"
  inc="-I/src/OpenParrot/src -I/src/OpenParrot/deps/inc -I/src/deps/inc -I/src/deps/udis86 -I$SHIM"
  # MinHook en udis86 (C)
  hde=hde64; [ "$arch" = i686 ] && hde=hde32
  for f in /src/deps/src/buffer.c /src/deps/src/hook.c /src/deps/src/trampoline.c /src/deps/src/hde/$hde.c /src/deps/udis86/libudis86/*.c; do
    "$cc" -O2 -w -DNDEBUG -include string.h $inc -c "$f" -o "$w/c/$(basename "$f").o"
  done
  # de kern zelf: een object per bronbestand, parallel
  # Niet gebouwd (de Type X2-games hebben ze niet nodig; zie third_party/openparrot/README.md):
  #  - Games/Other/GHA.cpp: gebruikt IGameExplorer uit <gameux.h> (Windows SDK), die MinGW-w64 niet heeft;
  #  - Games/ES3X/WMMT6.cpp, alleen 64-bit: gebruikt DnsQueryEx (DNS_QUERY_REQUEST), dat de MinGW-w64-headers niet hebben.
  excl='*/Games/Other/GHA.cpp'; [ "$arch" = x86_64 ] && excl2='*/Games/ES3X/WMMT6.cpp' || excl2='*/none'
  find /src/OpenParrot/src -name '*.cpp' ! -path "$excl" ! -path "$excl2" | sort > "$w/list"
  export ARCH="$arch" CXX_FLAGS="$CXXFLAGS_COMMON -include /third_party/mingw-compat.h -DWIN32 -D_WINDOWS -D_USRDLL -D_CRT_SECURE_NO_WARNINGS $inc" W="$w/cpp"
  xargs -P "$(nproc)" -I{} sh -c 'sh /third_party/compile-one.sh "{}" "$W/$(echo "{}" | sed "s|/src/OpenParrot/src/||; s|/|__|g").o"' < "$w/list"
  cat "$w"/cpp/*.how | sort | awk '{print $1}' | uniq -c
  extra=""
  if [ "$arch" = x86_64 ]; then "$cc" -c /third_party/win64init.S -o "$w/win64init.o"; extra="$w/win64init.o"; fi
  # Versiebron (OpenParrot.rc is UTF-16): zelfde VERSIONINFO als de MSVC-build van de bron (1.0.0.0; de CI van upstream telt het
  # buildnummer erbij, bv. 1.0.0.787 in de meegeleverde build).
  iconv -f UTF-16 -t UTF-8 /src/OpenParrot/src/OpenParrot.rc | tr -d '\r' > "$w/OpenParrot.rc"
  "$arch-w64-mingw32-windres" -I/src/OpenParrot/src -I"$SHIM" -O coff "$w/OpenParrot.rc" -o "$w/rc.o"
  extra="$extra $w/rc.o"
  # Zelfde laadadres als de MSVC-build (0x10000000 voor x86, 0x180000000 voor x64); x86 met dezelfde exportnamen.
  defs=""; base=0x180000000
  if [ "$arch" = i686 ]; then defs=/third_party/openparrot32.def; base=0x10000000; fi
  "$ld" -shared -static -s -o "/out/$dest/$name" $defs -Wl,--no-insert-timestamp -Wl,--disable-auto-image-base -Wl,--image-base,$base "$w"/cpp/*.o $extra "$w"/c/*.o \
    -Wl,--subsystem,windows -Wl,--allow-multiple-definition -lws2_32 -lwinmm -lshlwapi -ladvapi32 -lshell32 -luser32 -lntdll -ld3d9 -ld3dx9 -ld3d11 -ld3d12 -ldxguid -ldinput8 -lversion
}

# De exit-trace van patch 0007 als losse DLL (alleen op verzoek, niet in de standaarddoelen): voor een proces waar onze
# kern niet in zit; de lader laadt hem via TP_PRELOAD_DLL. Zelfde bronbestand als in de kern, alleen <windows.h> en MinHook.
build_exittrace() {  # arch uitvoernaam uitvoermap
  arch="$1"; w="$OBJ/exittrace-$arch"; rm -rf "$w"; mkdir -p "$w"
  hde=hde64; [ "$arch" = i686 ] && hde=hde32
  for f in /src/deps/src/buffer.c /src/deps/src/hook.c /src/deps/src/trampoline.c /src/deps/src/hde/$hde.c; do
    "$arch-w64-mingw32-gcc" -O2 -w -DNDEBUG -include string.h -I/src/deps/inc -c "$f" -o "$w/$(basename "$f").o"
  done
  "$arch-w64-mingw32-g++" -std=gnu++17 -O2 -DNDEBUG -DRG_EXITTRACE_STANDALONE -I/src/deps/inc \
    -c /src/OpenParrot/src/Utility/RgExitTrace.cpp -o "$w/RgExitTrace.o"
  "$arch-w64-mingw32-g++" -shared -static -s -o "/out/$3/$2" -Wl,--no-insert-timestamp "$w"/*.o -lntdll
}

for t in $TARGETS; do
  echo "== $t"
  case "$t" in
    loader32) build_loader i686 OpenParrotLoader.exe OpenParrotWin32 ;;
    loader64) build_loader x86_64 OpenParrotLoader64.exe OpenParrotx64 ;;
    core32) build_core i686 OpenParrot.dll OpenParrotWin32 ;;
    core64) build_core x86_64 OpenParrot64.dll OpenParrotx64 ;;
    exittrace64) build_exittrace x86_64 RgExitTrace64.dll OpenParrotx64 ;;
    *) echo "onbekend doel $t" >&2; exit 2 ;;
  esac
done

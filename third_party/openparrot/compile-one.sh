#!/bin/sh
# Compileert één C++-bronbestand van de OpenParrot-kern (aangeroepen door build-in-container.sh via xargs).
# Gebruik: compile-one.sh <bronbestand> <objectbestand>     Omgeving: ARCH, CXX_FLAGS
# Eerst GCC met -fpermissive (de bron leunt op MSVC-toegeeflijkheid, zoals een functiewijzer die zonder cast een void* wordt);
# lukt dat niet (MSVC-SEH `__try/__except`, MSVC-inline-assembler `__asm { }`), dan clang, dat die twee kent.
set -u
src="$1"; obj="$2"
if "$ARCH-w64-mingw32-g++" $CXX_FLAGS -fpermissive -c "$src" -o "$obj" 2> "$obj.gcc.log"; then
  echo "g++     $src" > "$obj.how"; exit 0
fi
if clang++ --target="$ARCH-w64-mingw32" $CXX_FLAGS -fms-extensions -Wno-c++11-narrowing -c "$src" -o "$obj" 2> "$obj.clang.log"; then
  echo "clang++ $src" > "$obj.how"; exit 0
fi
echo "== $src: mislukt (g++ en clang++)"; echo "-- g++:"; grep -E 'error' "$obj.gcc.log" | head -8; echo "-- clang++:"; grep -E 'error' "$obj.clang.log" | head -8
echo "FAIL $src" > "$obj.how"
[ -n "${KEEP_GOING:-}" ] && exit 0
exit 255

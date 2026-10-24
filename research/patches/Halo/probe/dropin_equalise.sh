#!/bin/bash
# Equalise the drop-in copies, ONE GAME AT A TIME, with sha256 before/after per file.
# The hook binary is built from the current source; nothing else is touched.
set -u
R=~/projects/ravengun
PR=~/projects/ravengun-patches
INC=()
[ -e /usr/include/gnu/stubs-32.h ] || INC=(-I"$R/hasp/src/inc")
TMP=$(mktemp -d)

echo "############ 0. de nieuwe bibliotheken bouwen (beide breedtes, zoals build-hasphook.sh doet)"
gcc -m32 "${INC[@]}" -shared -fPIC -O2 -w -Wno-format-truncation \
    -o "$TMP/libravengun-hasphook.so" "$R/tools/rt-hasphook/hasp_inline_hook.c" -ldl -lpthread || exit 1
gcc -shared -fPIC -O2 -w -Wno-format-truncation \
    -o "$TMP/libravengun-hasphook-x64.so" "$R/tools/rt-hasphook/hasp_inline_hook.c" -ldl -lpthread || exit 1
for f in "$TMP"/libravengun-hasphook*.so; do
  printf '  %-30s %8s  %s  %s\n' "$(basename "$f")" "$(stat -c%s "$f")" \
    "$(sha256sum "$f" | cut -c1-16)" "$(nm -D --defined-only "$f" | grep -c ' la_version$') la_version"
done

print_hash() {  # print_hash <game> <bestand>
  local g="$1" b="$2" f="$PR/patches/$g/dropin/$b"
  if [ -f "$f" ]; then
    printf '  %-14s %-30s %8s bytes  sha256 %s\n' "$g" "$b" "$(stat -c%s "$f")" "$(sha256sum "$f" | cut -c1-16)"
  else
    printf '  %-14s %-30s ONTBREEKT\n' "$g" "$b"
  fi
}

echo
echo "############ 1. de stand VOOR de gelijkstelling"
for g in Halo JurassicPark GalagaAssault Terminator; do
  for b in libravengun-hasphook.so libravengun-hasphook-x64.so; do print_hash "$g" "$b"; done
done

echo
echo "############ 2. de gecommitte Halo-versie (uit git), zodat 'oud' vastligt"
( cd "$R" && for b in libravengun-hasphook.so libravengun-hasphook-x64.so; do
    printf '  HEAD~1 %-28s ' "$b"
    git show "HEAD~1:patches/Halo/dropin/$b" 2>/dev/null | sha256sum | cut -c1-16 || echo "(niet in HEAD~1)"
  done )
( cd "$R" && for b in libravengun-hasphook.so libravengun-hasphook-x64.so; do
    printf '  HEAD   %-28s ' "$b"
    git show "HEAD:patches/Halo/dropin/$b" 2>/dev/null | sha256sum | cut -c1-16 || echo "(niet in HEAD)"
  done )

echo
echo "############ 3. JURASSICPARK bijwerken (alleen deze game)"
for b in libravengun-hasphook.so libravengun-hasphook-x64.so; do
  d="$PR/patches/JurassicPark/dropin/$b"
  echo "  $b:"
  echo "    voor: $( [ -f "$d" ] && { printf '%s bytes  sha256 %s' "$(stat -c%s "$d")" "$(sha256sum "$d" | cut -c1-16)"; } || echo ONTBREEKT )"
  install -m 0644 "$TMP/$b" "$d"
  echo "    na:   $(printf '%s bytes  sha256 %s' "$(stat -c%s "$d")" "$(sha256sum "$d" | cut -c1-16)")"
done
echo "  --- is de dropin.ini er nog (die hoort niet te veranderen)?"
ls -l "$PR/patches/JurassicPark/dropin/dropin.ini" 2>/dev/null | sed 's/^/    /' || echo "    geen dropin.ini"

echo
echo "############ 4. de stand NA de gelijkstelling"
for g in Halo JurassicPark GalagaAssault Terminator; do
  for b in libravengun-hasphook.so libravengun-hasphook-x64.so; do print_hash "$g" "$b"; done
done

echo
echo "############ 5. GalagaAssault: kan die drop-in ooit geladen worden?"
echo "  NAS-map:      $(ls -d /home/deck/mnt/spidernas_emulation/Teknoparrot/*alaga* 2>/dev/null || echo 'geen')"
echo "  profiel in native-games.json: $(grep -c GalagaAssault "$R/packaging/native-games.json")"
echo "  nog aanwezig in de repo:"
find "$PR/patches/GalagaAssault" -maxdepth 2 -type f 2>/dev/null | sed 's/^/    /'

echo
echo "############ 6. de check van de bouwer zelf"
bash "$R/scripts/build-hasphook.sh" --check 2>&1 | grep -E 'actueel|AFWIJKEND|ONTBREEKT' | sed 's/^/  /'
echo "== done"

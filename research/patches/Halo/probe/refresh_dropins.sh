#!/bin/bash
# Refresh the two placed drop-in copies after the reporting change, with old/new sha256.
set -u
R=~/projects/ravengun
PR=~/projects/ravengun-patches
INC=()
[ -e /usr/include/gnu/stubs-32.h ] || INC=(-I"$R/hasp/src/inc")
TMP=$(mktemp -d)

gcc -m32 "${INC[@]}" -shared -fPIC -O2 -w -Wno-format-truncation \
    -o "$TMP/libravengun-hasphook.so" "$R/tools/rt-hasphook/hasp_inline_hook.c" -ldl -lpthread || exit 1
gcc -shared -fPIC -O2 -w -Wno-format-truncation \
    -o "$TMP/libravengun-hasphook-x64.so" "$R/tools/rt-hasphook/hasp_inline_hook.c" -ldl -lpthread || exit 1

echo "############ de nieuw gebouwde bibliotheken"
for f in "$TMP"/libravengun-hasphook*.so; do
  printf '  %-30s %8s  sha256 %s\n' "$(basename "$f")" "$(stat -c%s "$f")" "$(sha256sum "$f" | cut -c1-16)"
done

for g in Halo JurassicPark; do
  echo
  echo "############ $g"
  for b in libravengun-hasphook.so libravengun-hasphook-x64.so; do
    d="$PR/patches/$g/dropin/$b"
    echo "  $b"
    echo "    oud: $(printf '%s bytes  sha256 %s' "$(stat -c%s "$d")" "$(sha256sum "$d" | cut -c1-16)")"
    install -m 0644 "$TMP/$b" "$d"
    echo "    nieuw: $(printf '%s bytes  sha256 %s' "$(stat -c%s "$d")" "$(sha256sum "$d" | cut -c1-16)")"
  done
done

echo
echo "############ de check van de bouwer"
bash "$R/scripts/build-hasphook.sh" --check 2>&1 | grep -E 'actueel|AFWIJKEND|ONTBREEKT' | sed 's/^/  /'
echo "== done"

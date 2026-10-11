# Wordt met `.` ingelezen door build-in-container.sh. Vereist: $SHIM (uitvoermap), /src (de bron).
# De bron is op Windows geschreven (hoofdletterongevoelig): <Shlwapi.h>, "PE.H". Voor elke include die op deze
# bestandssysteem niet bestaat maken we een alias in $SHIM, zonder de bron te wijzigen: eerst naar een MinGW-header
# met dezelfde naam in kleine letters, anders naar een bronbestand met dezelfde naam zonder letterverschil.
MINGW_INC=/usr/i686-w64-mingw32/include
make_shim() {
  grep -rhoE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^>"]+[>"]' /src --include='*.cpp' --include='*.h' --include='*.hpp' --include='*.c' \
    | sed -E 's/^[^<"]*[<"]([^>"]+)[>"]/\1/' | tr -d '\r' | sort -u | while read -r inc; do
    orig="$inc"
    inc=$(printf '%s' "$inc" | tr '\\' '/')
    case "$orig" in   # #include <Utility\\Foo.h>: op Linux is de backslash een gewoon teken in de bestandsnaam
      *\\*) printf '#include "%s"\n' "$inc" > "$SHIM/$orig"; continue ;;
    esac
    lower=$(printf '%s' "$inc" | tr 'A-Z' 'a-z')
    [ -e "$MINGW_INC/$inc" ] && continue
    case "$inc" in   # de meegeleverde Microsoft DirectX SDK niet gebruiken: de MinGW-w64-headers
      DirectXSDK/Include/*) b=$(basename "$lower"); mkdir -p "$SHIM/$(dirname "$inc")"; printf '#include <%s>\n' "$b" > "$SHIM/$inc"; continue ;;
    esac
    out="$SHIM/$inc"
    if [ -e "$MINGW_INC/$lower" ]; then
      mkdir -p "$(dirname "$out")"; printf '#include <%s>\n' "$lower" > "$out"
    else
      base=$(basename "$inc")
      found=$(find /src -iname "$base" -type f | head -n 1)
      if [ -n "$found" ] && [ ! -e "$(dirname "$found")/$base" ]; then
        mkdir -p "$(dirname "$out")"; printf '#include "%s"\n' "$found" > "$out"
      fi
    fi
  done
}
make_shim

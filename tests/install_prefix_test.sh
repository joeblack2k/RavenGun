#!/usr/bin/env bash
# De prefixcontrole van de installatie (cmake/CheckInstallPrefix.cmake): een prefix waarvan <prefix>/share/ravengun de gegevensmap
# van de gebruiker is (de gebruikerslaag) moet geweigerd worden, ook met een afsluitende "/", "//", "." of ".." in het pad en met een
# HOME of XDG_DATA_HOME die een symlink is; een eigen prefix, of dezelfde prefix onder een DESTDIR, mag. Daarna dezelfde weigering
# via een echte `cmake --install` met een tijdelijke HOME (die als eerste regel faalt, voor er iets geschreven is).
#
# Gebruik: install_prefix_test.sh <repo> <builddir>     (de tijdelijke map komt in builddir, nooit in /tmp)
set -u
REPO="$(cd "$1" && pwd)"
BUILD="$(mkdir -p "${2:-$REPO/build}" && cd "${2:-$REPO/build}" && pwd)"
command -v cmake >/dev/null || { echo "overgeslagen: cmake ontbreekt"; exit 77; }
T="$(mktemp -d "$BUILD/install-prefix-test.XXXXXX")" || exit 1
trap 'rm -rf "$T"' EXIT
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
CHECK="$REPO/cmake/CheckInstallPrefix.cmake"

H="$T/home"; mkdir -p "$H/.local/share" "$T/xdg/share"
ln -s "$H" "$T/homelink"; ln -s "$T/xdg/share" "$T/datalink"
# check <verwacht: weiger|sta toe> <prefix> <destdir> <xdg_data_home> <home> <naam>
check() {
    local want="$1" prefix="$2" dest="$3" xdg="$4" home="$5" name="$6" out rc
    out="$(cmake -DRG_PREFIX="$prefix" -DRG_DESTDIR="$dest" -DRG_DATA_HOME="$xdg" -DRG_HOME="$home" -P "$CHECK" 2>&1)"; rc=$?
    if [[ "$want" == weiger ]]; then
        [[ $rc != 0 && "$out" == *"gegevensmap"* ]] || fail "$name: had geweigerd moeten worden (rc=$rc): $out"
        [[ "$out" != *XDG_DATA_HOME* ]] || fail "$name: de melding adviseert XDG_DATA_HOME, en dat vernietigt juist de gebruikerslaag"
    else
        [[ $rc == 0 ]] || fail "$name: had toegestaan moeten worden (rc=$rc): $out"
    fi
}
check weiger "$H/.local"       ""      ""  "$H" "gewone prefix = ~/.local"
check weiger "$H/.local/"      ""      ""  "$H" "afsluitende /"
check weiger "$H//.local//"    ""      ""  "$H" "dubbele //"
check weiger "$H/./.local"     ""      ""  "$H" "met ."
check weiger "$H/x/../.local"  ""      ""  "$H" "met .."
check weiger "$T/homelink/.local" ""   ""  "$H" "HOME via een symlink in de prefix"
check weiger "$H/.local"       ""      ""  "$T/homelink" "HOME is zelf de symlink"
check weiger "$T/xdg"          ""      "$T/datalink" "$H" "XDG_DATA_HOME via een symlink"
check weiger "$T/xdg/"         ""      "$T/xdg/share/" "$H" "XDG_DATA_HOME met afsluitende /"
check weiger "/.local"         "$H"    ""  "$H" "DESTDIR + prefix komt op de gegevensmap uit"
check staat "$H/.local/opt/ravengun" "" ""  "$H" "eigen prefix"
check staat "/usr"             ""      ""  "$H" "systeemprefix"
check staat "$H/.local"        "$T/dest" "" "$H" "DESTDIR verplaatst de installatie"
check staat "$H/.local"        ""      "$T/xdg/share" "$H" "XDG_DATA_HOME wijst elders: ~/.local is dan een gewone prefix"
check staat "$H/.local"        ""      ""  ""   "geen HOME en geen XDG_DATA_HOME: geen datamap, dus niets te botsen"

# Dezelfde weigering in een echte installatie (als de bouwmap een installatiescript heeft): als eerste regel, dus er is niets geschreven.
if [[ -f "$BUILD/cmake_install.cmake" ]] && grep -q CheckInstallPrefix "$BUILD/cmake_install.cmake"; then
    out="$(env -i PATH="$PATH" HOME="$H" cmake --install "$BUILD" --prefix "$H/.local/" 2>&1)"; rc=$?
    [[ $rc != 0 && "$out" == *"gegevensmap"* ]] || fail "cmake --install --prefix ~/.local/ is niet geweigerd (rc=$rc): $(echo "$out" | head -5)"
    [[ -z "$(ls -A "$H/.local/share")" ]] || fail "de geweigerde installatie heeft toch iets geschreven: $(ls "$H/.local/share")"
else
    echo "let op: de bouwmap heeft geen cmake_install.cmake met de prefixcontrole; de echte installatie is niet getoetst"
fi
if [[ $FAILS -eq 0 ]]; then echo "install_prefix_test: alles goed"; exit 0; fi
echo "install_prefix_test: $FAILS fout(en)"; exit 1

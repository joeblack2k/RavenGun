#!/usr/bin/env bash
# De prefixcontrole van de installatie (cmake/CheckInstallPrefix.cmake). Geweigerd:
#   - een prefix waarvan <prefix>, <prefix>/patches of <prefix>/share/ravengun de gegevensmap van de gebruiker is of erin ligt (de
#     gebruikerslaag), ook met een afsluitende "/", "//", "." of ".." in het pad, met een HOME of XDG_DATA_HOME die een symlink is,
#     en ALTIJD ~/.local/share/ravengun, ook als de installatieshell een andere XDG_DATA_HOME heeft (de daemon ziet die niet);
#   - de wortel van een git-werkmap;
#   - een <prefix>/patches die niet aantoonbaar van een eerdere installatie is, of een koppeling.
# Toegestaan: een eigen prefix, dezelfde prefix onder een DESTDIR, een relatieve XDG_DATA_HOME (die negeert de daemon ook).
# Daarna dezelfde weigeringen via een echte `cmake --install` met een tijdelijke HOME (die als eerste regel faalt, voor er iets
# geschreven is): de gebruikerslaag en de patches/ van een repository blijven heel.
#
# Gebruik: install_prefix_test.sh <repo> <builddir>     (de tijdelijke map komt in builddir, nooit in /tmp)
set -u
REPO="$(cd "$1" && pwd)"
BUILD="$(mkdir -p "${2:-$REPO/build}" && cd "${2:-$REPO/build}" && pwd)"
command -v cmake >/dev/null || { echo "overgeslagen: cmake ontbreekt"; exit 77; }
T="$(mktemp -d "$BUILD/install-prefix-test.XXXXXX")" || exit 1
trap 'chmod -R u+rwX "$T" 2>/dev/null; rm -rf "$T"' EXIT
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
CHECK="$REPO/cmake/CheckInstallPrefix.cmake"

H="$T/home"; mkdir -p "$H/.local/share" "$T/xdg/share"
ln -s "$H" "$T/homelink"; ln -s "$T/xdg/share" "$T/datalink"
# check <verwacht: weiger|staat> <prefix> <destdir> <xdg_data_home> <home> <naam> [tekst in de melding, standaard "gegevensmap"]
check() {
    local want="$1" prefix="$2" dest="$3" xdg="$4" home="$5" name="$6" msg="${7:-gegevensmap}" out rc
    out="$(cmake -DRG_PREFIX="$prefix" -DRG_DESTDIR="$dest" -DRG_DATA_HOME="$xdg" -DRG_HOME="$home" -P "$CHECK" 2>&1)"; rc=$?
    if [[ "$want" == weiger ]]; then
        [[ $rc != 0 && "$out" == *"$msg"* ]] || fail "$name: had geweigerd moeten worden (rc=$rc): $out"
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
# De daemon (systemd-gebruikersdienst) ziet de XDG_DATA_HOME van de installatieshell niet: ~/.local blijft verboden.
check weiger "$H/.local"       ""      "$T/xdg/share" "$H" "XDG_DATA_HOME wijst elders, maar de daemon gebruikt ~/.local/share"
# De prefix IS de gegevensmap of ligt erin: install zou <prefix>/patches (= de gebruikerslaag/patches) weggooien.
check weiger "$H/.local/share/ravengun"     "" "" "$H" "prefix = de gegevensmap zelf"
check weiger "$H/.local/share/ravengun/"    "" "" "$H" "prefix = de gegevensmap zelf, afsluitende /"
check weiger "$H/.local/share/ravengun/sub" "" "" "$H" "prefix in de gegevensmap"
check weiger "$T/xdg/share/ravengun"        "" "$T/xdg/share" "$H" "prefix = de absolute XDG-gegevensmap"
# <prefix>/patches is een koppeling naar de gebruikerslaag/patches
mkdir -p "$H/.local/share/ravengun/patches" "$T/pfxlink"; ln -s "$H/.local/share/ravengun/patches" "$T/pfxlink/patches"
check weiger "$T/pfxlink"      ""      ""  "$H" "<prefix>/patches wijst in de gegevensmap"
# Een relatieve XDG_DATA_HOME negeert de daemon (xdg_data_base); de controle dus ook.
mkdir -p "$T/rel"
out="$(cd "$T" && cmake -DRG_PREFIX="$T/rel" -DRG_DESTDIR= -DRG_DATA_HOME="rel/share" -DRG_HOME="$H" -P "$CHECK" 2>&1)" ||
    fail "relatieve XDG_DATA_HOME: had genegeerd moeten worden: $out"
# De wortel van een git-werkmap (de repo zelf, of een worktree met een .git-bestand).
mkdir -p "$T/repo1/.git" "$T/repo1/patches/Game" "$T/repo2/patches"; echo x > "$T/repo1/patches/Game/game.ini"
echo "gitdir: /elders" > "$T/repo2/.git"
check weiger "$T/repo1"        ""      ""  "$H" "wortel van een git-werkmap" "git-werkmap"
check weiger "$T/repo2/"       ""      ""  "$H" "wortel van een git-worktree (.git-bestand)" "git-werkmap"
# Een bestaande <prefix>/patches die niet van een eerdere installatie is.
mkdir -p "$T/vreemd/patches/Game"; echo eigen > "$T/vreemd/patches/Game/notes.txt"
check weiger "$T/vreemd"       ""      ""  "$H" "vreemde <prefix>/patches" "niet aantoonbaar"
mkdir -p "$T/leeg/patches"
check staat  "$T/leeg"         ""      ""  "$H" "lege <prefix>/patches"
mkdir -p "$T/eerder/patches/Game" "$T/eerder/share/ravengun"; echo x > "$T/eerder/patches/Game/game.ini"
ln -s ../../patches "$T/eerder/share/ravengun/patches"
check staat  "$T/eerder"       ""      ""  "$H" "eerdere installatie (koppeling share/ravengun/patches -> ../../patches)"
mkdir -p "$T/merk/patches/Game" "$T/merk/share/ravengun"; echo x > "$T/merk/patches/Game/game.ini"; echo x > "$T/merk/share/ravengun/.patches-installatie"
check staat  "$T/merk"         ""      ""  "$H" "eerdere installatie (merkbestand)"
mkdir -p "$T/anderlink/share/ravengun"; ln -s "$T/vreemd/patches" "$T/anderlink/share/ravengun/patches"
check weiger "$T/anderlink"    ""      ""  "$H" "share/ravengun/patches wijst elders heen" "../../patches"
mkdir -p "$T/toplink"; ln -s "$T/vreemd/patches" "$T/toplink/patches"
check weiger "$T/toplink"      ""      ""  "$H" "<prefix>/patches is een koppeling" "koppeling"
# G5: de omzetting naar <prefix>/patches (cmake/PatchesLayout.cmake). Het merkbestand komt VOOR de rename: een onderbroken eerste
# omzetting (rename gedaan, koppeling nog niet) wordt daarna door de controle en door de volgende installatie geaccepteerd.
LAYOUT="$REPO/cmake/PatchesLayout.cmake"
layout() {  # layout <root> [stop]
    cmake -DRG_PREFIX="$1" -DRG_ROOT="$1" -DRG_DATADIR=share/ravengun -DRG_TEST_STOP="${2:-}" -P "$LAYOUT" > "$T/layout.txt" 2>&1
}
vers() { mkdir -p "$1/share/ravengun/patches/Game"; echo "$2" > "$1/share/ravengun/patches/Game/game.ini"; }  # net geïnstalleerd
vers "$T/om" v1
layout "$T/om" na-rename || fail "G5: omzetting (tot na de rename) faalde: $(cat "$T/layout.txt")"
[[ -f "$T/om/patches/Game/game.ini" && ! -e "$T/om/share/ravengun/patches" && -f "$T/om/share/ravengun/.patches-installatie" ]] ||
    fail "G5: na een onderbroken omzetting ontbreekt het merkbestand (of de rename): $(ls -A "$T/om" "$T/om/share/ravengun" 2>&1)"
check staat  "$T/om"           ""      ""  "$H" "G5: onderbroken eerste omzetting (rename gedaan, koppeling niet)"
vers "$T/om" v2
layout "$T/om" || fail "G5: de installatie na een onderbroken omzetting faalde: $(cat "$T/layout.txt")"
[[ "$(cat "$T/om/patches/Game/game.ini")" == v2 && "$(readlink "$T/om/share/ravengun/patches")" == ../../patches ]] ||
    fail "G5: na de herhaalde installatie klopt de indeling niet"
# Een lege <prefix>/patches zonder merk mag weg (GLOB telt "." en ".." niet), een met alleen een verborgen bestand niet.
vers "$T/leeg2" v1; mkdir -p "$T/leeg2/patches"
layout "$T/leeg2" || fail "G5: een lege <prefix>/patches werd geweigerd (telt GLOB . en ..?): $(cat "$T/layout.txt")"
[[ -f "$T/leeg2/patches/Game/game.ini" ]] || fail "G5: lege <prefix>/patches niet vervangen"
vers "$T/verborgen" v1; mkdir -p "$T/verborgen/patches"; echo x > "$T/verborgen/patches/.eigen"
layout "$T/verborgen" && fail "G5: een <prefix>/patches met alleen een verborgen bestand werd zonder merk vervangen"
[[ -f "$T/verborgen/patches/.eigen" ]] || fail "G5: het verborgen bestand is weg"
# Systeemprefix: geen <prefix>/patches.
vers "$T/sys/usr" v1
cmake -DRG_PREFIX=/usr -DRG_ROOT="$T/sys/usr" -DRG_DATADIR=share/ravengun -P "$LAYOUT" >/dev/null 2>&1 || fail "G5: systeemprefix faalde"
[[ ! -e "$T/sys/usr/patches" && -d "$T/sys/usr/share/ravengun/patches" ]] || fail "G5: systeemprefix kreeg een <prefix>/patches"
check staat "$H/.local/opt/ravengun" "" ""  "$H" "eigen prefix"
check staat "/usr"             ""      ""  "$H" "systeemprefix"
check staat "$H/.local"        "$T/dest" "" "$H" "DESTDIR verplaatst de installatie"
check staat "$H/.local"        ""      ""  ""   "geen HOME en geen XDG_DATA_HOME: geen datamap, dus niets te botsen"

# Dezelfde weigeringen in een echte installatie (als de bouwmap een installatiescript heeft): als eerste regel, dus er is niets
# geschreven en de gebruikerslaag en de patches/ van de repository blijven heel.
if [[ -f "$BUILD/cmake_install.cmake" ]] && grep -q CheckInstallPrefix "$BUILD/cmake_install.cmake"; then
    UL="$H/.local/share/ravengun"; mkdir -p "$UL/patches/Game"; echo eigen > "$UL/patches/Game/eigen.ini"
    inst() { env -i PATH="$PATH" HOME="$H" "$@" > "$T/inst.txt" 2>&1; }
    intact() { [[ "$(cat "$UL/patches/Game/eigen.ini" 2>/dev/null)" == eigen && "$(ls -A "$H/.local/share")" == ravengun ]]; }
    inst cmake --install "$BUILD" --prefix "$H/.local/" && fail "cmake --install --prefix ~/.local/ is niet geweigerd"
    grep -q gegevensmap "$T/inst.txt" || fail "--prefix ~/.local/: geen melding: $(head -5 "$T/inst.txt")"
    intact || fail "--prefix ~/.local/: de gebruikerslaag is aangeraakt"
    inst env XDG_DATA_HOME="$T/xdg/share" cmake --install "$BUILD" --prefix "$H/.local" && fail "XDG_DATA_HOME elders + --prefix ~/.local is niet geweigerd"
    intact || fail "XDG_DATA_HOME elders + --prefix ~/.local: de gebruikerslaag van de daemon is verplaatst"
    inst cmake --install "$BUILD" --prefix "$UL" && fail "cmake --install --prefix ~/.local/share/ravengun is niet geweigerd"
    intact || fail "--prefix ~/.local/share/ravengun: de gebruikerslaag (patches/) is weggegooid"
    [[ ! -e "$UL/bin" && ! -e "$UL/share" ]] || fail "--prefix ~/.local/share/ravengun: er is toch iets geschreven"
    inst cmake --install "$BUILD" --prefix "$T/repo1" && fail "cmake --install --prefix <git-werkmap> is niet geweigerd"
    [[ "$(cat "$T/repo1/patches/Game/game.ini" 2>/dev/null)" == x && ! -e "$T/repo1/bin" ]] || fail "--prefix <git-werkmap>: de repo is aangeraakt"
    inst cmake --install "$BUILD" --prefix "$T/vreemd" && fail "cmake --install over een vreemde <prefix>/patches is niet geweigerd"
    [[ "$(cat "$T/vreemd/patches/Game/notes.txt" 2>/dev/null)" == eigen && ! -e "$T/vreemd/bin" ]] || fail "vreemde <prefix>/patches: aangeraakt"
else
    echo "let op: de bouwmap heeft geen cmake_install.cmake met de prefixcontrole; de echte installatie is niet getoetst"
fi
if [[ $FAILS -eq 0 ]]; then echo "install_prefix_test: alles goed"; exit 0; fi
echo "install_prefix_test: $FAILS fout(en)"; exit 1

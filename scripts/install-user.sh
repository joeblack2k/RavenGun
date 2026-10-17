#!/usr/bin/env bash
# Installeert RavenGun voor de gebruiker, ZONDER root: programma's, bibliotheken en gegevens onder één map (standaard
# ~/.local/opt/ravengun), de gebruikersservices en de snelkoppelingen. De root-onderdelen (udev-regels, Bluetooth-hulp, sudoers)
# volgen in de eerste-gebruik-wizard van de app (één beheerdersprompt) of met `pkexec <prefix>/share/ravengun/scripts/
# install-permissions.sh --apply`; dit script raakt ze niet aan.
#
# Twee manieren om dit script te gebruiken:
#   1. Vanuit een uitgepakt releasearchief (ravengun-<versie>-x86_64-steamos/install.sh): de boom (bin/, lib/, share/) wordt naar
#      de installatiemap gekopieerd.
#   2. Vanuit de repo: `cmake --install` van de builds (build/ en build-app/) naar de installatiemap.
# In beide gevallen wijzen de units en snelkoppelingen daarna naar de geïnstalleerde kopie, niet naar de repo of het archief.
# Een bestaande installatie wordt vervangen (eerst naast de oude klaargezet en dan verwisseld); de gebruikersgegevens
# (~/.config/ravengun, ~/.local/share/ravengun, ~/.local/state/ravengun) blijven ongemoeid.
# $RAVENGUN_SYSTEMCTL vervangt systemctl (tests). De uitvoer van dit script is voor de gebruiker en dus Engels.
set -euo pipefail

SELF="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SELF/.." && pwd)"
SYSTEMCTL="${RAVENGUN_SYSTEMCTL:-systemctl}"
PREFIX="${RAVENGUN_PREFIX:-$HOME/.local/opt/ravengun}"
BUILD="" ; APPBUILD="" ; NOSTART=0 ; NODESKTOP=0

die() { echo "Error: $*" >&2; exit 1; }
say() { echo "== $*"; }
usage() {
  cat <<'EOF'
Usage: install-user.sh [options]

Installs RavenGun for the current user (no root needed).

  --prefix DIR          install into DIR (default: $RAVENGUN_PREFIX or ~/.local/opt/ravengun)
  --no-start            do not (re)start the engine service
  --no-desktop          no menu entry, desktop icon or links in ~/.local/bin
  --build-dir DIR       (from a source checkout) CMake build of the engine and CLI (default: build/)
  --app-build-dir DIR   (from a source checkout) CMake build of the Qt app (default: build-app/)
  --uninstall [opts]    run uninstall-user.sh
  -h, --help            this text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix) [[ $# -ge 2 ]] || die "--prefix needs a folder"; PREFIX="$2"; shift ;;
    --build-dir) [[ $# -ge 2 ]] || die "--build-dir needs a folder"; BUILD="$2"; shift ;;
    --app-build-dir) [[ $# -ge 2 ]] || die "--app-build-dir needs a folder"; APPBUILD="$2"; shift ;;
    --no-start) NOSTART=1 ;;
    --no-desktop) NODESKTOP=1 ;;
    --uninstall) shift; exec bash "$SELF/uninstall-user.sh" "$@" ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; die "unknown option: $1" ;;
  esac
  shift
done

[[ $EUID -ne 0 ]] || die "run this as your normal user, not as root (everything is installed under your home folder)."
[[ -n ${HOME:-} && -d $HOME ]] || die "HOME is not set."
case "$PREFIX" in /*) ;; *) die "--prefix must be an absolute path: $PREFIX" ;; esac
case "$PREFIX" in *[[:space:]%\"\'\\]*) die "the install folder must not contain spaces or special characters: $PREFIX" ;; esac
mkdir -p "$(dirname "$PREFIX")"
PREFIX="$(cd "$(dirname "$PREFIX")" && pwd)/$(basename "$PREFIX")"
case "$PREFIX" in /|"$HOME"|/usr|/usr/*|/etc|/etc/*|/opt|/home|/home/*/..) die "unsafe install folder: $PREFIX" ;; esac

# A folder that exists and is not ours is left alone.
if [[ -e $PREFIX ]]; then
  [[ -d $PREFIX ]] || die "$PREFIX exists and is not a folder"
  if [[ -n "$(ls -A "$PREFIX")" && ! -f $PREFIX/share/ravengun/scripts/install-user.sh ]]; then
    die "$PREFIX already exists and is not a RavenGun installation; pick another folder with --prefix"
  fi
fi

STAGE="$PREFIX.new.$$"
cleanup() { [[ -z ${STAGE:-} ]] || rm -rf "$STAGE"; }
trap cleanup EXIT
rm -rf "$STAGE"; mkdir -p "$STAGE"

# --- 1. De boom klaarzetten -------------------------------------------------------------------------------------------------
if [[ "$(basename "$(dirname "$ROOT")")" == share && -x "$ROOT/../../bin/ravengund" ]]; then
  SRC_PREFIX="$(cd "$ROOT/../.." && pwd)"
  if [[ "$SRC_PREFIX" == "$PREFIX" ]]; then
    say "RavenGun is already in $PREFIX; only refreshing the services and shortcuts"
    rm -rf "$STAGE"; STAGE=""
  else
    say "Copying $SRC_PREFIX to $PREFIX"
    for d in bin lib share; do [[ -d $SRC_PREFIX/$d ]] && cp -a "$SRC_PREFIX/$d" "$STAGE/$d"; done
  fi
else
  # Vanuit de repo: cmake --install. De app-build eerst, daarna de gewone build (die gaat voor bij dubbele bestanden).
  [[ -f $ROOT/CMakeLists.txt && -f $ROOT/scripts/tp-add-game.py ]] || die "this is neither a release folder nor a source checkout: $ROOT"
  command -v cmake >/dev/null || die "cmake is missing (needed to install from a source checkout)"
  BUILD="${BUILD:-$ROOT/build}"; APPBUILD="${APPBUILD:-$ROOT/build-app}"
  [[ -x $BUILD/ravengund && -x $BUILD/ravengunctl ]] || die "ravengund/ravengunctl not found in $BUILD (build first: docs/build.md)"
  say "Installing from the source checkout ($ROOT) to $PREFIX"
  if [[ -x $APPBUILD/ravengun ]]; then cmake --install "$APPBUILD" --component app --prefix "$STAGE" >/dev/null
  else echo "Note: the app is not built ($APPBUILD/ravengun is missing; see scripts/build-app.sh); only the engine and CLI are installed." >&2; fi
  cmake --install "$BUILD" --prefix "$STAGE" >/dev/null
fi

if [[ -n $STAGE ]]; then
  [[ -x $STAGE/bin/ravengund && -x $STAGE/bin/ravengunctl ]] || die "ravengund or ravengunctl is missing in the tree to install"
  [[ -f $STAGE/share/ravengun/scripts/tp-add-game.py && -d $STAGE/share/ravengun/packaging ]] || die "the data folder (share/ravengun, with packaging/) is missing in the tree to install"
  # De patches per game staan in hun eigen repo (ravengun-patches), niet in die van de loader: het is
  # gebruikersinhoud en de loader hoort schoon te zijn. Ze zijn dus optioneel -- staat de map er (een
  # ontwikkelcheckout met een koppeling ernaartoe), dan gaan ze mee; zo niet, dan zegt de installer
  # waar ze horen.
  if [[ -d $STAGE/share/ravengun/patches ]]; then
    say "Patches per game: meegeleverd uit de checkout."
  else
    echo "Note: no patches/ in this tree -- the per-game patches live in their own repo (ravengun-patches)." >&2
    echo "      Put them in ~/.local/share/ravengun/patches, or set RAVENGUN_PATCHES_DIR to a checkout." >&2
  fi
  # Eisen van deze build (door make-release.sh vastgelegd): de glibc van de container waarin gebouwd is.
  if [[ -f $STAGE/share/ravengun/REQUIRES ]]; then
    need="$(sed -n 's/^glibc[ =>]*//p' "$STAGE/share/ravengun/REQUIRES" | head -1)"
    have="$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')"
    if [[ -n $need && -n $have && "$(printf '%s\n%s\n' "$need" "$have" | sort -V | head -1)" != "$need" ]]; then
      die "this build of RavenGun needs glibc $need or newer, but this system has $have. Update SteamOS, or build RavenGun yourself (docs/build.md)."
    fi
  fi
  # Ontbrekende systeembibliotheken (Qt 6.11 voor de app; hidapi, BlueZ enz. voor de engine): nu melden, niet bij het starten.
  missing=""
  for b in ravengun ravengund ravengunctl; do
    [[ -x $STAGE/bin/$b ]] || continue
    out="$(ldd "$STAGE/bin/$b" 2>&1 | grep -E 'not found' || true)"
    [[ -z $out ]] || missing="$missing"$'\n'"  $b:"$'\n'"$(echo "$out" | sed 's/^/    /')"
  done
  [[ -z $missing ]] || die "missing or too old system libraries:$missing"$'\n'"  (The app needs Qt 6.11 or newer: SteamOS 3.x has it; on Arch install qt6-base.)"

  # --- 2. Verwisselen met de bestaande installatie ----------------------------------------------------------------------------
  OLD=""
  if [[ -e $PREFIX ]]; then OLD="$PREFIX.old.$$"; mv "$PREFIX" "$OLD"; fi
  mv "$STAGE" "$PREFIX"; STAGE=""
  [[ -z $OLD ]] || rm -rf "$OLD"
  say "Installed in $PREFIX"
fi

DATA="$PREFIX/share/ravengun"
SYSDIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
APPDIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
STATE="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun"
mkdir -p "$STATE/logs"

# De Unity-plugins: de daemon weigert een plugin die ouder is dan een bronbestand (src/ravengun/launch/unity.cpp). Zeg het hier al,
# niet pas bij een dubbelklik (2026-10-16: een gewijzigde opmerking in Settings.cs hield Tomb Raider en Rabbids tegen).
if [[ -d $DATA/plugins/unity/src && -d $PREFIX/lib/unity-plugin ]]; then
  newest_src="$(find "$DATA/plugins/unity/src" -name '*.cs' -printf '%T@\n' 2>/dev/null | sort -n | tail -1)"
  for dll in "$PREFIX"/lib/unity-plugin/*/RavenGun.*.dll; do
    [[ -f $dll ]] || continue
    if [[ -n $newest_src ]] && awk -v a="$newest_src" -v b="$(stat -c %.9Y "$dll")" 'BEGIN { exit !(a > b) }'; then
      g="$(basename "$(dirname "$dll")")"
      echo "WARNING: the Unity plugin for $g is older than its sources and will not start; rebuild it: scripts/build-unity-plugin.sh $g" >&2
    fi
  done
fi

# --- 3. Gebruikersservices ---------------------------------------------------------------------------------------------------
# Engine: het sjabloon invullen en activeren (install-service.sh uit de geïnstalleerde kopie).
WAS_ACTIVE=0
if "$SYSTEMCTL" --user is-active --quiet ravengund.service 2>/dev/null; then WAS_ACTIVE=1; fi
say "Installing the engine service (ravengund)"
if [[ $NOSTART -eq 1 || $WAS_ACTIVE -eq 1 ]]; then bash "$DATA/scripts/install-service.sh" --apply --no-start
else bash "$DATA/scripts/install-service.sh" --apply; fi
if [[ $WAS_ACTIVE -eq 1 && $NOSTART -eq 0 ]]; then
  # Een draaiende engine alleen herstarten als er geen game draait (anders verliest de game zijn invoerapparaten).
  if ! bash "$DATA/scripts/safe-restart-daemon.sh"; then
    echo "The engine is still running the old version (a game is running). Restart it later: Engine > Restart in the app." >&2
  fi
fi
# DXVK voor games met [Launch] Dxvk = true (docs/dxvk.md): eenmalig ophalen; zonder netwerk zoekt rg-direct het in Proton van Steam.
if [[ ! -e "${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/dxvk/current" && -x "$DATA/scripts/fetch-dxvk.sh" ]]; then
  say "Fetching DXVK for games that use it"
  bash "$DATA/scripts/fetch-dxvk.sh" "${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/dxvk" ||
    echo "DXVK could not be fetched now; games with Dxvk = true will use the DXVK of an installed Proton, or run $DATA/scripts/fetch-dxvk.sh later." >&2
fi
# 32-bits GStreamer voor films in games met [Launch] Gst32 = true (docs/gst32.md): links naar Proton van Steam, niets gedownload.
if [[ ! -e "${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/gst32/proton" && -x "$DATA/scripts/setup-gst32.sh" ]]; then
  say "Linking a 32-bit GStreamer from Steam's Proton for game videos"
  bash "$DATA/scripts/setup-gst32.sh" "${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/gst32" ||
    echo "No Proton with a 32-bit GStreamer found; games with Gst32 = true need Proton in Steam, then run $DATA/scripts/setup-gst32.sh." >&2
fi
# Native DirectX-DLL's uit Steam's eigen herdistribueerbare, voor games met [Launch] NativeDlls (docs/redist.md); niets gedownload.
if [[ ! -d "${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/redist/x86" && -x "$DATA/scripts/setup-redist.sh" ]]; then
  say "Extracting DirectX DLLs from Steam's redistributables for games that need them"
  bash "$DATA/scripts/setup-redist.sh" "${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/redist" ||
    echo "Steam's DirectX redistributables were not found; games with NativeDlls need Steam, then run $DATA/scripts/setup-redist.sh." >&2
fi
# PhysX-systeemsoftware van NVIDIA (download) in de gedeelde prefix, voor games met [Launch] PhysX (docs/redist.md, "PhysX").
SHARED_PFX="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/prefixes/shared/wine"
if [[ -d $SHARED_PFX/drive_c && ! -f "$SHARED_PFX/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.1/PhysXCore.dll" &&
      -x "$DATA/scripts/setup-physx.sh" ]]; then
  say "Installing NVIDIA PhysX (legacy system software) for games that need it"
  bash "$DATA/scripts/setup-physx.sh" "$SHARED_PFX" ||
    echo "PhysX could not be installed (no network?); games with PhysX = true need it: run $DATA/scripts/setup-physx.sh." >&2
fi
# BepInEx (officiële release) voor games met [Launch] BepInEx (Men in Black); de unity-route gebruikt dezelfde map.
BEPINEX_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun/bepinex/5.4.21.0"
if [[ ! -f $BEPINEX_DIR/x64/BepInEx/core/BepInEx.Preloader.dll || ! -f $BEPINEX_DIR/x86/BepInEx/core/BepInEx.Preloader.dll ]] &&
   [[ -x "$DATA/scripts/fetch-bepinex.sh" ]]; then
  say "Fetching BepInEx 5.4.21.0 for Unity games that need a plugin"
  bash "$DATA/scripts/fetch-bepinex.sh" 5.4.21.0 ||
    echo "BepInEx could not be fetched (no network?); games with BepInEx = ... need it: run $DATA/scripts/fetch-bepinex.sh." >&2
fi
# Mods uit [Mod <naam>] van game.ini (de Engelse vertaling van Mario Kart DX); een mod die al klaarstaat wordt overgeslagen.
if [[ -f "$DATA/scripts/fetch-mods.py" && -d "$DATA/patches" ]] && grep -qs '^\[Mod ' "$DATA"/patches/*/game.ini; then
  say "Fetching game mods (translations) described in the patches"
  python3 -I "$DATA/scripts/fetch-mods.py" --patches "$DATA/patches" ||
    echo "A mod could not be fetched (no network?); games with Mod = ... need it: run $DATA/scripts/fetch-mods.py." >&2
fi
# OLED-bescherming: alleen het unitbestand (aan/uit staat in de app: Settings > Display).
say "Installing the optional OLED protection service"
mkdir -p "$SYSDIR"
sed -e "s|@DATADIR@|$DATA|g" -e "s|@BINDIR@|$PREFIX/bin|g" "$DATA/packaging/systemd/ravengun-oled-saver.service.in" > "$SYSDIR/ravengun-oled-saver.service"
chmod 644 "$SYSDIR/ravengun-oled-saver.service"
"$SYSTEMCTL" --user daemon-reload || true

# --- 4. Snelkoppelingen ------------------------------------------------------------------------------------------------------
if [[ $NODESKTOP -eq 0 ]]; then
  say "Creating shortcuts"
  mkdir -p "$APPDIR"
  sed -e "s|@BINDIR@|$PREFIX/bin|g" -e "s|@DATADIR@|$DATA|g" "$DATA/packaging/desktop/ravengun-qt.desktop.in" > "$APPDIR/ravengun-qt.desktop"
  chmod 755 "$APPDIR/ravengun-qt.desktop"
  if command -v update-desktop-database >/dev/null; then update-desktop-database "$APPDIR" 2>/dev/null || true; fi
  # Een pictogram op het bureaublad (Desktop Mode), alleen als die map er is.
  DESK="$HOME/Desktop"
  if command -v xdg-user-dir >/dev/null; then d="$(xdg-user-dir DESKTOP 2>/dev/null || true)"; if [[ -n $d && -d $d && $d != "$HOME" ]]; then DESK="$d"; fi; fi
  if [[ -d $DESK ]]; then cp "$APPDIR/ravengun-qt.desktop" "$DESK/ravengun-qt.desktop"; chmod 755 "$DESK/ravengun-qt.desktop"; fi
  # Handige koppelingen voor de terminal.
  mkdir -p "$HOME/.local/bin"
  for b in ravengun ravengunctl; do if [[ -x $PREFIX/bin/$b ]]; then ln -sfn "$PREFIX/bin/$b" "$HOME/.local/bin/$b"; fi; done
fi

cat <<EOF

RavenGun is installed in $PREFIX.
Next: start RavenGun from the application menu (or run $PREFIX/bin/ravengun). The first start opens the setup wizard,
which sets up permissions (one password prompt), pairs your PS Moves and adds your game folder.
To remove RavenGun later: $DATA/scripts/uninstall-user.sh
EOF

# Welk script is dit? (met `.` ingelezen, geen eigen programma)
#
# Drie metingen in een week gingen verloren aan een oude kopie waarvan niemand wist dat hij draaide (een oude
# ELF-lader, een oude drop-in-DLL, een oude geinstalleerde launcher). Daarom zegt elk startscript bovenaan zijn
# logboek zelf wie het is: zijn pad, zijn sha256, of het de versie uit de repo of de geinstalleerde is, en de
# sha256 van de bestanden die bij de run horen (het gegevensbestand, de lader, meegegeven bibliotheken).
# scripts/launch-direct.py doet hetzelfde voor de Wine-routes.
#
#   rg_identiteit SCRIPT [LABEL=BESTAND ...]
#
# Drukt regels af; de eerste is "script: <pad>". De sha256 is wat telt; de commit (repo) of het versienummer
# (installatie) is de omschrijving erbij. Een bestand dat er niet is wordt als ONTBREEKT genoemd, niet overgeslagen.
# Het kan de start nooit laten omvallen: zonder git, zonder VERSION en zonder sha256sum komt er minder te staan.

rg_sha256() {   # $1 = bestand; drukt de sha256 af, of "onbekend"
  local s
  s=$(sha256sum "$1" 2>/dev/null) && printf '%s' "${s%% *}" || printf 'onbekend'
}

rg_identiteit() {
  local script wortel soort detail="" extra ander paar label bestand
  script=$(readlink -f "$1" 2>/dev/null) || script="$1"
  shift || true
  wortel=$(dirname "$(dirname "$script")")
  if [[ "$(basename "$(dirname "$wortel")")" == share && -d "$wortel/../../bin" ]]; then
    soort="de GEINSTALLEERDE versie"
    [[ -r "$wortel/VERSION" ]] && detail=$(tr -s '[:space:]' ' ' < "$wortel/VERSION" | sed 's/ *$//')
  elif [[ -e "$wortel/.git" ]]; then
    soort="de versie uit de REPO"
    local commit wijz
    if commit=$(timeout 5 git -C "$wortel" rev-parse --short HEAD 2>/dev/null) && [[ -n "$commit" ]]; then
      detail="commit $commit"
      if wijz=$(timeout 5 git -C "$wortel" status --porcelain -- scripts 2>/dev/null); then
        [[ -n "$wijz" ]] && detail+="; met wijzigingen in scripts/ die niet in die commit zitten" || detail+="; scripts/ gelijk aan die commit"
      fi
    fi
  else
    soort="een losse kopie (geen repo, geen installatie)"
  fi
  echo "script: $script"
  echo "  $soort${detail:+ ($detail)}; sha256 $(rg_sha256 "$script")"
  for paar in "$@"; do
    label="${paar%%=*}"; bestand="${paar#*=}"
    if [[ -f "$bestand" ]]; then
      echo "  $label: $bestand sha256 $(rg_sha256 "$bestand")"
    else
      echo "  $label: $bestand ONTBREEKT"
    fi
  done
  # Vanuit de repo (of een losse kopie): is de geinstalleerde kopie dezelfde? Alleen de standaardplek.
  if [[ "$soort" != "de GEINSTALLEERDE versie" ]]; then
    ander="${HOME:-/nonexistent}/.local/opt/ravengun/share/ravengun/scripts/$(basename "$script")"
    if [[ -f "$ander" && "$(readlink -f "$ander")" != "$script" ]]; then
      if [[ "$(rg_sha256 "$ander")" == "$(rg_sha256 "$script")" ]]; then
        echo "  het geinstalleerde script ($ander) is hetzelfde"
      else
        echo "  het geinstalleerde script ($ander) is een ANDERE versie (sha256 $(rg_sha256 "$ander")): een run via de installatie gebruikt dus niet deze code"
      fi
    fi
  fi
  return 0
}

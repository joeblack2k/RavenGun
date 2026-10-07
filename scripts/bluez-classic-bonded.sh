#!/usr/bin/env bash
# Zet BlueZ ClassicBondedOnly=false zodat een PS Move ZCM1 (die geen authenticatie ondersteunt)
# als HID-apparaat kan verbinden. GLOBALE beveiligingsverlaging voor Bluetooth-HID: ook andere
# niet-geauthenticeerde HID-apparaten worden dan niet meer op dat punt geweigerd.
# Bewijs (btmon 2026-10-07): verbinding geaccepteerd, L2CAP PSM 17 -> "Authentication pending" -> "Authentication Failure".
#
# Gebruik:  sudo scripts/bluez-classic-bonded.sh --dry-run | --apply | --rollback
set -euo pipefail
MODE="${1:---dry-run}"
[[ $EUID -eq 0 ]] || { echo "Start met sudo." >&2; exit 1; }
CONF=/etc/bluetooth/input.conf; BAK="$CONF.ravengun-backup"
[[ -f "$CONF" ]] || { echo "$CONF ontbreekt" >&2; exit 1; }

current() { grep -E '^\s*ClassicBondedOnly\s*=' "$CONF" | tail -1 || true; }

case "$MODE" in
  --dry-run)
    echo "Huidig: $(current || true) (leeg = standaard true)"
    echo "--apply zou: back-up naar $BAK, onder [General] 'ClassicBondedOnly=false' zetten, bluetooth.service herstarten." ;;
  --apply)
    if [[ "$(current)" =~ false ]]; then echo "Staat al op false; niets gewijzigd."; exit 0; fi
    [[ -e "$BAK" ]] || cp -a "$CONF" "$BAK"
    if grep -qE '^\s*ClassicBondedOnly\s*=' "$CONF"; then
      sed -i -E 's/^\s*ClassicBondedOnly\s*=.*/ClassicBondedOnly=false/' "$CONF"
    else
      sed -i -E '0,/^\[General\]/s//[General]\nClassicBondedOnly=false/' "$CONF"
    fi
    systemctl restart bluetooth.service
    echo "Toegepast: $(current). Back-up: $BAK" ;;
  --rollback)
    [[ -e "$BAK" ]] || { echo "Geen back-up ($BAK)" >&2; exit 1; }
    cp -a "$BAK" "$CONF" && rm -f "$BAK"
    systemctl restart bluetooth.service
    echo "Hersteld: ClassicBondedOnly weer standaard." ;;
  *) echo "Gebruik: $0 --dry-run | --apply | --rollback" >&2; exit 2 ;;
esac

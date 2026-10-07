#!/usr/bin/env bash
# Registreert een Sony HID-controller zonder eigen authenticatie (PS Move ZCM1, Navigation Controller)
# bij BlueZ als vertrouwd + gebonden, zodat de kernel de inkomende verbinding accepteert.
# Het master-adres in de controller moet vooraf naar deze host gezet zijn (via USB).
# Vereist ClassicBondedOnly=false (zie bluez-classic-bonded.sh). Raakt alleen de map van dit apparaat.
#
# Gebruik: sudo scripts/bt-register-hid.sh --apply|--rollback|--dry-run ADRES NAAM CLASS PRODUCT_DEC
#   bv.    sudo scripts/bt-register-hid.sh --apply 00:11:22:33:44:55 "Navigation Controller" 0x000508 1071
set -euo pipefail
MODE="${1:-}"; ADDR="${2:-}"; NAME="${3:-}"; CLASS="${4:-}"; PROD="${5:-}"
[[ $EUID -eq 0 ]] || { echo "Start met sudo." >&2; exit 1; }
[[ "$ADDR" =~ ^([0-9A-F]{2}:){5}[0-9A-F]{2}$ ]] || { echo "Ongeldig adres (hoofdletters, AA:BB:..)" >&2; exit 2; }
HOSTDIR=$(ls -d /var/lib/bluetooth/*/ | head -1); DIR="${HOSTDIR}${ADDR}"; INFO="$DIR/info"; MARK="$DIR/.ravengun-created"

case "$MODE" in
  --dry-run) echo "Zou $INFO aanmaken (Name=$NAME, Class=$CLASS, Product=$PROD) en bluetooth herstarten." ;;
  --apply)
    [[ -n "$NAME" && -n "$CLASS" && -n "$PROD" ]] || { echo "NAAM, CLASS en PRODUCT verplicht" >&2; exit 2; }
    if [[ -e "$INFO" && ! -e "$MARK" ]]; then cp -a "$INFO" "$INFO.ravengun-backup"; fi
    KEY=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n' | tr a-f A-F)
    systemctl stop bluetooth.service
    mkdir -p "$DIR"; chmod 700 "$DIR"
    cat > "$INFO" <<EOF
[General]
Name=$NAME
Class=$CLASS
SupportedTechnologies=BR/EDR
Trusted=true
Blocked=false
Services=00001124-0000-1000-8000-00805f9b34fb;

[DeviceID]
Source=2
Vendor=1356
Product=$PROD
Version=0

[LinkKey]
Key=$KEY
Type=0
PINLength=0
EOF
    chmod 600 "$INFO"; [[ -e "$INFO.ravengun-backup" ]] || touch "$MARK"
    systemctl start bluetooth.service
    echo "Geregistreerd: $ADDR ($NAME)." ;;
  --rollback)
    systemctl stop bluetooth.service
    if [[ -e "$MARK" ]]; then rm -rf "$DIR"; echo "Verwijderd: $DIR"
    elif [[ -e "$INFO.ravengun-backup" ]]; then mv "$INFO.ravengun-backup" "$INFO"; echo "Hersteld: $INFO"
    else echo "Niets door RavenGun aangemaakt voor $ADDR."; fi
    systemctl start bluetooth.service ;;
  *) echo "Gebruik: $0 --apply|--rollback|--dry-run ADRES NAAM CLASS PRODUCT_DEC" >&2; exit 2 ;;
esac

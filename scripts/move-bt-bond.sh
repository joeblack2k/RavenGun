#!/usr/bin/env bash
# Laat BlueZ een via 'psmove pair' geregistreerde PS Move (ZCM1) als gebonden apparaat behandelen,
# zodat de kernel de inkomende verbinding accepteert en ClassicBondedOnly hem niet weigert.
# Dit raakt uitsluitend het info-bestand van de Move; de globale Bluetooth-instellingen blijven gelijk.
#
# Achtergrond: btmon toonde "Connection Rejected due to Unacceptable BD_ADDR (0x0f)" voor de Move.
# Een ZCM1 gebruikt geen authenticatie; de toegevoegde koppelsleutel wordt door de Move nooit gebruikt.
#
# Gebruik:  sudo scripts/move-bt-bond.sh --dry-run | --apply | --rollback
set -euo pipefail
MODE="${1:---dry-run}"
[[ $EUID -eq 0 ]] || { echo "Start met sudo." >&2; exit 1; }

mapfile -t INFOS < <(grep -l '^Name=Motion Controller$' /var/lib/bluetooth/*/*/info 2>/dev/null || true)
[[ ${#INFOS[@]} -eq 1 ]] || { echo "Verwacht precies één Motion Controller-registratie, gevonden: ${#INFOS[@]}. Eerst 'psmove pair' draaien." >&2; exit 1; }
INFO="${INFOS[0]}"; BAK="$INFO.ravengun-backup"
echo "Move-registratie: $INFO"

case "$MODE" in
  --dry-run)
    if grep -q '^\[LinkKey\]' "$INFO"; then echo "Bevat al een [LinkKey]-sectie; --apply zou niets doen."
    else echo "--apply zou: back-up naar $BAK, een [LinkKey]-sectie toevoegen, bluetooth.service herstarten."; fi ;;
  --apply)
    if grep -q '^\[LinkKey\]' "$INFO"; then echo "Al toegepast; niets gewijzigd."; exit 0; fi
    [[ -e "$BAK" ]] || cp -a "$INFO" "$BAK"
    KEY=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n' | tr a-f A-F)
    systemctl stop bluetooth.service
    printf '\n[LinkKey]\nKey=%s\nType=0\nPINLength=0\n' "$KEY" >> "$INFO"
    systemctl start bluetooth.service
    echo "Toegepast. Back-up: $BAK. Druk nu (zonder USB) op de PS-knop." ;;
  --rollback)
    [[ -e "$BAK" ]] || { echo "Geen back-up gevonden ($BAK); niets te herstellen." >&2; exit 1; }
    systemctl stop bluetooth.service
    cp -a "$BAK" "$INFO" && rm -f "$BAK"
    systemctl start bluetooth.service
    echo "Hersteld naar de toestand van na 'psmove pair'." ;;
  *) echo "Gebruik: $0 --dry-run | --apply | --rollback" >&2; exit 2 ;;
esac

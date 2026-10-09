#!/usr/bin/env bash
# Installeert de Bluetooth-hulp van RavenGun (scripts/ravengun-bt-helper) root-owned op een plek die op SteamOS beschrijfbaar
# en blijvend is, plus een sudoers-regel die de gebruiker ALLEEN dat ene programma zonder wachtwoord laat starten.
#
#   sudo scripts/install-bt-helper.sh --apply      installeren (of bijwerken)
#   sudo scripts/install-bt-helper.sh --rollback   alles weer weghalen
#   sudo scripts/install-bt-helper.sh --dry-run    tonen wat --apply zou doen
#   scripts/install-bt-helper.sh --check           zonder sudo: is het geïnstalleerd, actueel en toegestaan?
#
# Plek (docs/decisions.md, "Root-hulp voor BlueZ"): /opt/ravengun/bin/ravengun-bt-helper. Op SteamOS is /usr alleen-lezen en
# wordt het bij een update vervangen (ook /usr/local zit op de rootpartitie); /opt is een bind-mount van
# /home/.steamos/offload/opt op de thuispartitie en blijft dus bij updates staan; /etc (de sudoers-regel) is een overlay op
# /var en blijft ook. Alle mappen op het pad zijn van root en niet door de gebruiker beschrijfbaar.
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)/ravengun-bt-helper"
DST_DIR=/opt/ravengun/bin
DST="$DST_DIR/ravengun-bt-helper"
SUDOERS=/etc/sudoers.d/ravengun-bt-helper
MODE="${1:---dry-run}"

sudoers_for() {  # $1 = gebruiker
    cat <<EOF
# RavenGun: de gebruiker $1 mag ALLEEN de Bluetooth-hulp zonder wachtwoord als root starten (koppelen/verwijderen van een
# PS Move of Navigation Controller). Geïnstalleerd door scripts/install-bt-helper.sh; weghalen met --rollback.
Defaults!$DST env_reset
$1 ALL=(root) NOPASSWD: $DST register *, $DST unregister *
EOF
}

check() {
    local ok=0
    if [[ ! -e $DST ]]; then echo "Niet geïnstalleerd: $DST"; ok=1
    else
        local st; st=$(stat -c '%U:%G %a' "$DST")
        if [[ $st != "root:root 755" ]]; then echo "Verkeerde eigenaar of rechten: $DST ($st, verwacht root:root 755)"; ok=1; fi
        if cmp -s "$SRC" "$DST"; then echo "Hulp actueel: $DST"; else echo "Hulp verouderd: $DST wijkt af van $SRC (sudo $0 --apply)"; ok=1; fi
        for d in /opt /opt/ravengun "$DST_DIR"; do
            local ds; ds=$(stat -c '%U %a' "$d")
            [[ $ds == "root 755" ]] || { echo "Map niet veilig: $d ($ds)"; ok=1; }
        done
    fi
    if sudo -n -l "$DST" register >/dev/null 2>&1; then echo "sudo: toegestaan zonder wachtwoord"; else echo "sudo: (nog) niet toegestaan ($SUDOERS ontbreekt?)"; ok=1; fi
    return $ok
}

case "$MODE" in
  --check) check; exit $? ;;
  --print-sudoers)  # alleen de tekst (voor tests/bt_helper_test.sh); verandert niets
    [[ ${2-} =~ ^[a-z_][a-z0-9_-]{0,31}$ ]] || { echo "gebruik: $0 --print-sudoers GEBRUIKER" >&2; exit 2; }
    sudoers_for "$2"; exit 0 ;;
  --dry-run|--apply|--rollback) ;;
  *) echo "Gebruik: $0 --dry-run|--apply|--rollback|--check" >&2; exit 2 ;;
esac

[[ $EUID -eq 0 ]] || { echo "Start met sudo." >&2; exit 1; }
USER_NAME="${SUDO_USER:-}"
[[ $USER_NAME =~ ^[a-z_][a-z0-9_-]{0,31}$ && $USER_NAME != root ]] || { echo "Start met sudo vanuit het gewone gebruikersaccount (SUDO_USER ontbreekt of is root)." >&2; exit 1; }
[[ -f $SRC ]] || { echo "Bron ontbreekt: $SRC" >&2; exit 1; }
bash -n "$SRC" || { echo "Syntaxfout in $SRC" >&2; exit 1; }

case "$MODE" in
  --dry-run)
    echo "--apply zou:"
    echo "  $SRC installeren als $DST (root:root 0755; mappen /opt/ravengun en $DST_DIR root:root 0755)"
    echo "  $SUDOERS schrijven (root:root 0440), gecontroleerd met visudo -cf:"
    sudoers_for "$USER_NAME" | sed 's/^/    /'
    ;;
  --apply)
    install -d -o root -g root -m 755 /opt/ravengun "$DST_DIR"
    install -o root -g root -m 755 "$SRC" "$DST.new"
    mv -f "$DST.new" "$DST"
    TMP=$(mktemp /etc/sudoers.d/.ravengun-bt-helper.XXXXXX)
    trap 'rm -f "$TMP"' EXIT
    sudoers_for "$USER_NAME" >"$TMP"
    chown root:root "$TMP"
    chmod 440 "$TMP"
    visudo -cf "$TMP" >/dev/null || { echo "visudo keurt de sudoers-regel af; niets gewijzigd aan $SUDOERS" >&2; exit 1; }
    mv -f "$TMP" "$SUDOERS"
    trap - EXIT
    visudo -c >/dev/null || { echo "LET OP: visudo -c meldt een fout in de sudoers-configuratie" >&2; exit 1; }
    logger -t ravengun-bt-helper -- "geïnstalleerd voor gebruiker $USER_NAME" 2>/dev/null || true
    echo "Geïnstalleerd: $DST en $SUDOERS (gebruiker $USER_NAME)."
    ;;
  --rollback)
    rm -f "$SUDOERS"
    rm -f "$DST"
    rmdir "$DST_DIR" /opt/ravengun 2>/dev/null || true
    logger -t ravengun-bt-helper -- "verwijderd" 2>/dev/null || true
    echo "Verwijderd: $DST en $SUDOERS. (De log /var/log/ravengun-bt-helper.log blijft staan.)"
    ;;
esac

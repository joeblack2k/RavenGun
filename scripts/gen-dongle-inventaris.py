#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Genereert docs/inventaris-dongle-per-game.md: per game welke dongle hij vraagt.

De donglekolom komt uit `patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json` (veld `kind`). Dat bestand is de enige bron
van waarheid en het bestaan ervan betekent "deze game vraagt een dongle" (docs/dropins.md, "Drop-ins per game"). Ontbreekt
het, dan is de dongle "-". Een los `patches/<Game>/dongle.json` (de oude plaats) blijft ook werken.
De kolom Bron zegt waar de dongle vandaan komt: de drop-in in de map van de game.
De kolom Status komt uit `[Status] Status` in game.ini. De donglesoort staat dus NIET in game.ini: `[Dongle] Source` kiest
alleen tussen de fysieke dongle van de eigenaar (`usb-passthrough`, de standaard) en de drop-in (`drop-in`), en de
meegeleverde laag laat die keuze aan de eigenaar (docs/per-game-patches.md, "Dongle per game").

Gebruik:
  scripts/gen-dongle-inventaris.py               schrijft docs/inventaris-dongle-per-game.md
  scripts/gen-dongle-inventaris.py --out F       schrijft naar een ander bestand (bv. de NAS: findings/INVENTARIS-dongle-per-game.md)
  scripts/gen-dongle-inventaris.py --stdout      toont de tekst
  scripts/gen-dongle-inventaris.py --check       exit 1 als het bestand niet actueel is (niets schrijven; voor ctest)
Opties: --patches DIR (de meegeleverde map; ook $RAVENGUN_PATCHES_DIR).
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import gamesettings as gs  # noqa: E402

ROOT = gs.ROOT
OUT = os.path.join(ROOT, "docs", "inventaris-dongle-per-game.md")
NOTE_PREVIEW = 150          # de statusnotitie is in deze lijst een voorproefje; de volledige tekst staat in game.ini

# De soorten uit docs/dropins.md, met de leesbare naam voor de tabel.
KINDS = {"hasp": "hasp", "matrix": "matrix", "region": "region", "amdaemon": "amdaemon"}


def game_rows(patches):
    """Eén dict per game met wat de tabel nodig heeft (alleen uit de repo; niets van de NAS)."""
    rows = []
    for game in gs.list_games(patches, os.path.join(os.devnull, "geen-gebruikerslaag")):
        gdir = os.path.join(patches, game)
        sec, _ = gs.read_ini(os.path.join(gdir, "game.ini"))
        kind, detail = dongle_of(gdir, game)
        rows.append({
            "game": game,
            "type": sec.get("Game", {}).get("Type", ""),
            "profile": sec.get("Game", {}).get("EmulationProfile", ""),
            "kind": kind,
            "source": dongle_source(gdir, kind),
            "status": sec.get("Status", {}).get("Status", ""),
            "note": sec.get("Status", {}).get("Note", ""),
            "detail": detail,
        })
    return rows


def dongle_json_pad(gdir, game):
    """Het dongle.json van deze game: eerst de drop-in, anders de oude losse plaats.

    Een dongle is een patch en staat dus in de patchmap van de game:
        patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json
    De oude vorm patches/<Game>/dongle.json blijft werken voor wie hem al had.
    """
    for pad in (os.path.join(gdir, "dropin", "ravengun-hasp", game, "dongle.json"),
                os.path.join(gdir, "dongle.json")):
        if os.path.isfile(pad):
            return pad
    return ""


def dongle_of(gdir, game):
    """(kind, detail) uit het dongle.json van de game; ("", "") als de game geen dongle vraagt of het bestand onleesbaar is."""
    path = dongle_json_pad(gdir, game)
    if not path:
        return "", ""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        return "?", "%s is onleesbaar: %s" % (os.path.relpath(path, gdir), e)
    kind = str(data.get("kind", "")).strip().lower()
    if kind not in KINDS:
        return kind or "?", "dongle.json noemt een onbekende soort %r" % data.get("kind")
    bits = []
    mem = data.get("memory")
    if isinstance(mem, list) and mem:
        # De geheugenbestanden staan naast dongle.json. In de drop-in-vorm is dat memory/,
        # in de oude losse vorm dongle/memory/; noem wat er echt staat.
        memmap = "memory" if os.path.isdir(os.path.join(os.path.dirname(path), "memory")) else "dongle"
        bits.append("%s/ met %d geheugenbestand%s" % (memmap, len(mem), "" if len(mem) == 1 else "en"))
    if os.path.isdir(os.path.join(gdir, "dropin")):
        bits.append("dropin/")
    if data.get("enforced") is False:
        bits.append("niet afgedwongen, enforced = false")
    return kind, ", ".join(bits)


def dongle_source(gdir, kind):
    """Waar de dongle vandaan komt: de drop-in in de map van de game."""
    if not kind:
        return "-"
    return "drop-in (dongle.json%s)" % (" + dropin/" if os.path.isdir(os.path.join(gdir, "dropin")) else "")


def render(rows):
    lines = [
        "# Inventaris: dongle en patches per game",
        "",
        "Automatisch gemaakt uit `patches/<Game>/game.ini` en het `dongle.json` in de drop-in van elke game door",
        "`scripts/gen-dongle-inventaris.py`. Niet met de hand bijwerken: pas de map van de game aan en draai het script opnieuw.",
        "",
        "De kolom **Dongle** komt uit `patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json` (veld `kind`); het bestaan van",
        "dat bestand betekent \"deze game vraagt een dongle\" (`docs/dropins.md`). Ontbreekt het, dan is de dongle `-`. De kolom",
        "**Bron** zegt waar de dongle vandaan komt: de drop-in in de map van de game. De kolom **Status** komt uit",
        "`[Status] Status` in `game.ini`.",
        "",
        "`[Dongle] Source` in `game.ini` kiest tussen de fysieke dongle van de eigenaar (`usb-passthrough`, de standaard) en de",
        "drop-in (`drop-in`); de meegeleverde laag laat die keuze aan de eigenaar (zie `docs/per-game-patches.md`, \"Dongle per game\").",
        "Staat in `dongle.json` `\"enforced\": false`, dan gaat de game zonder die dongle verder (hij valt terug op zijn eigen",
        "standaardwaarde); dat staat er dan bij.",
        "",
        "| Game | Type | Emulatieprofiel | Dongle | Bron | Status |",
        "|---|---|---|---|---|---|",
    ]
    for r in rows:
        lines.append("| **%s** | %s | %s | %s | %s | %s |" % (r["game"], r["type"] or "-", r["profile"] or "-",
                                                              r["kind"] or "-", r["source"], r["status"]))
    lines += ["", "## Per game: de dongle in de map van de game", ""]
    for r in rows:
        if r["kind"]:
            extra = " (%s)" % r["detail"] if r["detail"] else ""
            lines.append("* **%s** — %s%s" % (r["game"], r["kind"], extra))
        else:
            lines.append("* **%s** — -" % r["game"])
    lines += ["", "## Per game: de statusnotitie", ""]
    for r in rows:
        note = r["note"][:NOTE_PREVIEW]
        lines.append("* **%s** — %s" % (r["game"], note or "-"))
    return "\n".join(lines) + "\n"


def main(argv):
    ap = argparse.ArgumentParser(description="Genereert docs/inventaris-dongle-per-game.md uit de map van elke game")
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--patches", default=gs.shipped_dir())
    ap.add_argument("--stdout", action="store_true")
    ap.add_argument("--check", action="store_true", help="exit 1 als het bestand niet actueel is (niets schrijven)")
    a = ap.parse_args(argv)
    text = render(game_rows(a.patches))
    if a.stdout:
        sys.stdout.write(text)
        return 0
    try:
        with open(a.out, encoding="utf-8") as f:
            cur = f.read()
    except OSError:
        cur = None
    if cur == text:
        print("%s is actueel" % os.path.relpath(a.out, ROOT))
        return 0
    if a.check:
        print("%s is niet actueel; draai scripts/gen-dongle-inventaris.py" % os.path.relpath(a.out, ROOT), file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out + ".tmp", "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(a.out + ".tmp", a.out)
    print("%s geschreven" % os.path.relpath(a.out, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

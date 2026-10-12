#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Zet de statusregel die de OLED-saver (`ravengun --saver`) op de tv toont: een korte, heldere zin met
# wat RavenGun nu doet. De saver leest de eerste regel van dit bestand en toont eronder de tijd en
# hoelang het stil is (sinds de laatste wijziging van dit bestand). Zo kan de eigenaar de laptop
# dichtdoen en op de tv de voortgang volgen.
#
# Gebruik: scripts/rg-saver-status.sh "korte zin"      (leeg = toon alleen tijd/stil)
set -euo pipefail
STATE="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun"
mkdir -p "$STATE"
printf '%s\n' "${*:-RavenGun}" > "$STATE/saver-status.txt"

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# De markerbestanden van het gereedschap (.aan) horen nooit in de git-kluis. Ze horen bij een meting
# in de view van een speler: rg-rhio-stack.aan zet de stackmeting aan, rg-rhio-mutex.aan de
# mutex-haken, rg-compat.aan de meetdraad. Een marker die per ongeluk meegaat, verandert de volgende
# start van iedereen stil (review 12, punt 4).
#
# .gitignore heeft een uitzondering voor patches/*/dropin/** zodat de drop-in-DLL's en dropin.ini wel
# meegaan. Die uitzondering mag de markers niet meetrekken; deze proef controleert beide kanten.
set -uo pipefail
ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$ROOT" || exit 2
fout=0
zeg() { echo "markers: $*"; }

sporen=$(git ls-files -- '*.aan')
if [ -n "$sporen" ]; then
    zeg "FOUT: deze markerbestanden staan in git:"
    echo "$sporen"
    fout=1
else
    zeg "geen enkel .aan-bestand in git"
fi

for m in patches/RobinHood/dropin/rg-rhio-stack.aan \
         patches/RobinHood/dropin/rg-compat.aan \
         patches/MarioKartDX/dropin/rg-rhio-mutex.aan \
         patches/AfterDark/dropin/rg-rhio-stack.aan; do
    if git check-ignore -q "$m"; then
        zeg "wordt genegeerd (goed): $m"
    else
        zeg "FOUT: $m zou genegeerd moeten worden"
        fout=1
    fi
done

for m in patches/RobinHood/dropin/rg-rhio.dll \
         patches/RobinHood/dropin/dropin.ini; do
    if git check-ignore -q "$m"; then
        zeg "FOUT: $m wordt genegeerd maar hoort mee te gaan"
        fout=1
    else
        zeg "gaat mee (goed): $m"
    fi
done

if [ "$fout" != 0 ]; then
    zeg "mislukt"
    exit 1
fi
zeg "alles goed"

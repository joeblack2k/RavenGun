#!/usr/bin/env bash
# (symbols.txt: alle FMOD-namen uit de dynstr van de game, ook de const-methoden _ZNK...)
# Genereert fmod_stub.c uit symbols.txt. Diagnostische stub zonder geluid:
# iedere FMOD-functie retourneert een foutcode, zodat de game (hypothese) zonder audio verdergaat.
# Dit is GEEN FMOD-implementatie en bevat geen FMOD-code.
set -euo pipefail
cd "$(dirname "$0")"
{
  echo '/* Gegenereerd door gen.sh — niet handmatig bewerken. */'
  echo '#define STUB_ERR 52 /* niet-nul FMOD_RESULT: "initialisatie mislukt" */'
  while read -r s; do echo "int $s(void) { return STUB_ERR; }"; done < symbols.txt
} > fmod_stub.c

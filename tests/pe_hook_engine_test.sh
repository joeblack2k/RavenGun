#!/usr/bin/env bash
# Toets de PE-haakengine (tools/rt-hasphook/rg_pe_hook.c) op Linux, zonder Wine en zonder spel.
#   bash tests/pe_hook_engine_test.sh
# Bouwt met -Wall -Wextra; elke waarschuwing is een fout.
#
# WAT DEZE TOETS NIET KAN (alleen in een echte run te zien): DllMain en het laden via TP_PRELOAD_DLL,
# VirtualProtect/FlushInstructionCache op echte image-pagina's, de echte omleidingen op
# ntdll!NtProtectVirtualMemory en kernel32!ExitProcess, het moment van de trigger t.o.v. de pakker,
# en de Windows-x64-aanroepconventie van de handlers (hier alleen de stubs, via een asm-aanroeper).
set -u
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
gcc -Wall -Wextra -Werror -O1 -mno-red-zone -I"$R/tools/rt-hasphook" -o "$T/toets" \
    "$R/tests/pe_hook_engine_test.c" "$R/tools/rt-hasphook/rg_pe_hook.c" -lpthread
rc=$?
[ $rc -eq 0 ] || { echo "bouwen mislukt (gcc exitcode $rc)"; exit 1; }
"$T/toets"

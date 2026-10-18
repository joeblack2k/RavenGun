#!/usr/bin/env bash
# Bouwt librg-oss-stub.so (i386, LD_PRELOAD-shim) naast de bron.
# De bron is header-vrij, dus er zijn GEEN 32-bits glibc-ontwikkelheaders nodig
# (op deze machine ontbreekt /usr/include/gnu/stubs-32.h; een gewone -m32-bouw met <dlfcn.h> stopt daarop).
set -euo pipefail
HIER="$(cd "$(dirname "$0")" && pwd)"
gcc -m32 -shared -fPIC -O2 -Wall -Wextra -o "$HIER/librg-oss-stub.so" "$HIER/rg_oss_stub.c" -ldl -lpthread
file "$HIER/librg-oss-stub.so"
nm -D --defined-only "$HIER/librg-oss-stub.so" | grep -E ' T (open|open64|openat|openat64|ioctl|close|write)$'

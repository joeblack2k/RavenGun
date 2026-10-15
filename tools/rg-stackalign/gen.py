#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Schrijft rg_stackalign.c uit symbols.txt (een functienaam per regel). Gebruik: python3 -I tools/rg-stackalign/gen.py
#
# Waarom: oude i386-games (Lindbergh, gebouwd met 4-bytes stapeluitlijning) roepen de GL-bibliotheken van SteamOS aan met een
# stapel die niet op 16 bytes staat; die bibliotheken (lib32, met SSE gebouwd) gaan daar wel van uit. Too Spicy viel zo op
# `movaps %xmm0,0xc(%esp)` in /usr/lib32/libGLdispatch.so.0 (__glDispatchCheckMultithreaded), bench 2026-10-15. Elke functie
# hieronder lijnt de stapel uit (force_align_arg_pointer) en geeft 24 dwords door aan de echte functie (RTLD_NEXT). Functies uit
# glXGetProcAddress(ARB) krijgen een wrapper uit een vaste voorraad.
import os

HERE = os.path.dirname(os.path.abspath(__file__))
NARGS = 24      # dwords: gluLookAt heeft 9 doubles = 18
POOL = 4096    # wrappers voor functies uit glXGetProcAddress (Too Spicy vraagt er meer dan 512)

syms = [l.strip() for l in open(os.path.join(HERE, "symbols.txt")) if l.strip() and not l.startswith("#")]
gpa = {"glXGetProcAddress", "glXGetProcAddressARB"}
args = ", ".join(f"uint32_t a{i}" for i in range(NARGS))
pass_ = ", ".join(f"a{i}" for i in range(NARGS))

out = []
w = out.append
w("// SPDX-License-Identifier: GPL-3.0-or-later")
w("// GEGENEREERD door tools/rg-stackalign/gen.py uit symbols.txt; niet met de hand wijzigen.")
w("// librg-stackalign.so: GL-aanroepen van een oude i386-game met een op 16 bytes uitgelijnde stapel (zie gen.py, WAAROM).")
w("#define _GNU_SOURCE")
w("#include <dlfcn.h>")
w("#include <stdint.h>")
w("#include <stdio.h>")
w("#include <stdlib.h>")
w("#include <string.h>")
w("")
w(f"typedef uint64_t (*rg_fn)({', '.join(['uint32_t'] * NARGS)});")
w(f"#define RG_N {len(syms)}")
w(f"#define RG_POOL {POOL}")
w("static const char *const rg_names[RG_N] = {")
for s in syms:
    w(f'    "{s}",')
w("};")
w("static void *rg_real[RG_N];")
w("static void *rg_pool_target[RG_POOL];")
w("static int rg_pool_used;")
w("static int rg_logged;")
w("")
w("static void *rg_resolve(int i) {")
w("    void *p = __atomic_load_n(&rg_real[i], __ATOMIC_ACQUIRE);")
w("    if (p) return p;")
w("    p = dlsym(RTLD_NEXT, rg_names[i]);")
w("    if (!p) { fprintf(stderr, \"rg-stackalign: %s niet gevonden (RTLD_NEXT)\\n\", rg_names[i]); abort(); }")
w("    __atomic_store_n(&rg_real[i], p, __ATOMIC_RELEASE);")
w("    return p;")
w("}")
w("")
w("static void *rg_pool_wrap(void *target);")
w("")
for i, s in enumerate(syms):
    w(f"__attribute__((visibility(\"default\"), force_align_arg_pointer, noinline)) uint64_t {s}({args}) {{")
    if s in gpa:
        w(f"    void *p = (void *)(uintptr_t)((rg_fn)rg_resolve({i}))({pass_});")
        w("    return (uint64_t)(uintptr_t)rg_pool_wrap(p);")
    else:
        w(f"    return ((rg_fn)rg_resolve({i}))({pass_});")
    w("}")
w("")
w("// De voorraad voor functies uit glXGetProcAddress: wrapper k roept rg_pool_target[k] aan.")
for k in range(POOL):
    w(f"__attribute__((force_align_arg_pointer, noinline)) static uint64_t rg_pool_{k}({args}) {{ return ((rg_fn)rg_pool_target[{k}])({pass_}); }}")
w("static void *const rg_pool_fns[RG_POOL] = {")
w("    " + ", ".join(f"(void *)rg_pool_{k}" for k in range(POOL)))
w("};")
w("")
w("static void *rg_pool_wrap(void *target) {")
w("    if (!target) return NULL;")
w("    int n = __atomic_load_n(&rg_pool_used, __ATOMIC_ACQUIRE);")
w("    for (int k = 0; k < n; k++)")
w("        if (rg_pool_target[k] == target) return rg_pool_fns[k];")
w("    int k = __atomic_fetch_add(&rg_pool_used, 1, __ATOMIC_ACQ_REL);")
w("    if (k >= RG_POOL) {")
w("        if (!__atomic_exchange_n(&rg_logged, 1, __ATOMIC_ACQ_REL)) fprintf(stderr, \"rg-stackalign: voorraad op; verdere functies zonder uitlijning\\n\");")
w("        return target;")
w("    }")
w("    rg_pool_target[k] = target;")
w("    return rg_pool_fns[k];")
w("}")
w("")
w("// RG_XINITTHREADS=1: XInitThreads() voordat de game X gebruikt (oude games roepen het niet aan, maar laden texturen in een tweede")
w("// thread met een eigen GLX-context; HOTD EX gaf zonder shim een xcb-assert \"Unknown sequence number\").")
w("__attribute__((constructor)) static void rg_init(void) {")
w("    if (getenv(\"RG_STACKALIGN_QUIET\") == NULL) fprintf(stderr, \"rg-stackalign: %d GL-functies en %d voor glXGetProcAddress met uitgelijnde stapel\\n\", RG_N, RG_POOL);")
w("    const char *xt = getenv(\"RG_XINITTHREADS\");")
w("    if (xt && xt[0] == '1') {")
w("        int (*xinit)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, \"XInitThreads\");")
w("        fprintf(stderr, \"rg-stackalign: XInitThreads %s\\n\", xinit ? (xinit() ? \"aan\" : \"mislukt\") : \"niet gevonden\");")
w("    }")
w("}")
open(os.path.join(HERE, "rg_stackalign.c"), "w").write("\n".join(out) + "\n")
print(f"geschreven: rg_stackalign.c ({len(syms)} functies, voorraad {POOL})")

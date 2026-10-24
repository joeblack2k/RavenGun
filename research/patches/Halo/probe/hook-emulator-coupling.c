/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hook-emulator-coupling.c — bench-free proof that the hasphook's emulator coupling works
 * for Halo, with exactly the environment the native recipe sets.
 *
 * It reproduces hh_laad_emulator()/hh_koppel_emulator() from tools/rt-hasphook/hasp_inline_hook.c:
 * load the emulator with RTLD_LOCAL and resolve the eleven names by handle. It then checks that
 * the emulator's hasp_* exports did NOT reach the global namespace — which is the reason the
 * emulator is a dlopen target in the recipe and not an LD_PRELOAD entry.
 *
 * Usage: hook-emulator-coupling <path-to-ravengun-hasp-x64.so>
 * Exit 0 = all eleven names resolve.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static const char *namen[] = {
    "hasp_login", "hasp_login_scope", "hasp_logout", "hasp_read", "hasp_write",
    "hasp_encrypt", "hasp_decrypt", "hasp_get_size", "hasp_get_info",
    "hasp_get_sessioninfo", "hasp_free",
};
#define N (int)(sizeof namen / sizeof namen[0])

int main(int argc, char **argv)
{
    const char *pad = argc > 1 ? argv[1] : getenv("RAVENGUN_HASP_ELF_LIB");
    if (!pad || !*pad) { fprintf(stderr, "geen pad\n"); return 2; }

    void *h = dlopen(pad, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("  [FOUT] dlopen(RTLD_LOCAL) mislukt: %s\n", dlerror()); return 1; }
    printf("  [ok ] emulator met RTLD_LOCAL geladen: %s\n", pad);

    int miss = 0;
    for (int i = 0; i < N; i++) {
        void *p = dlsym(h, namen[i]);
        printf("  [%s] %s\n", p ? "ok " : "FOUT", namen[i]);
        if (!p) miss++;
    }

    /* de globale naamruimte: hier mag hasp_login NIET staan */
    void *g = dlsym(RTLD_DEFAULT, "hasp_login");
    printf("  [%s] hasp_login staat %s in de globale naamruimte\n",
           g ? "let" : "ok ", g ? "WEL" : "niet");

    dlclose(h);
    printf("  %s — %d ontbrekend(e) naam/en\n", miss ? "MISLUKT" : "GESLAAGD", miss);
    return miss ? 1 : 0;
}

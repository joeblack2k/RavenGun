// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * rg-exec: start een programma met een SCHONE omgeving, op de genoemde namen na.
 *
 *   rg-exec [--keep NAAM]... -- /absoluut/programma [argumenten...]
 *
 * Waarom: de daemon start een native game als `gamescope ... -- rg-exec --keep DISPLAY -- bwrap ...`. gamescope geeft zijn kind
 * veel eigen variabelen mee (ENABLE_GAMESCOPE_WSI zet zijn Vulkan-laag aan, SDL2_COMPAT, GAMESCOPE_*, vk_*, XDG_SESSION_TYPE, ...).
 * Het spel hoort van gamescope alleen DISPLAY te krijgen (het nummer van zijn Xwayland, dat pas bij het starten bekend is); de rest
 * zet bwrap uit het profiel, zoals scripts/play-native.sh dat met --clearenv deed. Gemeten 2026-10-14: zonder dit laadde de
 * WSI-laag van gamescope in Jurassic Park (OpenGL via zink) en kwam het venster "Gamescope WSI Layer Error: Hooking has failed".
 * Geen shell, geen PATH-zoektocht: het programma moet een absoluut pad zijn.
 */
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define MAX_KEEP 16

int main(int argc, char **argv) {
    const char *keep[MAX_KEEP];
    int nk = 0, i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (strcmp(argv[i], "--keep") == 0 && i + 1 < argc && nk < MAX_KEEP) { keep[nk++] = argv[++i]; continue; }
        fprintf(stderr, "rg-exec: onbekende optie '%s' (gebruik: rg-exec [--keep NAAM]... -- /pad/programma [argumenten])\n", argv[i]);
        return 64;
    }
    if (i >= argc || argv[i][0] != '/') {
        fprintf(stderr, "rg-exec: na -- hoort een programma met een absoluut pad\n");
        return 64;
    }
    char *envp[MAX_KEEP + 1];
    int ne = 0;
    for (int k = 0; k < nk; k++) {
        const char *v = getenv(keep[k]);
        if (!v) continue;
        size_t n = strlen(keep[k]) + strlen(v) + 2;
        char *s = static_cast<char *>(std::malloc(n));
        if (!s) return 70;
        snprintf(s, n, "%s=%s", keep[k], v);
        envp[ne++] = s;
    }
    envp[ne] = NULL;
    execve(argv[i], &argv[i], envp);
    fprintf(stderr, "rg-exec: %s: %s\n", argv[i], strerror(errno));
    return 127;
}

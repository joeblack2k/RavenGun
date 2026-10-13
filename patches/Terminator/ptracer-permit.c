/* ptracer-permit.c - 32-bits LD_PRELOAD-bibliotheek, de MAATREGEL.
 *
 * Zet in het proces dat deze bibliotheek laadt PR_SET_PTRACER op PR_SET_PTRACER_ANY. Daarmee staat
 * de kernel toe dat een WILLEKEURIG ander proces dit proces aanhaakt, ondanks
 * kernel.yama.ptrace_scope=1. In de laderrun is dat het proces waarin de gast draait -- precies het
 * proces waarop het kind van de gast ptrace(PTRACE_ATTACH, ouder) wil doen.
 *
 * WAAROM DIT EN NIET IETS ANDERS
 *  - per proces, niet hostbreed: er verandert NIETS aan /proc/sys/kernel/yama/ptrace_scope;
 *  - geen root nodig;
 *  - het dooft uit met het proces: zodra de game stopt, is de ruimte weg;
 *  - het is het mechanisme dat de kernel er ZELF voor geeft (PR_SET_PTRACER is de officiele
 *    ontsnappingsroute van Yama), niet een omweg eromheen.
 * De maatregel verandert niets aan de gast: geen patch, geen vervalste retourwaarde, geen aanpassing
 * van de controle zelf. Alleen de host wordt even toeschietelijk als de kast, die geen Yama had.
 *
 * Bouwen: gcc -m32 -shared -fPIC -O1 -o libravengun-ptracer-permit.so ptracer-permit.c
 * Gebruik: LD_PRELOAD=.../libravengun-ptracer-permit.so <lader> <game>
 *   Het logboek staat in $RAVENGUN_PTRACER_LOG, of anders in /tmp/ravengun-ptracer-permit.log
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>

#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif
#ifndef PR_SET_PTRACER_ANY
#define PR_SET_PTRACER_ANY ((unsigned long)-1)
#endif

static void meld(const char *fmt, ...)
{
    const char *pad = getenv("RAVENGUN_PTRACER_LOG");
    FILE *f = fopen(pad && *pad ? pad : "/tmp/ravengun-ptracer-permit.log", "a");
    va_list ap;
    if (!f) return;
    fprintf(f, "[ravengun-ptracer-permit] pid %d: ", (int)getpid());
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

__attribute__((constructor))
static void rg_ptracer_permit(void)
{
    int r = prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    if (r == 0)
        meld("PR_SET_PTRACER_ANY gezet: dit proces mag door een willekeurig proces worden aangehaakt "
             "(yama ptrace_scope blijft ongemoeid)");
    else
        meld("PR_SET_PTRACER_ANY MISLUKT: %s (errno %d) -- de maatregel doet dan niets",
             strerror(errno), errno);
}

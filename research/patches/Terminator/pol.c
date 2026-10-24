/* pol.c - een tracer-vrije kinder-teller. Leest /proc/<pid>/stat van ALLE processen en telt hoeveel er
 * als ouder de opgegeven pid hebben. Geen ptrace, geen LD_PRELOAD, geen signaal naar de gast: dit kijkt
 * alleen mee via /proc. Doel: zien of de 11 ms-metronoom er OOK is als onze eigen waarnemers NIET
 * meeladen, zodat wij kunnen uitsluiten dat onze instrumentatie de lus veroorzaakt.
 *
 * Bouwen (64-bits, draait BUITEN de sandbox):  gcc -O2 -o pol pol.c
 * Gebruik:  ./pol <ouderpid> <seconden>
 * Uitvoer per monster:  <t> <aantal kinderen met die ppid>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

static double nu(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* leest ppid uit /proc/<pid>/stat; veld 4, maar let op: de comm kan spaties bevatten, dus zoek de
   laatste ')' en tel vanaf daar. */
static int ppid_van(const char *pid)
{
    char pad[64], buf[4096];
    FILE *f;
    char *p;
    int veld = 0, ppid = -1;

    snprintf(pad, sizeof pad, "/proc/%s/stat", pid);
    f = fopen(pad, "r");
    if (!f) return -1;
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return -1; }
    fclose(f);
    p = strrchr(buf, ')');
    if (!p) return -1;
    p++;                       /* nu staan we op de spatie na de comm */
    /* velden vanaf hier: 3=state 4=ppid */
    for (; *p; p++) {
        if (*p == ' ') { veld++; if (veld == 1) { ppid = atoi(p + 1); break; } }
    }
    return ppid;
}

int main(int argc, char **argv)
{
    int doel, seconden, i;
    double t0, t;
    long monsters = 0, metkind = 0, totaal = 0;

    if (argc < 3) { fprintf(stderr, "gebruik: pol <ouderpid> <seconden>\n"); return 2; }
    doel = atoi(argv[1]);
    seconden = atoi(argv[2]);
    t0 = nu();
    while ((t = nu() - t0) < seconden) {
        DIR *d = opendir("/proc");
        struct dirent *e;
        int n = 0, p;
        if (!d) break;
        while ((e = readdir(d))) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            p = ppid_van(e->d_name);
            if (p == doel) n++;
        }
        closedir(d);
        printf("%.6f %d\n", t, n);
        monsters++;
        if (n > 0) metkind++;
        totaal += n;
    }
    fprintf(stderr,
            "monsters=%ld  met-minstens-1-kind=%ld (%.1f%%)  kindwaarnemingen-totaal=%ld\n",
            monsters, metkind, monsters ? 100.0 * metkind / monsters : 0.0, totaal);
    return 0;
}

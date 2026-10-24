/* pol2.c - snelle tracer-vrije kinder-teller. Leest /proc/<pid>/task/<pid>/children: dat is EEN bestand
 * met de pids van de directe kinderen, dus per monster een paar microseconden in plaats van een scan van
 * heel /proc (die ~6 ms kostte en daardoor kinderen van 0,4 ms bijna nooit kon vangen -- dat was de fout
 * in de eerste poging).
 *
 * Bouwen:  gcc -O2 -o pol2 pol2.c
 * Gebruik: ./pol2 <ouderpid> <seconden>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

static double nu(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* telt de pids in /proc/<pid>/task/<pid>/children (spatiegescheiden, één regel) */
static int kinderen(int pid)
{
    char pad[96], buf[8192];
    int fd, n;
    int aantal = 0;
    snprintf(pad, sizeof pad, "/proc/%d/task/%d/children", pid, pid);
    fd = open(pad, O_RDONLY);
    if (fd < 0) return -1;
    n = (int)read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    { char *p = buf; while (*p) { if (*p >= '0' && *p <= '9') { aantal++; while (*p >= '0' && *p <= '9') p++; } else p++; } }
    return aantal;
}

int main(int argc, char **argv)
{
    int doel, seconden;
    double t0, t;
    long monsters = 0, metkind = 0, totaal = 0, max = 0;

    if (argc < 3) { fprintf(stderr, "gebruik: pol2 <ouderpid> <seconden>\n"); return 2; }
    doel = atoi(argv[1]);
    seconden = atoi(argv[2]);
    t0 = nu();
    while ((t = nu() - t0) < seconden) {
        int n = kinderen(doel);
        if (n < 0) { fprintf(stderr, "kan /proc/%d/task/%d/children niet lezen\n", doel, doel); return 3; }
        printf("%.6f %d\n", t, n);
        monsters++; if (n > 0) metkind++; totaal += n; if (n > max) max = n;
    }
    fprintf(stderr, "monsters=%ld  met-minstens-1-kind=%ld (%.1f%%)  kindwaarnemingen=%ld  max-tegelijk=%ld\n",
            monsters, metkind, monsters ? 100.0 * metkind / monsters : 0.0, totaal, max);
    return 0;
}

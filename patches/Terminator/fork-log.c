/* fork-log.c - 32-bits LD_PRELOAD-bibliotheek, de KIND-WAARNEMER (geen tracer).
 *
 * Waarom dit bestaat: de A/B van 2026-10-13 liet in arm B **392 verschillende kindprocessen** zien in
 * 4 seconden (390 geslaagde ATTACH's), tegen **twee** in arm A. Dat verschil is het cijfer dat de poort
 * opent, maar het zegt nog niet of de gast daarna WERKT of SPINT. Deze bibliotheek meet dat: wanneer een
 * kind gemaakt wordt, door wie, hoe lang het leeft, hoeveel kinderen tegelijk bestaan, en of een kind
 * netjes eindigde of GESTOPT is.
 *
 * De gast importeert `fork`, `waitpid` en `exit` dynamisch (UND fork/waitpid/exit@GLIBC_2.0), dus die
 * drie zijn via de PLT te onderscheppen. Er is GEEN tracer nodig en er verandert niets aan het gedrag.
 *
 * Logboek: $RAVENGUN_FORK_LOG, of anders /tmp/ravengun-fork.log
 * Regelvorm: +<seconden sinds start> pid=<n> ppid=<n> <gebeurtenis> [velden]
 *
 *   start                      dit proces laadt de waarnemer
 *   kind-gemaakt kind=<pid>    een ouder maakt een kind
 *   kind-start                 het kind begint
 *   einde exit-code=<n>        het kind verlaat zich via exit()
 *   kind-opgeraapt kind=<pid> status=.. <geëindigd met code N | GESTOPT door signaal N>
 *                              de ouder raapt het kind op: DIT IS DE AUTORITEIT over het einde
 *
 * BELANGRIJK over de interpretatie: een ontbrekend `einde exit-code=` betekent NIET automatisch
 * "gestopt". Een kind dat via `_exit()` of een syscall verdwijnt laat die regel ook niet achter. De
 * waarheid staat in `kind-opgeraapt`, waar het echte wachtstatus-cijfer bij staat. kind-analyse.py
 * gebruikt daarom dat laatste veld, niet het eerste.
 *
 * Bouwen: gcc -m32 -shared -fPIC -O1 -o libravengun-fork-log.so fork-log.c -ldl
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/wait.h>

static pid_t (*echte_fork)(void);
static pid_t (*echte_waitpid)(pid_t, int *, int);
static void  (*echte_exit)(int);
static double t0;
static int t0_gezet;

static double nu(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Per regel openen/sluiten: een gebufferde FILE* zou na fork() in het kind een kopie van de buffer
   meedragen en bij _exit() kunnen wegvallen. */
static void meld(const char *fmt, ...)
{
    const char *pad = getenv("RAVENGUN_FORK_LOG");
    FILE *f = fopen(pad && *pad ? pad : "/tmp/ravengun-fork.log", "a");
    va_list ap;
    if (!f) return;
    if (!t0_gezet) { t0 = nu(); t0_gezet = 1; }
    fprintf(f, "+%9.6f pid=%-6d ppid=%-6d ", nu() - t0, (int)getpid(), (int)getppid());
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

pid_t fork(void)
{
    pid_t r;

    if (!echte_fork)
        echte_fork = (pid_t (*)(void))dlsym(RTLD_NEXT, "fork");
    if (!echte_fork) {
        meld("FOUT: de echte fork is niet te vinden");
        errno = ENOSYS;
        return -1;
    }

    errno = 0;
    r = echte_fork();
    if (r == 0) {
        meld("kind-start");
    } else if (r > 0) {
        meld("kind-gemaakt kind=%d", (int)r);
    } else {
        meld("fork MISLUKT errno=%d (%s)", errno, strerror(errno));
    }
    return r;
}

/* De AUTORITEIT over het einde van een kind: de ouder die het ophaalt, met het echte statuscijfer.
   De gast importeert `waitpid` (UND waitpid@GLIBC_2.0), maar `wait` is een APART symbool: bibliotheken
   of libc-internals kunnen dat gebruiken, dus die wikkelen we ook. Anders zou een kind dat via wait()
   wordt opgeraapt geen levensduur krijgen en lijkt het voor de analyse nog te leven. */
static void meld_opgeraapt(pid_t r, const int *status)
{
    if (r <= 0)
        return;
    /* LET OP: de aanroeper mag status==NULL meegeven (bv. `wait(NULL)`). Dan weten we DAT het kind
       geeindigd is maar niet hoe. Toch loggen: de levensduur en het aantal zijn de hoofdmeting, en
       anders zou een ouder die geen status opvraagt al zijn kinderen als 'nog levend' laten lijken. */
    if (!status) {
        meld("kind-opgeraapt kind=%d status=(niet opgevraagd door de gast)", (int)r);
        return;
    }
    if (WIFSIGNALED(*status))
        meld("kind-opgeraapt kind=%d status=0x%x GESTOPT door signaal %d",
             (int)r, *status, WTERMSIG(*status));
    else if (WIFEXITED(*status))
        meld("kind-opgeraapt kind=%d status=0x%x geëindigd met code %d",
             (int)r, *status, WEXITSTATUS(*status));
    else
        meld("kind-opgeraapt kind=%d status=0x%x (niet geeindigd, bv. WUNTRACED)", (int)r, *status);
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    pid_t r;

    if (!echte_waitpid)
        echte_waitpid = (pid_t (*)(pid_t, int *, int))dlsym(RTLD_NEXT, "waitpid");
    if (!echte_waitpid) { errno = ENOSYS; return -1; }

    r = echte_waitpid(pid, status, options);
    meld_opgeraapt(r, status);
    return r;
}

pid_t wait(int *status)
{
    static pid_t (*echte_wait)(int *);
    pid_t r;

    if (!echte_wait)
        echte_wait = (pid_t (*)(int *))dlsym(RTLD_NEXT, "wait");
    if (!echte_wait) { errno = ENOSYS; return -1; }

    r = echte_wait(status);
    meld_opgeraapt(r, status);
    return r;
}

void exit(int code)
{
    meld("einde exit-code=%d", code);
    if (!echte_exit)
        echte_exit = (void (*)(int))dlsym(RTLD_NEXT, "exit");
    if (echte_exit)
        echte_exit(code);
    _exit(code);          /* als de echte exit niet gevonden is */
}

__attribute__((constructor))
static void rg_fork_log_init(void)
{
    t0 = nu();
    t0_gezet = 1;
    meld("start (kind-waarnemer actief, meet alleen)");
}

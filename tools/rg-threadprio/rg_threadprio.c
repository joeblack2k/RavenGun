/* SPDX-License-Identifier: GPL-3.0-or-later
 * librg-threadprio.so -- een draad die real-time prioriteit vraagt en die niet mag krijgen, start toch.
 *
 * Arcadekasten draaien hun spel als root. De spellen maken draden met een EXPLICIETE planning (SCHED_FIFO, prioriteit
 * 50): pthread_attr_setinheritsched(PTHREAD_EXPLICIT_SCHED) + pthread_attr_setschedpolicy(SCHED_FIFO). Voor een gewone
 * gebruiker (RLIMIT_RTPRIO 0, geen CAP_SYS_NICE) geeft pthread_create dan EPERM, en het spel leest dat als een fout van
 * zijn eigen onderdeel. Gemeten op Jurassic Park (2026-10-13): StartThread voor de dongle-schrijfdraad -> EPERM ->
 * "Game dongle initialization failed!" (src/game_sys.c:760), terwijl elk dongle-antwoord goed was.
 *
 * Deze bibliotheek zet zich voor pthread_create. Lukt de aanroep, dan verandert er niets -- wie WEL real-time mag
 * (rtkit, een verhoogde limiet) houdt het. Geeft hij EPERM en vroeg de aanroeper een expliciete planning, dan volgt
 * een tweede poging met dezelfde attributen maar met de planning van de aanroepende draad (PTHREAD_INHERIT_SCHED).
 * Het spel krijgt zijn draad; alleen de voorrang is die van een gewone draad.
 *
 * Laden: LD_PRELOAD (de eigen lader zoekt namen met dlsym(RTLD_DEFAULT), dus een voorgeladen pthread_create wint).
 * RG_THREADPRIO_LOG=<bestand>  schrijft een laadbewijs en per teruggevallen draad een regel.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int (*echte_create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
static int rg_teruggevallen;

static void meld(const char *fmt, ...)
{
    const char *pad = getenv("RG_THREADPRIO_LOG");
    char b[256]; va_list ap; int n, fd;
    if (!pad || !*pad) return;
    va_start(ap, fmt); n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof b - 1) n = sizeof b - 1;
    fd = open(pad, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return;
    if (write(fd, b, (size_t)n) < 0) { /* een logboek dat niet schrijft mag het spel niet storen */ }
    close(fd);
}

__attribute__((constructor)) static void rg_threadprio_laad(void)
{
    meld("[rg-threadprio] geladen in pid %d (laadbewijs)\n", (int)getpid());
}

int pthread_create(pthread_t *t, const pthread_attr_t *attr, void *(*start)(void *), void *arg)
{
    int rc, inherit = PTHREAD_INHERIT_SCHED, policy = 0;
    struct sched_param sp;
    pthread_attr_t kopie;
    if (!echte_create)
        echte_create = (int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *))dlsym(RTLD_NEXT, "pthread_create");
    if (!echte_create) return ENOSYS;
    rc = echte_create(t, attr, start, arg);
    if (rc != EPERM || !attr) return rc;
    if (pthread_attr_getinheritsched(attr, &inherit) != 0 || inherit != PTHREAD_EXPLICIT_SCHED) return rc;
    memset(&sp, 0, sizeof sp);
    pthread_attr_getschedpolicy(attr, &policy);
    pthread_attr_getschedparam(attr, &sp);
    kopie = *attr;                                           /* glibc: een attr is een platte structuur; de stack-instellingen gaan mee */
    if (pthread_attr_setinheritsched(&kopie, PTHREAD_INHERIT_SCHED) != 0) return rc;
    rc = echte_create(t, &kopie, start, arg);
    rg_teruggevallen++;
    meld("[rg-threadprio] draad %d: beleid %d prioriteit %d geweigerd (EPERM); opnieuw met de planning van de aanroeper -> %d\n",
         rg_teruggevallen, policy, sp.sched_priority, rc);
    return rc;
}

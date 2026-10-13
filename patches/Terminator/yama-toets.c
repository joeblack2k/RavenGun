/* yama-toets.c - 32-bits, STATISCH, GEEN GAME.
 *
 * Bewijst op systeemniveau wat de gast overkomt: mag een KIND zijn OUDER aanhaken onder
 * kernel.yama.ptrace_scope=1, en maakt PR_SET_PTRACER_ANY dat mogelijk?
 *
 * Dat is precies de vorm die de strace van de laderbouwer liet zien:
 *   kind 2 doet ptrace(PTRACE_ATTACH, ouder) -> EPERM -> kind eindigt 1
 *   de ouder ziet dat, geeft kind 1 SIGKILL en beeindigt zichzelf met -1 (exit 255)
 *
 *   mode 0 : geen maatregel                     -> verwacht: ATTACH MISLUKT (EPERM)
 *   mode 1 : ouder doet PR_SET_PTRACER_ANY eerst -> verwacht: ATTACH GELUKT
 *
 * Bouwen: gcc -m32 -pie -fPIE -O1 -o yama-toets yama-toets.c
 * Gebruik: ./yama-toets 0|1
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif
#ifndef PR_SET_PTRACER_ANY
#define PR_SET_PTRACER_ANY ((unsigned long)-1)
#endif

int main(int argc, char **argv)
{
    int mode = (argc > 1) ? atoi(argv[1]) : 0;
    pid_t kind, ouder = getpid();
    int st;

    printf("=== mode %d (%s) ===\n", mode,
           mode ? "ouder doet PR_SET_PTRACER_ANY" : "geen maatregel");

    if (mode) {
        int r = prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
        printf("  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY) -> %d%s\n", r,
               r ? strerror(errno) : " (ok)");
        if (r) { printf("  -> maatregel NIET gezet; de rest zegt weinig\n"); }
    }

    fflush(stdout);
    kind = fork();
    if (kind < 0) { perror("fork"); return 2; }

    if (kind == 0) {
        /* KIND: haak de OUDER aan -- de upward-attach die Yama bij scope=1 verbiedt */
        long r;
        errno = 0;
        r = ptrace(PTRACE_ATTACH, ouder, 0, 0);
        printf("  kind: ptrace(PTRACE_ATTACH, %d) -> %ld, errno=%d (%s)\n",
               (int)ouder, r, errno, r ? strerror(errno) : "geen fout");
        fflush(stdout);                                  /* _exit() hieronder zou de buffer anders weggooien */
        if (r == 0) {
            waitpid(ouder, &st, 0);              /* de ouder staat nu stil */
            ptrace(PTRACE_DETACH, ouder, 0, 0);
            printf("  kind: gedetacheerd; CONCLUSIE: aanhaken van de ouder MAG\n");
            _exit(0);
        }
        printf("  kind: CONCLUSIE: aanhaken van de ouder MAG NIET\n");
        fflush(stdout);
        _exit(1);                                 /* precies wat de gast-kind doet */
    }

    /* OUDER */
    fflush(stdout);
    if (waitpid(kind, &st, 0) < 0) perror("waitpid");
    printf("  ouder: kind-eindstatus = %d (exitcode %d, signaal %d)\n",
           st, WIFEXITED(st) ? WEXITSTATUS(st) : -1, WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    return 0;
}

/* ptrace-log.c - 32-bits LD_PRELOAD-bibliotheek, de WAARNEMER (geen tracer!).
 *
 * Waarom dit bestaat: een strace-run BEWIJST de aanroep maar niet het gedrag zonder tracer, want
 * strace is zelf een tracer en verandert de omstandigheden (het kind dat wil aanhaken krijgt dan een
 * andere uitkomst). Deze bibliotheek is GEEN tracer: hij wikkelt alleen de ptrace-functie in het
 * proces zelf en schrijft op wat de gast vraagt en wat hij terugkrijgt. Daarmee is de oorzaak ook
 * buiten strace vast te stellen.
 *
 * Dit kan omdat `game` ptrace DYNAMISCH importeert (UND ptrace@GLIBC_2.0, symbool 347 in de dynsym),
 * dus de aanroep loopt via de PLT en is met LD_PRELOAD te onderscheppen.
 *
 * Bouwen: gcc -m32 -shared -fPIC -O1 -o libravengun-ptrace-log.so ptrace-log.c -ldl
 * Gebruik: LD_PRELOAD=.../libravengun-ptrace-log.so <lader> <game>
 *   Logboek: $RAVENGUN_PTRACE_LOG, of anders /tmp/ravengun-ptrace.log
 *
 * LET OP: deze bibliotheek laat elke aanroep ONGEWIJZIGD door naar de echte ptrace. Hij vervangt
 * niets en verandert geen uitkomst -- hij kijkt alleen mee.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/ptrace.h>
#include <sys/types.h>

static long (*echte_ptrace)(enum __ptrace_request, ...);
static FILE *log;

static void meld(const char *fmt, ...)
{
    va_list ap;
    if (!log) {
        const char *pad = getenv("RAVENGUN_PTRACE_LOG");
        log = fopen(pad && *pad ? pad : "/tmp/ravengun-ptrace.log", "a");
        if (!log) return;
        setvbuf(log, NULL, _IOLBF, 0);
    }
    fprintf(log, "[ravengun-ptrace-log] pid %d ppid %d: ", (int)getpid(), (int)getppid());
    va_start(ap, fmt);
    vfprintf(log, fmt, ap);
    va_end(ap);
    fputc('\n', log);
}

static const char *req_naam(long r)
{
    switch (r) {
    case PTRACE_TRACEME:   return "PTRACE_TRACEME";
    case PTRACE_PEEKTEXT:  return "PTRACE_PEEKTEXT";
    case PTRACE_PEEKDATA:  return "PTRACE_PEEKDATA";
    case PTRACE_POKETEXT:  return "PTRACE_POKETEXT";
    case PTRACE_POKEDATA:  return "PTRACE_POKEDATA";
    case PTRACE_CONT:      return "PTRACE_CONT";
    case PTRACE_KILL:      return "PTRACE_KILL";
    case PTRACE_SINGLESTEP:return "PTRACE_SINGLESTEP";
    case PTRACE_ATTACH:    return "PTRACE_ATTACH";
    case PTRACE_DETACH:    return "PTRACE_DETACH";
    case PTRACE_SYSCALL:   return "PTRACE_SYSCALL";
    case PTRACE_SETOPTIONS:return "PTRACE_SETOPTIONS";
    case PTRACE_GETEVENTMSG:return "PTRACE_GETEVENTMSG";
    case PTRACE_GETREGS:   return "PTRACE_GETREGS";
    case PTRACE_SETREGS:   return "PTRACE_SETREGS";
    case PTRACE_GETFPREGS: return "PTRACE_GETFPREGS";
    case PTRACE_SETFPREGS: return "PTRACE_SETFPREGS";
    case PTRACE_GETSIGINFO:return "PTRACE_GETSIGINFO";
    case PTRACE_SETSIGINFO:return "PTRACE_SETSIGINFO";
    default:               return "?";
    }
}

/* glibc's prototype is `long ptrace(enum __ptrace_request, ...)`, dus die signatuur moeten we exact
   overnemen, anders weigert de compiler de definitie (conflicting types). */
long ptrace(enum __ptrace_request request, ...)
{
    va_list ap;
    pid_t pid = 0;
    void *addr = NULL, *data = NULL;
    long r;

    va_start(ap, request);
    pid  = va_arg(ap, pid_t);
    addr = va_arg(ap, void *);
    data = va_arg(ap, void *);
    va_end(ap);

    if (!echte_ptrace)
        echte_ptrace = (long (*)(enum __ptrace_request, ...))dlsym(RTLD_NEXT, "ptrace");

    if (!echte_ptrace) {
        meld("%s(pid %d) -> de echte ptrace is niet te vinden; aanroep niet uitgevoerd",
             req_naam(request), (int)pid);
        errno = ENOSYS;
        return -1;
    }

    errno = 0;
    r = echte_ptrace(request, pid, addr, data);
    {
        int e = errno;
        if (request == PTRACE_TRACEME)
            meld("%s() -> %ld%s", req_naam(request), r, r ? "" : " (ok: ik laat mij volgen)");
        else if (request == PTRACE_ATTACH || request == PTRACE_DETACH)
            meld("%s(pid %d) -> %ld%s%s", req_naam(request), (int)pid, r,
                 r ? " FOUT: " : " (ok)",
                 r ? strerror(e) : "");
        else
            meld("%s(pid %d) -> %ld", req_naam(request), (int)pid, r);
        errno = e;
    }
    return r;
}

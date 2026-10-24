/* net-log.c - 32-bits LD_PRELOAD-bibliotheek, de NETWERK-WAARNEMER (geen tracer).
 *
 * Waarom dit bestaat: Terminator Salvation is een NetHASP-licentieclient. Zijn strings noemen
 * `<license_manager hostname="localhost">`, de `xlm_api_*`-functies (HASP HL License Manager API) en
 * de bail-regel "Too many consexutive dongle errs, bailing" -> exit na ~4 s. Op de kast leest
 * `ServiceDongleApp` de USB-dongle en bedient de licentie op localhost; hier is er geen van beide.
 * Deze waarnemer laat zien WAARHEEN de gast zijn licentie probeert te halen: connect/sendto-doel en
 * poort, of een broadcast. Dat bevestigt (of weerlegt) dat de 4-seconden-exit een mislukte
 * localhost-LM-verbinding is, en levert het protocol-aanknopingspunt voor een eigen LM.
 *
 * `game` importeert socket/connect/bind/sendto/recvfrom/send/recv/gethostbyname DYNAMISCH (UND in de
 * dynsym), dus de aanroepen lopen via de PLT en zijn met LD_PRELOAD te onderscheppen. Deze bibliotheek
 * laat elke aanroep ONGEWIJZIGD door naar de echte functie -- hij kijkt alleen mee.
 *
 * Bouwen: gcc -m32 -shared -fPIC -O1 -I<tools/rg-elfloader/inc> -o libravengun-net-log.so net-log.c -ldl
 *   (de -I levert gnu/stubs-32.h, die op SteamOS in de dev-headers ontbreekt)
 * Gebruik: LD_PRELOAD=.../libravengun-net-log.so <lader> <game>
 *   Logboek: $RAVENGUN_NET_LOG, of anders /tmp/ravengun-net.log
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

static int   (*echte_socket)(int, int, int);
static int   (*echte_connect)(int, const struct sockaddr *, socklen_t);
static int   (*echte_bind)(int, const struct sockaddr *, socklen_t);
static ssize_t (*echte_sendto)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
static ssize_t (*echte_recvfrom)(int, void *, size_t, int, struct sockaddr *, socklen_t *);
static struct timespec t0;
static int t0_gezet;

/* Rauwe write, zoals de andere waarnemers in deze map (anders logt de bestands-waarnemer onze
   logregels als bestandsopeningen van de gast). */
static void meld(const char *fmt, ...)
{
    char buf[1024], kern[768];
    va_list ap;
    int pre, n, fd;
    const char *pad;
    struct timespec ts;
    double dt;

    if (!t0_gezet) { clock_gettime(CLOCK_MONOTONIC, &t0); t0_gezet = 1; }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    dt = ((double)ts.tv_sec + (double)ts.tv_nsec / 1e9) -
         ((double)t0.tv_sec + (double)t0.tv_nsec / 1e9);

    va_start(ap, fmt);
    vsnprintf(kern, sizeof kern, fmt, ap);
    va_end(ap);

    pre = snprintf(buf, sizeof buf, "[ravengun-net-log] +%9.6f pid %d: %s\n",
                   dt, (int)getpid(), kern);
    if (pre < 0) return;
    n = (pre < (int)sizeof buf) ? pre : (int)sizeof buf - 1;

    pad = getenv("RAVENGUN_NET_LOG");
    if (!pad || !*pad) pad = "/tmp/ravengun-net.log";
    fd = open(pad, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    (void)write(fd, buf, n);
    close(fd);
}

/* Beschrijf een sockaddr: AF_INET -> ip:poort, AF_UNIX -> pad, anders de familie. */
static void beschrijf(char *uit, size_t groot, const struct sockaddr *sa, socklen_t len)
{
    if (!sa || len < (socklen_t)sizeof(sa->sa_family)) { snprintf(uit, groot, "<geen adres>"); return; }
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &in->sin_addr, ip, sizeof ip);
        snprintf(uit, groot, "AF_INET %s:%u", ip, (unsigned)ntohs(in->sin_port));
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        char ip[INET6_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET6, &in6->sin6_addr, ip, sizeof ip);
        snprintf(uit, groot, "AF_INET6 [%s]:%u", ip, (unsigned)ntohs(in6->sin6_port));
    } else if (sa->sa_family == AF_UNIX) {
        const struct sockaddr_un *un = (const struct sockaddr_un *)sa;
        snprintf(uit, groot, "AF_UNIX %.*s", (int)(sizeof un->sun_path), un->sun_path);
    } else {
        snprintf(uit, groot, "familie=%d", (int)sa->sa_family);
    }
}

static void init(void)
{
    if (!echte_socket)   echte_socket   = dlsym(RTLD_NEXT, "socket");
    if (!echte_connect)  echte_connect  = dlsym(RTLD_NEXT, "connect");
    if (!echte_bind)     echte_bind     = dlsym(RTLD_NEXT, "bind");
    if (!echte_sendto)   echte_sendto   = dlsym(RTLD_NEXT, "sendto");
    if (!echte_recvfrom) echte_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
}

int socket(int domain, int type, int protocol)
{
    int r;
    init();
    r = echte_socket(domain, type, protocol);
    meld("socket(domain=%d type=%d proto=%d) -> fd %d", domain, type & 0xff, protocol, r);
    return r;
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    char d[300]; int r, e;
    init();
    beschrijf(d, sizeof d, addr, len);
    r = echte_connect(fd, addr, len);
    e = errno;
    if (r == 0) meld("connect(fd %d -> %s) -> 0 (ok)", fd, d);
    else        meld("connect(fd %d -> %s) -> -1 errno=%d (%s)", fd, d, e, strerror(e));
    errno = e;
    return r;
}

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    char d[300]; int r, e;
    init();
    beschrijf(d, sizeof d, addr, len);
    r = echte_bind(fd, addr, len);
    e = errno;
    meld("bind(fd %d -> %s) -> %d%s", fd, d, r, r ? " FOUT" : "");
    errno = e;
    return r;
}

ssize_t sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *addr, socklen_t alen)
{
    char d[300]; ssize_t r; int e;
    init();
    beschrijf(d, sizeof d, addr, alen);
    r = echte_sendto(fd, buf, n, flags, addr, alen);
    e = errno;
    meld("sendto(fd %d, %zu bytes -> %s) -> %zd", fd, n, d, r);
    errno = e;
    return r;
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *addr, socklen_t *alen)
{
    ssize_t r; int e;
    init();
    r = echte_recvfrom(fd, buf, n, flags, addr, alen);
    e = errno;
    if (r >= 0) meld("recvfrom(fd %d) -> %zd bytes", fd, r);
    else        meld("recvfrom(fd %d) -> -1 errno=%d (%s)", fd, e, strerror(e));
    errno = e;
    return r;
}

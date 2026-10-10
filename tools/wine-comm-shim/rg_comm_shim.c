/* RavenGun: hulp voor de jvs-route (docs/architecture.md, "IO-brug", dialect jvs).
 *
 * Probleem: de game opent in Wine een COM-poort (Let's Go Safari: COM4) en vraagt GetCommModemStatus als sense-lijn. Wine doet dat met
 * ioctl(TIOCMGET) op het bestand dat dosdevices/com4 aanwijst. Onze daemon geeft de game een pty, en een pty kent TIOCMGET niet
 * (ENOTTY): de aanroep faalt dan. Deze LD_PRELOAD-bibliotheek vangt alleen dat geval af: slaagt de echte ioctl niet op een pty-slave,
 * dan geeft zij de modemstatus uit het bestand dat het JVS-dialect van de daemon bijhoudt (een byte met de Windows-bits CTS 0x10 en
 * DSR 0x20; geadresseerd = 0x30, anders 0x10). Zonder bestand: CTS en DSR aan. Alles anders gaat ongewijzigd naar de echte ioctl.
 *
 * De game van Let's Go Safari is een 32-bit proces (Wine met een 32-bit unix-kant), dus de hulp bestaat in 64 en 32 bit
 * (build/librg_comm_shim.so en build/lib32/librg_comm_shim.so). Daarom gebruikt dit bestand geen systeemheaders: de 32-bit
 * ontwikkelheaders van glibc staan niet op deze machine. De waarden van de ioctl-codes zijn die van Linux (x86 en x86-64 gelijk).
 *
 * Gebruik: LD_PRELOAD=pad64:pad32 voor het wine-proces (scripts/wine-prefix.sh: RG_LD_PRELOAD); de bibliotheek van de verkeerde
 * klasse wordt door ld.so met een melding genegeerd. Omgeving: RG_JVS_MODEM=pad overschrijft het statusbestand
 * (standaard /dev/shm/ravengun_jvs_modem). Eigen code, geen onderdeel van Wine.
 */
#define TIOCMGET 0x5415UL
#define TIOCMBIS 0x5416UL
#define TIOCMBIC 0x5417UL
#define TIOCMSET 0x5418UL
#define TIOCM_CTS 0x020
#define TIOCM_DSR 0x100
#define TIOCM_RNG 0x080
#define TIOCM_CAR 0x040
#define ENOTTY_ 25
#define EINVAL_ 22
#define O_RDONLY_ 0
#define O_CLOEXEC_ 02000000
#define RTLD_NEXT_ ((void*)-1L)

extern int* __errno_location(void);
extern void* dlsym(void* handle, const char* name);
extern int ttyname_r(int fd, char* buf, unsigned long buflen);
extern int open(const char* path, int flags, ...);
extern long read(int fd, void* buf, unsigned long n);
extern int close(int fd);
extern char* getenv(const char* name);

static int starts_with(const char* s, const char* p) {
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}

static int is_pty_slave(int fd) {
    char name[64];
    const int saved = *__errno_location();
    const int ok = ttyname_r(fd, name, sizeof name) == 0 && starts_with(name, "/dev/pts/");
    *__errno_location() = saved;
    return ok;
}

/* Linux-bits (TIOCM_*) uit de byte met Windows-bits (MS_CTS_ON 0x10, MS_DSR_ON 0x20, MS_RING_ON 0x40, MS_RLSD_ON 0x80). */
static int modem_bits(void) {
    const char* path = getenv("RG_JVS_MODEM");
    unsigned char b = 0x30;   /* geen bestand: CTS en DSR aan */
    const int saved = *__errno_location();
    int fd = open(path && *path ? path : "/dev/shm/ravengun_jvs_modem", O_RDONLY_ | O_CLOEXEC_);
    if (fd >= 0) {
        unsigned char v;
        if (read(fd, &v, 1) == 1) b = v;
        close(fd);
    }
    *__errno_location() = saved;
    int r = 0;
    if (b & 0x10) r |= TIOCM_CTS;
    if (b & 0x20) r |= TIOCM_DSR;
    if (b & 0x40) r |= TIOCM_RNG;
    if (b & 0x80) r |= TIOCM_CAR;
    return r;
}

/* ioctl heeft een derde argument van wisselend type; bij x86 en x86-64 is een pointer in dezelfde plek als een long. */
int ioctl(int fd, unsigned long request, void* arg) {
    static int (*real)(int, unsigned long, void*) = 0;
    if (!real) real = (int (*)(int, unsigned long, void*))dlsym(RTLD_NEXT_, "ioctl");
    const int r = real(fd, request, arg);
    if (r == -1 && (*__errno_location() == ENOTTY_ || *__errno_location() == EINVAL_) &&
        (request == TIOCMGET || request == TIOCMSET || request == TIOCMBIS || request == TIOCMBIC) && is_pty_slave(fd)) {
        if (request == TIOCMGET && arg) *(int*)arg = modem_bits();
        return 0;   /* DTR/RTS zetten: stil aanvaarden */
    }
    return r;
}

/* hasp_probe.c — nabootsing van een ElfLdr2-game met een STATISCH gelinkte HASP-client.
 *
 * Zelfde opzet als tools/rt-hasphook/hasp_zelftest.c, maar de fileid komt uit de omgeving
 * (RAVENGUN_PROBE_FILEID), zodat dezelfde test voor Jurassic Park (65522), Galaga Assault (65520)
 * en Terminator (65524) kan draaien. De "echte" client doet alsof hij met hardware praat en geeft
 * HASP_STATUS_NO_HARDWARE (3); de hook hoort hem nul keer aan te roepen.
 *
 * Bouwen:  gcc -m32 -rdynamic -o hasp_probe tools/rt-hasphook/hasp_probe.c
 * Draaien: LD_PRELOAD=<de hook> RAVENGUN_HASP_VERBOSE=1 RAVENGUN_HASP_GAME=<Game> \
 *          RAVENGUN_HASP_DIR=<.../dropin/ravengun-hasp> RAVENGUN_PROBE_FILEID=<fid> ./hasp_probe
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int echte_aanroepen = 0;

int hasp_login(unsigned vendor, const char *scope, void **handle)
{
    echte_aanroepen++;
    printf("ECHT: hasp_login(vendor=0x%08x)\n", vendor);
    return 3;
}
int hasp_login_scope(unsigned vendor, const char *scope, void **handle)
{
    echte_aanroepen++;
    printf("ECHT: hasp_login_scope\n");
    return 3;
}
int hasp_logout(void *handle)
{
    echte_aanroepen++;
    printf("ECHT: hasp_logout\n");
    return 3;
}
int hasp_read(void *handle, unsigned fileid, unsigned offset, unsigned length, void *buffer)
{
    echte_aanroepen++;
    printf("ECHT: hasp_read(fileid=%u, offset=%u, lengte=%u)\n", fileid, offset, length);
    return 3;
}
int hasp_write(void *handle, unsigned fileid, unsigned offset, unsigned length, const void *buffer)
{
    echte_aanroepen++;
    printf("ECHT: hasp_write(fileid=%u)\n", fileid);
    return 3;
}
int hasp_encrypt(void *handle, void *buffer, unsigned length)
{
    echte_aanroepen++;
    return 3;
}
int hasp_decrypt(void *handle, void *buffer, unsigned length)
{
    echte_aanroepen++;
    return 3;
}
int hasp_get_size(void *handle, unsigned fileid, unsigned *size)
{
    echte_aanroepen++;
    printf("ECHT: hasp_get_size(fileid=%u)\n", fileid);
    return 3;
}
int hasp_get_info(void *handle, const char *format, const char *scope, char **info)
{
    echte_aanroepen++;
    printf("ECHT: hasp_get_info\n");
    return 3;
}
int hasp_get_sessioninfo(void *handle, const char *format, char **info)
{
    echte_aanroepen++;
    printf("ECHT: hasp_get_sessioninfo\n");
    return 3;
}
int hasp_free(void *p)
{
    echte_aanroepen++;
    printf("ECHT: hasp_free(%p)\n", p);
    return 3;
}

static void *eigen_symbool(const char *naam)
{
    extern void *dlsym(void *, const char *);
    return dlsym((void *)0, naam);
}

int main(void)
{
    int (*login)(unsigned, const char *, void **);
    int (*read_)(void *, unsigned, unsigned, unsigned, void *);
    int (*get_size)(void *, unsigned, unsigned *);
    int (*get_info)(void *, const char *, const char *, char **);
    void *greep = NULL;
    unsigned grootte = 0;
    unsigned char buf[128];
    unsigned fid = 65524;
    const char *e;
    int rc, fouten = 0;

    printf("=== hasp_probe ===\n");
    e = getenv("RAVENGUN_PROBE_FILEID");
    if (e && *e)
        fid = (unsigned)strtoul(e, NULL, 0);
    printf("fileid onder test: %u (0x%x)\n", fid, fid);

    {
        const char *w = getenv("HASP_ZELFTEST_WACHT");
        long ms = (w && *w) ? strtol(w, NULL, 0) : 1500;
        printf("(wachten %ld ms op de hook)\n", ms);
        fflush(stdout);
        if (ms > 0)
            usleep((useconds_t)(ms * 1000));
    }

    login = (int (*)(unsigned, const char *, void **))eigen_symbool("hasp_login");
    read_ = (int (*)(void *, unsigned, unsigned, unsigned, void *))eigen_symbool("hasp_read");
    get_size = (int (*)(void *, unsigned, unsigned *))eigen_symbool("hasp_get_size");
    get_info = (int (*)(void *, const char *, const char *, char **))eigen_symbool("hasp_get_info");
    if (!login || !read_ || !get_size) {
        printf("FOUT: eigen hasp-symbolen niet gevonden (login=%p read=%p size=%p)\n",
               (void *)login, (void *)read_, (void *)get_size);
        return 2;
    }
    printf("eigen hasp_login op %p, hasp_read op %p, hasp_get_size op %p\n",
           (void *)login, (void *)read_, (void *)get_size);

    memset(buf, 0, sizeof(buf));
    rc = login(0xffff0000u, "probe", &greep);
    printf("hasp_login(0xffff0000) -> %d, greep = %p\n", rc, greep);
    if (rc != 0) fouten++;

    rc = get_size(greep, fid, &grootte);
    printf("hasp_get_size(%u) -> %d, grootte = %u\n", fid, rc, grootte);
    if (rc != 0) fouten++;

    rc = read_(greep, fid, 0, 16, buf);
    printf("hasp_read(%u, 0, 16) -> %d, eerste bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           fid, rc, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    if (rc != 0) fouten++;

    /* de offsets die Jurassic Park echt gebruikt: 0x3c en (index*4-4) */
    memset(buf, 0, sizeof(buf));
    rc = read_(greep, fid, 0x3c, 4, buf);
    printf("hasp_read(%u, 0x3c, 4) -> %d, bytes: %02x %02x %02x %02x\n",
           fid, rc, buf[0], buf[1], buf[2], buf[3]);
    if (rc != 0) fouten++;

    if (get_info) {
        char *info = NULL;
        rc = get_info(greep, NULL, NULL, &info);
        printf("hasp_get_info -> %d, info = %s\n", rc, info ? info : "(null)");
        if (rc != 0) fouten++;
    }

    printf("echte_aanroepen = %d\n", echte_aanroepen);
    if (echte_aanroepen != 0) {
        printf("RESULTAAT: de ECHTE client is aangeroepen (%d keer) -- de hook werkt NIET\n",
               echte_aanroepen);
        return 1;
    }
    if (fouten != 0) {
        printf("RESULTAAT: de hook is actief, maar de emulator gaf %d fout(en)\n", fouten);
        return 1;
    }
    printf("RESULTAAT: alle aanroepen zijn door de EMULATOR beantwoord; de statisch gelinkte "
           "client is NIET aangeroepen\n");
    return 0;
}

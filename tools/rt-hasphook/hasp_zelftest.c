/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hasp_zelftest.c — de zelftest van de HASP-inline-hook.
 *
 * Dit programma bootst een ElfLdr2-game na: het is een 32-bits ELF die zijn eigen `hasp_*`-functies
 * STATISCH in het beeld heeft (precies zoals `game` van Jurassic Park, Galaga Assault en Halo
 * Fireteam). Het roept ze aan via een functiepointer die het ZELF uit de dynsym haalt, zodat
 * symboolinterpositie geen rol speelt — net als in de game, waar de aanroepen rechtstreeks naar de
 * code in het eigen beeld gaan.
 *
 * De "echte" implementaties hieronder doen alsof ze met hardware praten: ze schrijven een regel
 * "ECHT: ..." en geven een fout terug. De hook hoort die regel te laten verdwijnen en in plaats
 * daarvan de emulator te laten antwoorden.
 *
 * Draaien:
 *   gcc -m32 -rdynamic -o hasp_zelftest hasp_zelftest.c
 *   LD_PRELOAD=<de hook> RAVENGUN_HASP_VERBOSE=1 ./hasp_zelftest
 *
 * Let op `-rdynamic`: dat zet de symbolen in de dynsym, zodat de hook ze kan vinden. Zonder die
 * vlag is dit programma geen goede nabootsing van de game.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------------- de "echte" cliënt */

/* Een teller zodat de compiler de functies niet wegoptimaliseert en zodat de test kan zien of de
   echte implementatie is aangeroepen. */
int echte_aanroepen;

int hasp_login(unsigned feature, const char *scope, void **handle)
{
    echte_aanroepen++;
    printf("ECHT: hasp_login(feature=%u) -> HASP_STATUS_NO_HARDWARE\n", feature);
    if (handle)
        *handle = NULL;
    return 3; /* HASP_STATUS_NO_HARDWARE */
}

int hasp_login_scope(unsigned feature, const char *scope, const char *format, void **handle)
{
    echte_aanroepen++;
    printf("ECHT: hasp_login_scope(feature=%u) -> HASP_STATUS_NO_HARDWARE\n", feature);
    if (handle)
        *handle = NULL;
    return 3;
}

int hasp_logout(void *handle)
{
    echte_aanroepen++;
    printf("ECHT: hasp_logout(%p)\n", handle);
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
    printf("ECHT: hasp_encrypt(lengte=%u)\n", length);
    return 3;
}

int hasp_decrypt(void *handle, void *buffer, unsigned length)
{
    echte_aanroepen++;
    printf("ECHT: hasp_decrypt(lengte=%u)\n", length);
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

/* ---------------------------------------------------------------- de aanroeper */

/*
 * De aanroepen gaan via een adres dat dit programma ZELF uit de dynsym van zijn eigen bestand
 * haalt. Dat is de kern van de nabootsing: geen PLT, geen GOT, geen symboolopzoeking tijdens het
 * draaien — precies zoals in de game.
 */
static void *vind_eigen_symbool(const char *naam)
{
    /* De eenvoudigste manier om bij het eigen dynsym-adres te komen zonder de hulpfuncties van de
       hook te gebruiken: dlsym met RTLD_DEFAULT levert hier het adres van ONZE eigen functie,
       omdat dit programma geen tweede definitie in een preload heeft (de hook exporteert de
       hasp-namen niet). */
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
    int rc;
    int fouten = 0;

    printf("=== hasp_zelftest ===\n");
    /* De hook werkt vanuit een kijker-thread: hij moet even de tijd krijgen om het beeld te vinden
       en de functies aan te haken. Een echt spel leeft lang genoeg; deze test moet erop wachten. */
    {
        const char *w = getenv("HASP_ZELFTEST_WACHT");
        long ms = (w && *w) ? strtol(w, NULL, 0) : 1500;
        if (ms > 0) {
            printf("(wachten %ld ms op de hook)\n", ms);
            fflush(stdout);
            usleep((useconds_t)(ms * 1000));
        }
    }
    login = (int (*)(unsigned, const char *, void **))vind_eigen_symbool("hasp_login");
    read_ = (int (*)(void *, unsigned, unsigned, unsigned, void *))vind_eigen_symbool("hasp_read");
    get_size = (int (*)(void *, unsigned, unsigned *))vind_eigen_symbool("hasp_get_size");
    get_info = (int (*)(void *, const char *, const char *, char **))vind_eigen_symbool("hasp_get_info");
    if (!login || !read_ || !get_size) {
        printf("FOUT: kon de eigen hasp-symbolen niet vinden (login=%p read=%p size=%p)\n",
               (void *)login, (void *)read_, (void *)get_size);
        return 2;
    }
    printf("hasp_login op %p, hasp_read op %p, hasp_get_size op %p\n", (void *)login,
           (void *)read_, (void *)get_size);

    memset(buf, 0, sizeof(buf));
    rc = login(0, NULL, &greep);
    printf("hasp_login -> %d, greep = %p\n", rc, greep);
    if (rc != 0)
        fouten++;
    if (greep == NULL)
        printf("LET OP: geen greep (de echte cliënt gaf er ook geen)\n");

    rc = get_size(greep, 65524, &grootte);
    printf("hasp_get_size(65524) -> %d, grootte = %u\n", rc, grootte);
    if (rc != 0)
        fouten++;

    rc = read_(greep, 65524, 0, 16, buf);
    printf("hasp_read(65524, 0, 16) -> %d, eerste bytes: %02x %02x %02x %02x\n", rc, buf[0], buf[1],
           buf[2], buf[3]);
    if (rc != 0)
        fouten++;

    if (get_info) {
        char *info = NULL;
        rc = get_info(greep, NULL, NULL, &info);
        printf("hasp_get_info -> %d, info = %s\n", rc, info ? info : "(null)");
        if (rc != 0)
            fouten++;
    }

    printf("echte_aanroepen = %d\n", echte_aanroepen);
    if (echte_aanroepen != 0) {
        printf("RESULTAAT: de ECHTE cliënt is aangeroepen (%d keer) — de hook werkt NIET\n",
               echte_aanroepen);
        return 1;
    }
    if (fouten != 0) {
        printf("RESULTAAT: de hook is actief, maar de emulator antwoordde met %d fout(en) "
               "(geen donglegegevens? zet RAVENGUN_HASP_DIR + RAVENGUN_HASP_GAME)\n", fouten);
        return 1;
    }
    printf("RESULTAAT: alle aanroepen zijn door de EMULATOR beantwoord; de statisch gelinkte "
           "cliënt is NIET aangeroepen\n");
    return 0;
}

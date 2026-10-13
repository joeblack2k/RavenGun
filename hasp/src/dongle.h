/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dongle.h — drop-in dongle-ondersteuning voor de eigen loader
 *
 * Deze laag leest een dongle-beschrijving uit de hasp-map en beantwoordt daarmee de
 * aanroepen van het spel. Er zit GEEN dongle-logica in: alles komt uit de beschrijving.
 *
 * Portable: Linux en Windows (mingw), geen afhankelijkheden buiten libc.
 *
 * De functiesignaturen zijn de ABI van de echte Sentinel/HASP-HL-runtime. Ze zijn nagemeten
 * op twee onafhankelijke exemplaren:
 *   - de Linux-koppeling van TeknoParrot (Halo Fireteam: g7/halo/halo.so, x86-64 SysV), en
 *   - de Windows-runtime van Time Crisis 5 (hasp_windows_x64_105293.dll, Win64).
 * Let op `hasp_read`/`hasp_write`: die hebben VIJF argumenten (greep, fileid, offset,
 * lengte, buffer) en niet vier. `hasp_encrypt`/`hasp_decrypt` werken in de buffer zelf
 * (greep, buffer, lengte). Zie docs/hasp-layer.md.
 */
#ifndef RAVENGUN_DONGLE_H
#define RAVENGUN_DONGLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- statuscodes */
/* Uit de HASP-API. De waarde 501 is geverifieerd in de bestaande ondersteuning:
 * `hasp_read` zet 0x1F5 (= 501) wanneer de handle of de buffer ongeldig is. */
#define HASP_STATUS_OK                    0
#define HASP_STATUS_INVALID_PARAMETER     501
#define HASP_STATUS_INSUFFICIENT_BUFFER   502
#define HASP_STATUS_NOT_FOUND             503
#define HASP_STATUS_IO_ERROR              505

/* De greep die de bestaande ondersteuning aan het spel geeft. Geverifieerd: halo.so schrijft
 * 0xE6F en de Windows-runtime gebruikt dezelfde soort waarde. */
#define DONGLE_HANDLE_VALUE               0xE6F

/* De versie die `hasp_get_version` meldt. Alleen informatief. */
#define DONGLE_HASP_VERSION               0x00010000u

/* ---------------------------------------------------------------- formaat */

typedef enum {
    DONGLE_NONE = 0,
    DONGLE_HASP_HL,
    DONGLE_REGION_PCB,
    DONGLE_SEGA_SCAN
} dongle_type_t;

#define DONGLE_MAX_MEMORY   8
#define DONGLE_MAX_FEATURES 16
#define DONGLE_MAX_KEYCAPS  8
#define DONGLE_MAX_STAGES   16

typedef struct {
    char         name[64];
    unsigned int fileid;
    unsigned int size;
    char         file[256];      /* pad binnen de dongle-map */
} dongle_memory_t;

typedef struct {
    int  stage;
    int  ok;                     /* 1 = goed, 0 = fout */
} dongle_stage_t;

typedef struct {
    dongle_type_t type;
    char          game[128];
    char          dir[512];      /* de map waarin dongle.json staat */

    /* type hasp-hl */
    unsigned int  haspid;
    unsigned int  features[DONGLE_MAX_FEATURES];
    int           nfeatures;
    char          keycaps[DONGLE_MAX_KEYCAPS][32];
    int           nkeycaps;
    dongle_memory_t memory[DONGLE_MAX_MEMORY];
    int           nmemory;
    char          port_type[16];
    unsigned int  port_address;

    /* type region-pcb */
    char          region[32];
    char          pcb[64];

    /* type sega-scan */
    dongle_stage_t stages[DONGLE_MAX_STAGES];
    int           nstages;

    /* opt-in: de tafel waaruit hasp_encrypt/hasp_decrypt antwoorden (zie dongle_crypto_replay).
     * Leeg = niet gebruikt: dan laten die twee functies de buffer ongemoeid. */
    char          crypto_table[512];
} dongle_t;

/* ---------------------------------------------------------------- laden */

/*
 * Laadt <dir>/dongle.json. Geeft 0 bij succes, -1 bij een fout.
 * `dir` mag zowel met als zonder afsluitende schuine streep worden doorgegeven.
 * Op Windows wordt een Unix-pad (/home/...) omgezet naar het Z-station van Wine, zodat
 * dezelfde omgevingsvariabele voor de Linux- en de Windows-laag werkt.
 */
int dongle_load(const char *dir, dongle_t *out);

/*
 * Zoekt in `root` een map waarvan dongle.json het veld "game" heeft dat overeenkomt met
 * `game` (hoofdletterongevoelig). Geeft 0 bij gevonden, -1 anders.
 * Eerst wordt <root>/<game> geprobeerd (de naam van de map); lukt dat niet, dan wordt elke
 * onderliggende map gelezen en vergeleken op het veld "game" en op de mapnaam.
 */
int dongle_find(const char *root, const char *game, dongle_t *out);

/* ---------------------------------------------------------------- hulpmiddelen */

/*
 * Zet de 32-bits binaire vorm van `fileid` in `out` (32 tekens + afsluitende nul),
 * hoogste bit eerst. Dit is DE regel van het formaat:
 *   fileid 65524 = 0x0000FFF4  ->  "00000000000000001111111111110100"
 */
void dongle_fileid_name(unsigned int fileid, char out[33]);

/*
 * Stelt de XML samen die `hasp_get_info` teruggeeft. `extended` = 0 geeft de korte vorm,
 * 1 de uitgebreide descriptor. Geeft de lengte, of -1 als de buffer te klein is.
 */
int dongle_get_info_xml(const dongle_t *d, int extended, char *buf, size_t buflen);

/* Leest/schrijft een geheugenbestand van de dongle, opgezocht via de fileid. */
int dongle_read (const dongle_t *d, unsigned int fileid, void *buf, size_t len);
int dongle_write(const dongle_t *d, unsigned int fileid, const void *buf, size_t len);

/* Als dongle_read/dongle_write, maar vanaf `offset` in het geheugenbestand. */
int dongle_read_at (const dongle_t *d, unsigned int fileid, unsigned int offset, void *buf, size_t len);
int dongle_write_at(const dongle_t *d, unsigned int fileid, unsigned int offset, const void *buf, size_t len);

/* Geeft de beschreven grootte van een fileid, of 0 als hij niet bestaat. */
unsigned int dongle_memory_size(const dongle_t *d, unsigned int fileid);

/*
 * OPT-IN antwoordtafel voor hasp_encrypt/hasp_decrypt (Terminator Salvation).
 *
 * Sommige games laten hun databestanden met een sleutel ontsleutelen die via de dongle loopt:
 * het spel geeft een buffer aan hasp_encrypt/hasp_decrypt en gebruikt het resultaat. De echte
 * dongle antwoordt daar deterministisch op, en de game levert die antwoorden zelf mee: in haar
 * map `hasp/` staat een bestand per aanroep, genoemd naar de invoer. De regel (gemeten aan
 * Terminator Salvation, 8 van 8 treffers):
 *
 *   naam = kleine hexadecimale tekst van de EERSTE 32 bytes van de invoerbuffer (64 tekens)
 *          en, als dat bestand niet bestaat, van de eerste 16 bytes (32 tekens);
 *   de 32-bytes-vorm wordt alleen geprobeerd als lengte >= 32;
 *   de 16-bytes-vorm als lengte >= 16;
 *   inhoud = het antwoord; de eerste min(lengte, bestandsgrootte) bytes komen in de buffer.
 *
 * Dezelfde map dient voor encrypt en decrypt. De map komt uit RAVENGUN_HASP_CRYPTO_TABLE, of
 * anders uit het veld "crypto_table" van dongle.json. Een relatief pad geldt vanuit de werkmap
 * van het proces. Is geen van beide gezet, dan doet dongle_crypto_replay niets en blijft de
 * buffer zoals hij was (het gedrag van elke andere game).
 *
 * Geeft 1 bij een treffer (buffer is overschreven), 0 bij geen tafel of geen treffer.
 * `*gebruikt` (mag NULL zijn) krijgt 1 als er een tafel was ingesteld, ook bij een misser.
 */
int dongle_crypto_replay(const dongle_t *d, void *buffer, size_t length, int *gebruikt,
                         char *naam_uit, size_t naam_sz);

/* ---------------------------------------------------------------- HASP-API */

/*
 * De functies die het spel aanroept. Ze gebruiken één wereldwijd geladen dongle
 * (zie dongle_install). Elke functie geeft een HASP_STATUS_* terug.
 *
 * De signaturen zijn de ABI van de echte runtime; zie de kop van dit bestand.
 */
int dongle_install(const dongle_t *d);   /* zet de actieve dongle; 0 bij succes */
void dongle_uninstall(void);

/* De actief geïnstalleerde dongle, of NULL als er geen is. */
const dongle_t *dongle_active(void);

/*
 * Laadt en installeert de dongle uit de omgeving. Dit is wat de vooraf te laden
 * bibliotheek (Linux) en de DllMain (Windows) doen.
 *   RAVENGUN_HASP_DIR      waar de dongles staan (standaard /hasp)
 *   RAVENGUN_HASP_GAME     de naam van de game (verplicht; anders gebeurt er niets)
 *   RAVENGUN_HASP_VERBOSE  als hij gezet is: regels op stderr
 *   RAVENGUN_HASP_WINPREFIX  Windows-only: het station van de Unix-wortel (standaard Z:)
 * Geeft 0 als er een dongle actief is, -1 anders.
 */
int dongle_install_from_env(void);

/* De omgeving, uitgelezen; alleen voor tests en voor scripts/hasp.py. */
const char *dongle_env_root(void);
const char *dongle_env_game(void);

int hasp_login(unsigned int vendor_code, const char *scope, void **handle);
int hasp_login_scope(unsigned int vendor_code, const char *scope, const char *format, void **handle);
int hasp_login_ex(unsigned int vendor_code, const char *scope, const char *format, void **handle);
int hasp_login_port(unsigned int vendor_code, const char *port, const char *scope, void **handle);
int hasp_logout(void *handle);
int hasp_get_info(void *handle, const char *format, const char *scope, char **info);
int hasp_get_sessioninfo(void *handle, const char *format, char **info);
int hasp_get_size(void *handle, unsigned int fileid, unsigned int *size);
int hasp_read(void *handle, unsigned int fileid, unsigned int offset, unsigned int length, void *buffer);
int hasp_write(void *handle, unsigned int fileid, unsigned int offset, unsigned int length, const void *buffer);
int hasp_encrypt(void *handle, void *buffer, unsigned int length);
int hasp_decrypt(void *handle, void *buffer, unsigned int length);
int hasp_free(void *p);

/* De overige namen uit de runtime. Ze worden door de gemeten games niet aangeroepen; ze
 * bestaan zodat de bibliotheek een volledige vervanging is (de imports van een spel gaan
 * op ordinalen en een ontbrekende naam laat het laden mislukken). */
int hasp_get_version(void *handle, unsigned int *version);
int hasp_detach(void *handle);
int hasp_update(void *handle, const char *scope);
int hasp_get_rtc(void *handle, void *time);
int hasp_get_trace(void *handle, char **trace);
int hasp_enable_trace(void *handle, unsigned int enable);
int hasp_legacy_encrypt(void *handle, void *buffer, unsigned int length);
int hasp_legacy_decrypt(void *handle, void *buffer, unsigned int length);
int hasp_legacy_set_idletime(void *handle, unsigned int seconds);
int hasp_legacy_set_rtc(void *handle, const void *time);
int hasp_datetime_to_hasptime(const void *datetime, void *hasptime);
int hasp_hasptime_to_datetime(const void *hasptime, void *datetime);

#ifdef __cplusplus
}
#endif

#endif /* RAVENGUN_DONGLE_H */

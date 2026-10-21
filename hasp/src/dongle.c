/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dongle.c — het lezen van de beschrijving, de geheugenbestanden en de HASP-API.
 *
 * Bevat een kleine, doelgerichte JSON-lezer: genoeg voor het dongle-formaat, geen
 * afhankelijkheden. Zie docs/hasp-layer.md voor het formaat en de API.
 *
 * Draait ongewijzigd op Linux (de ELF-games) en op Windows (mingw, de PE-DLL voor de
 * games onder Proton).
 */
#include "dongle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>  /* GetCurrentProcessId for the log file */
#else
#include <unistd.h>   /* getpid for the log file */
#endif

#ifndef _WIN32
#include <dirent.h>   /* voor de lineaire scan in dongle_find */
#endif

/* ============================================================ kleine JSON-lezer */

static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Zoekt "key" op het hoogste niveau en geeft een pointer naar de waarde. */
static const char *json_find(const char *json, const char *key)
{
    size_t klen = strlen(key);
    int depth = 0;
    const char *p = json;
    while (*p) {
        if (*p == '{' || *p == '[') { depth++; p++; continue; }
        if (*p == '}' || *p == ']') { depth--; p++; continue; }
        if (*p == '"' && depth == 1) {
            const char *start = ++p;
            while (*p && *p != '"') p++;
            size_t len = (size_t)(p - start);
            const char *after = skip_ws(p + 1);
            if (len == klen && strncmp(start, key, klen) == 0 && *after == ':') {
                return skip_ws(after + 1);
            }
            if (*p) p++;
            continue;
        }
        p++;
    }
    return NULL;
}

static int json_str(const char *json, const char *key, char *out, size_t outlen)
{
    const char *v = json_find(json, key);
    if (!v || *v != '"') return -1;
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i + 1 < outlen) {
        if (*v == '\\' && v[1]) v++;          /* eenvoudige ontsnapping */
        out[i++] = *v++;
    }
    out[i] = '\0';
    return 0;
}

static int json_uint(const char *json, const char *key, unsigned int *out)
{
    const char *v = json_find(json, key);
    if (!v) return -1;
    char *end = NULL;
    unsigned long n = strtoul(v, &end, 10);
    if (end == v) return -1;
    *out = (unsigned int)n;
    return 0;
}

/* Een eigen kopie van een tekenreeks; `strdup` is niet overal even netjes gedeclareerd. */
static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Een regiowaarde lezen: het formaat van de laag is een object
 *   "region": { "field": "DongleRegion", "name": "JAPAN", "game_name": "JPN" }
 * (docs/region-layer.md). Een losse tekenreeks blijft ook werken.
 *
 * Dit is een echte fout geweest: json_str() eist een " en een object begint met {, dus met het
 * objectformaat bleven out->region en out->pcb leeg -- de emulator las het veld niet dat de
 * gegevens wel hadden. De waarden worden hier alleen vastgelegd (de regiocode van de game is nog
 * niet bekend; docs/region-layer.md paragraaf 6), maar wie hem later uitleest hoort de waarde te
 * zien die in dongle.json staat. */
static void json_naam(const char *json, const char *key, char *out, size_t outlen)
{
    const char *v = json_find(json, key);
    if (!v) return;
    if (*v == '"') { json_str(json, key, out, outlen); return; }
    if (*v != '{') return;
    char *tmp = dup_str(v);
    if (!tmp) return;
    /* alleen het object zelf: afkappen op de bijbehorende sluit-accolade */
    int depth = 0;
    for (char *p = tmp; *p; p++) {
        if (*p == '{') depth++;
        else if (*p == '}') { depth--; if (depth == 0) { p[1] = '\0'; break; } }
    }
    json_str(tmp, "name", out, outlen);
    free(tmp);
}

/* Loopt over de objecten in een array met de gegeven naam. */
typedef void (*obj_cb)(const char *obj_begin, const char *obj_end, void *user);

static void json_array_objects(const char *json, const char *key, obj_cb cb, void *user)
{
    const char *v = json_find(json, key);
    if (!v || *v != '[') return;
    v++;
    const char *obj = NULL;
    int depth = 0;
    for (const char *p = v; *p; p++) {
        if (*p == '{') {
            if (depth == 0) obj = p;
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0 && obj) { cb(obj, p + 1, user); obj = NULL; }
        } else if (*p == ']' && depth == 0) {
            break;
        }
    }
}

/* ============================================================ sporen (RAVENGUN_HASP_VERBOSE) */

/*
 * Met RAVENGUN_HASP_VERBOSE in de omgeving schrijft de laag één regel per aangeroepen
 * functie naar stderr. Dat is het bewijs dat de emulator in het proces geladen is en dat
 * het spel hem ook echt aanspreekt; zonder die variabele gebeurt er niets.
 *
 * Er zit een plafond op het aantal regels, zodat een spel dat duizenden keren leest het
 * logboek niet vult.
 */
#define HASP_TRACE_MAX 64
static int g_trace_lines = 0;

static int hasp_verbose(void)
{
    const char *v = getenv("RAVENGUN_HASP_VERBOSE");
    return v && *v && strcmp(v, "0") != 0;
}

/*
 * Every log line goes to stderr and, when RAVENGUN_HASP_LOG names a file, is also appended to that
 * file with the process id in front. A Windows game started by its own launcher (Lost Land
 * Adventure: RSLauncher.exe -> DomeShooterGame-Win64-Shipping.exe) gets no stderr handle, so
 * without the file its calls are never seen. The file is opened per line, so the launcher and the
 * game can share it.
 */
static void dongle_path(char *out, size_t outlen, const char *in);

static void hasp_log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fputs(line, stderr);
    fflush(stderr);
    const char *path = getenv("RAVENGUN_HASP_LOG");
    if (path && *path) {
        char fpath[1024];
        dongle_path(fpath, sizeof fpath, path);
        FILE *f = fopen(fpath, "a");
        if (f) {
#ifdef _WIN32
            fprintf(f, "[pid %lu] %s", (unsigned long)GetCurrentProcessId(), line);
#else
            fprintf(f, "[pid %ld] %s", (long)getpid(), line);
#endif
            fclose(f);
        }
    }
}

static void hasp_trace(const char *fn, const char *detail)
{
    if (!hasp_verbose()) return;
    if (g_trace_lines >= HASP_TRACE_MAX) return;
    g_trace_lines++;
    if (g_trace_lines == HASP_TRACE_MAX)
        hasp_log("[ravengun-hasp] %s: verdere aanroepen worden niet meer gemeld\n", fn);
    else
        hasp_log("[ravengun-hasp] %s%s%s\n", fn, detail ? " " : "", detail ? detail : "");
}

/* ============================================================ paden (Windows) */

/*
 * Onder Wine is de Unix-wortel het Z-station. Dezelfde RAVENGUN_HASP_DIR moet zowel voor de
 * Linux-laag (LD_PRELOAD) als voor de Windows-laag (de PE-DLL) werken; daarom zetten we een
 * Unix-pad hier om. Met RAVENGUN_HASP_WINPREFIX is het station te kiezen (standaard Z:).
 */
static void dongle_path(char *out, size_t outlen, const char *in)
{
#ifdef _WIN32
    if (in && in[0] == '/' ) {
        const char *pfx = getenv("RAVENGUN_HASP_WINPREFIX");
        if (!pfx || !*pfx) pfx = "Z:";
        snprintf(out, outlen, "%s%s", pfx, in);
        return;
    }
#endif
    snprintf(out, outlen, "%s", in ? in : "");
}

/* ============================================================ inlezen */

static void add_memory(const char *b, const char *e, void *user)
{
    dongle_t *d = (dongle_t *)user;
    if (d->nmemory >= DONGLE_MAX_MEMORY) return;
    size_t len = (size_t)(e - b);
    char *tmp = (char *)malloc(len + 1);
    if (!tmp) return;
    memcpy(tmp, b, len);
    tmp[len] = '\0';

    dongle_memory_t *m = &d->memory[d->nmemory];
    memset(m, 0, sizeof(*m));
    if (json_str(tmp, "name", m->name, sizeof(m->name)) != 0)
        strcpy(m->name, "memory");
    json_uint(tmp, "fileid", &m->fileid);
    json_uint(tmp, "size", &m->size);
    json_str(tmp, "file", m->file, sizeof(m->file));
    d->nmemory++;
    free(tmp);
}

static void add_stage(const char *b, const char *e, void *user)
{
    dongle_t *d = (dongle_t *)user;
    if (d->nstages >= DONGLE_MAX_STAGES) return;
    size_t len = (size_t)(e - b);
    char *tmp = (char *)malloc(len + 1);
    if (!tmp) return;
    memcpy(tmp, b, len);
    tmp[len] = '\0';

    dongle_stage_t *s = &d->stages[d->nstages];
    unsigned int stage = 0;
    json_uint(tmp, "stage", &stage);
    s->stage = (int)stage;
    char res[16] = {0};
    json_str(tmp, "result", res, sizeof(res));
    s->ok = (strcmp(res, "ok") == 0) ? 1 : 0;
    d->nstages++;
    free(tmp);
}

static void read_list(const char *json, const char *key, char out[][32], int max, int *n)
{
    const char *v = json_find(json, key);
    if (!v || *v != '[') return;
    v++;
    while (*v && *v != ']') {
        v = skip_ws(v);
        if (*v == '"') {
            const char *s = ++v;
            while (*v && *v != '"') v++;
            if (*n < max) {
                size_t len = (size_t)(v - s);
                if (len > 31) len = 31;
                memcpy(out[*n], s, len);
                out[*n][len] = '\0';
                (*n)++;
            }
        }
        if (*v) v++;
        v = skip_ws(v);
        if (*v == ',') v++;
    }
}

static void read_features(const char *json, unsigned int *out, int max, int *n)
{
    const char *v = json_find(json, "features");
    if (!v || *v != '[') return;
    v++;
    while (*v && *v != ']') {
        v = skip_ws(v);
        if (isdigit((unsigned char)*v)) {
            char *end = NULL;
            unsigned long x = strtoul(v, &end, 10);
            if (*n < max) out[(*n)++] = (unsigned int)x;
            v = end;
        } else {
            v++;
        }
        v = skip_ws(v);
        if (*v == ',') v++;
    }
}

int dongle_load(const char *dir, dongle_t *out)
{
    if (!dir || !out) return -1;
    memset(out, 0, sizeof(*out));

    char omgezet[512];
    dongle_path(omgezet, sizeof(omgezet), dir);
    /* The memory files are opened at hasp_read/hasp_write time, not here, so a relative dir is
     * made absolute now: a game may change its working directory after the DLL loaded (Lost Land
     * Adventure's UE3 game moves to Binaries/Win64, and every hasp_read of fileid 65520 then failed,
     * 2026-10-21). If that fails the dir stays as given. */
#ifdef _WIN32
    char vol[512];
    if (_fullpath(vol, omgezet, sizeof(vol))) snprintf(omgezet, sizeof(omgezet), "%s", vol);
#else
    if (omgezet[0] && omgezet[0] != '/') {
        char cwd[512], vol[1100];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(vol, sizeof(vol), "%s/%s", cwd, omgezet);
            if (strlen(vol) < sizeof(omgezet)) snprintf(omgezet, sizeof(omgezet), "%s", vol);
        }
    }
#endif
    snprintf(out->dir, sizeof(out->dir), "%s", omgezet);
    size_t dl = strlen(out->dir);
    if (dl && (out->dir[dl - 1] == '/' || out->dir[dl - 1] == '\\'))
        out->dir[dl - 1] = '\0';

    char path[700];
    snprintf(path, sizeof(path), "%s/dongle.json", out->dir);

    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof(path), "%s\\dongle.json", out->dir);
        f = fopen(path, "rb");
        if (!f) return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (1 << 20)) { fclose(f); return -1; }

    char *json = (char *)malloc((size_t)sz + 1);
    if (!json) { fclose(f); return -1; }
    size_t rd = fread(json, 1, (size_t)sz, f);
    json[rd] = '\0';
    fclose(f);

    /* De donglebeschrijving staat in de patchmap van de game
     * (patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json) en gebruikt daar het veld
     * "kind" (docs/dropins.md). "type" is de oudere naam en blijft werken. */
    char type[32] = {0};
    json_str(json, "kind", type, sizeof(type));
    if (!type[0]) json_str(json, "type", type, sizeof(type));
    json_str(json, "game", out->game, sizeof(out->game));

    if      (strcmp(type, "hasp-hl")   == 0) out->type = DONGLE_HASP_HL;
    else if (strcmp(type, "hasp")      == 0) out->type = DONGLE_HASP_HL;
    else if (strcmp(type, "region-pcb")== 0) out->type = DONGLE_REGION_PCB;
    else if (strcmp(type, "region")    == 0) out->type = DONGLE_REGION_PCB;
    else if (strcmp(type, "sega-scan") == 0) out->type = DONGLE_SEGA_SCAN;
    else                                     out->type = DONGLE_NONE;

    json_uint(json, "haspid", &out->haspid);
    read_features(json, out->features, DONGLE_MAX_FEATURES, &out->nfeatures);
    read_list(json, "keycaps", out->keycaps, DONGLE_MAX_KEYCAPS, &out->nkeycaps);
    json_array_objects(json, "memory", add_memory, out);
    json_array_objects(json, "stages", add_stage, out);

    json_str(json, "crypto_table", out->crypto_table, sizeof(out->crypto_table));

    json_naam(json, "region", out->region, sizeof(out->region));
    json_naam(json, "pcb", out->pcb, sizeof(out->pcb));

    const char *port = json_find(json, "port");
    if (port && *port == '{') {
        char *tmp = dup_str(port);
        if (tmp) {
            /* alleen het port-object: afkappen op de bijbehorende sluit-accolade */
            int depth = 0;
            for (char *p = tmp; *p; p++) {
                if (*p == '{') depth++;
                else if (*p == '}') { depth--; if (depth == 0) { p[1] = '\0'; break; } }
            }
            json_str(tmp, "type", out->port_type, sizeof(out->port_type));
            json_uint(tmp, "address", &out->port_address);
            free(tmp);
        }
    }

    free(json);
    return 0;
}

/* Vergelijkt twee namen zonder op hoofdletters te letten (en zonder streepjes of spaties
 * aan het eind mee te tellen). */
static int naam_gelijk(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (isspace(ca) || ca == '-' || ca == '_') { a++; continue; }
        if (isspace(cb) || cb == '-' || cb == '_') { b++; continue; }
        if (tolower(ca) != tolower(cb)) return 0;
        a++; b++;
    }
    while (*a && (isspace((unsigned char)*a) || *a == '-' || *a == '_')) a++;
    while (*b && (isspace((unsigned char)*b) || *b == '-' || *b == '_')) b++;
    return *a == '\0' && *b == '\0';
}

/*
 * Zoekt de dongle van een game. Eerst <root>/<game> (dat is wat het spel zelf als naam
 * gebruikt), en anders een lineaire scan van de onderliggende mappen: die wordt vergeleken
 * op het veld "game" in dongle.json en op de mapnaam. Daardoor werkt zowel "Halo Fireteam"
 * als "Halo" voor dezelfde dongle.
 */
/*
 * Probeert een gamemap in de vormen die in het wild voorkomen:
 *   <root>/<naam>                                  (losse dongle)
 *   <root>/<naam>/dropin/ravengun-hasp/<naam>      (de drop-in van de patch)
 *   <root>/<naam>/dropin/ravengun-hasp/<game>      (drop-in onder de spelnaam)
 * De drop-in-vorm is de gewone vorm: een dongle is een patch en hoort in de
 * patchmap van de game te staan.
 */
static int dongle_probe(const char *root, const char *naam, const char *game, dongle_t *out)
{
    char sub[900];
    snprintf(sub, sizeof(sub), "%s/%s", root, naam);
    if (dongle_load(sub, out) == 0) return 0;
    snprintf(sub, sizeof(sub), "%s/%s/dropin/ravengun-hasp/%s", root, naam, naam);
    if (dongle_load(sub, out) == 0) return 0;
    if (game && game[0] && strcmp(game, naam) != 0) {
        snprintf(sub, sizeof(sub), "%s/%s/dropin/ravengun-hasp/%s", root, naam, game);
        if (dongle_load(sub, out) == 0) return 0;
    }
    return -1;
}

int dongle_find(const char *root, const char *game, dongle_t *out)
{
    if (!root || !game || !out) return -1;
    if (dongle_probe(root, game, game, out) == 0) return 0;

#ifdef _WIN32
    /* Op Windows heeft opendir/readdir een andere vorm; de launcher geeft daar altijd de
     * juiste mapnaam door (scripts/hasp.py zoekt de naam voor ons op). */
    return -1;
#else
    {
        DIR *dp = opendir(root);
        if (!dp) return -1;
        struct dirent *de;
        int gevonden = -1;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.') continue;
            if (!naam_gelijk(de->d_name, game)) continue;
            if (dongle_probe(root, de->d_name, game, out) == 0) { gevonden = 0; break; }
        }
        closedir(dp);
        return gevonden;
    }
#endif
}

/* ============================================================ hulpmiddelen */

void dongle_fileid_name(unsigned int fileid, char out[33])
{
    for (int i = 0; i < 32; i++) {
        unsigned int bit = (fileid >> (31 - i)) & 1u;
        out[i] = bit ? '1' : '0';
    }
    out[32] = '\0';
}

int dongle_get_info_xml(const dongle_t *d, int extended, char *buf, size_t buflen)
{
    if (!d || !buf) return -1;
    size_t n = 0;
    int w;

    if (!extended) {
        w = snprintf(buf, buflen,
            "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
            "<hasp_info>\n  <hasp id=\"%u\" type=\"HASP-HL\">\n",
            d->haspid);
        if (w < 0 || (size_t)w >= buflen) return -1;
        n = (size_t)w;
        for (int i = 0; i < d->nfeatures; i++) {
            w = snprintf(buf + n, buflen - n, "    <feature id=\"%u\" />\n", d->features[i]);
            if (w < 0 || (size_t)w >= buflen - n) return -1;
            n += (size_t)w;
        }
        w = snprintf(buf + n, buflen - n, "  </hasp>\n</hasp_info>\n");
        if (w < 0 || (size_t)w >= buflen - n) return -1;
        return (int)(n + (size_t)w);
    }

    w = snprintf(buf, buflen,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
        "<hasp_info>\n  <keyspec>\n    <keycaps>\n");
    if (w < 0 || (size_t)w >= buflen) return -1;
    n = (size_t)w;
    for (int i = 0; i < d->nkeycaps; i++) {
        w = snprintf(buf + n, buflen - n, "      <%s />\n", d->keycaps[i]);
        if (w < 0 || (size_t)w >= buflen - n) return -1;
        n += (size_t)w;
    }
    w = snprintf(buf + n, buflen - n,
        "    </keycaps>\n    <hasp>\n      <haspid>%u</haspid>\n"
        "      <nethasptype>0</nethasptype>\n", d->haspid);
    if (w < 0 || (size_t)w >= buflen - n) return -1;
    n += (size_t)w;
    for (int i = 0; i < d->nmemory; i++) {
        w = snprintf(buf + n, buflen - n,
            "      <memoryinfo>\n        <name>%s</name>\n"
            "        <fileid>%u</fileid>\n        <size>%u</size>\n      </memoryinfo>\n",
            d->memory[i].name, d->memory[i].fileid, d->memory[i].size);
        if (w < 0 || (size_t)w >= buflen - n) return -1;
        n += (size_t)w;
    }
    w = snprintf(buf + n, buflen - n,
        "    </hasp>\n    <port><type>%s</type><address>%u</address></port>\n"
        "  </keyspec>\n</hasp_info>\n",
        d->port_type[0] ? d->port_type : "USB", d->port_address);
    if (w < 0 || (size_t)w >= buflen - n) return -1;
    return (int)(n + (size_t)w);
}

unsigned int dongle_memory_size(const dongle_t *d, unsigned int fileid)
{
    if (!d) return 0;
    for (int i = 0; i < d->nmemory; i++)
        if (d->memory[i].fileid == fileid) return d->memory[i].size;
    return 0;
}

/* Zoekt het geheugenbestand op. Eerst volgens de beschrijving, en als dat niet lukt
 * rechtstreeks op de binaire fileid-naam in memory/ — dat is de regel die de API zelf
 * gebruikt, dus die werkt ook zonder beschrijving. */
static const dongle_memory_t *find_memory(const dongle_t *d, unsigned int fileid)
{
    for (int i = 0; i < d->nmemory; i++)
        if (d->memory[i].fileid == fileid) return &d->memory[i];
    return NULL;
}

static FILE *open_memory(const dongle_t *d, unsigned int fileid, const char *mode)
{
    char name[33];
    dongle_fileid_name(fileid, name);
    char path[900];

    const dongle_memory_t *m = find_memory(d, fileid);
    if (m && m->file[0]) {
        snprintf(path, sizeof(path), "%s/%s", d->dir, m->file);
        FILE *f = fopen(path, mode);
        if (f) return f;
    }
    snprintf(path, sizeof(path), "%s/memory/%s", d->dir, name);
    FILE *f = fopen(path, mode);
    if (f) return f;
    snprintf(path, sizeof(path), "%s\\memory\\%s", d->dir, name);
    return fopen(path, mode);
}

int dongle_read_at(const dongle_t *d, unsigned int fileid, unsigned int offset, void *buf, size_t len)
{
    if (!d || !buf) return -1;
    FILE *f = open_memory(d, fileid, "rb");
    if (!f) return -1;
    if (offset && fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, len, f);
    fclose(f);
    return (int)rd;
}

int dongle_read(const dongle_t *d, unsigned int fileid, void *buf, size_t len)
{
    return dongle_read_at(d, fileid, 0, buf, len);
}

int dongle_write_at(const dongle_t *d, unsigned int fileid, unsigned int offset, const void *buf, size_t len)
{
    if (!d || !buf) return -1;
    FILE *f = open_memory(d, fileid, "r+b");
    if (!f) f = open_memory(d, fileid, "wb");
    if (!f) return -1;
    if (offset && fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return -1; }
    size_t wr = fwrite(buf, 1, len, f);
    fclose(f);
    return (wr == len) ? 0 : -1;
}

int dongle_write(const dongle_t *d, unsigned int fileid, const void *buf, size_t len)
{
    return dongle_write_at(d, fileid, 0, buf, len);
}

/* ============================================================ HASP-API */

static dongle_t g_active;
static int      g_installed = 0;

int dongle_install(const dongle_t *d)
{
    if (!d) return -1;
    g_active = *d;
    g_installed = 1;
    return 0;
}

void dongle_uninstall(void)
{
    g_installed = 0;
    memset(&g_active, 0, sizeof(g_active));
}

const dongle_t *dongle_active(void)
{
    return g_installed ? &g_active : NULL;
}

const char *dongle_env_root(void)
{
    const char *v = getenv("RAVENGUN_HASP_DIR");
    return (v && *v) ? v : "/hasp";
}

const char *dongle_env_game(void)
{
    const char *v = getenv("RAVENGUN_HASP_GAME");
    return (v && *v) ? v : "";
}

int dongle_install_from_env(void)
{
    const char *root = dongle_env_root();
    const char *game = dongle_env_game();
    dongle_t d;

    if (!game[0]) {
        if (hasp_verbose())
            hasp_log("[ravengun-hasp] RAVENGUN_HASP_GAME is niet gezet; geen dongle geladen\n");
        return -1;
    }
    if (dongle_find(root, game, &d) != 0) {
        if (hasp_verbose())
            hasp_log("[ravengun-hasp] geen dongle voor '%s' in %s\n", game, root);
        return -1;
    }
    if (dongle_install(&d) != 0) {
        if (hasp_verbose())
            hasp_log("[ravengun-hasp] dongle '%s' gevonden maar niet te installeren\n", game);
        return -1;
    }
    if (hasp_verbose())
        hasp_log("[ravengun-hasp] dongle '%s' geladen uit %s (%d geheugenbestand(en), haspid %u)\n",
                d.game[0] ? d.game : game, d.dir, d.nmemory, d.haspid);
    return 0;
}

/* De greep is een pointerwaardige waarde: op 64-bits Windows is de handle 8 bytes en mag
 * er niet maar 4 bytes in geschreven worden. */
static void zet_handle(void **handle)
{
    /* hasp_handle_t is in de Sentinel-API een 32-bits unsigned int, ook op 64-bits systemen. Een pointer (8 bytes) schrijven
       overschreef op x86_64 de 4 bytes naast de greep op de stapel van het spel (Halo: Fireteam Raven, 2026-10-15: het spel
       stopte direct na hasp_login). Op i386 viel dat samen, daarom was het bij de 32-bits games nooit te zien. */
    if (handle) *(unsigned int *)handle = DONGLE_HANDLE_VALUE;
}

int hasp_login(unsigned int vendor_code, const char *scope, void **handle)
{
    char detail[160];
    (void)scope;
    snprintf(detail, sizeof(detail), "(vendor_code=%u)", vendor_code);
    hasp_trace("hasp_login", detail);
    if (!g_installed) return HASP_STATUS_NOT_FOUND;
    zet_handle(handle);
    return HASP_STATUS_OK;
}

int hasp_login_scope(unsigned int vendor_code, const char *scope, const char *format, void **handle)
{
    (void)format;
    hasp_trace("hasp_login_scope", NULL);
    return hasp_login(vendor_code, scope, handle);
}

int hasp_login_ex(unsigned int vendor_code, const char *scope, const char *format, void **handle)
{
    return hasp_login_scope(vendor_code, scope, format, handle);
}

int hasp_login_port(unsigned int vendor_code, const char *port, const char *scope, void **handle)
{
    (void)port;
    hasp_trace("hasp_login_port", NULL);
    return hasp_login(vendor_code, scope, handle);
}

/*
 * Diagnostics for reading a game's own state without a debugger (Lost Land, 2026-10-21). On every
 * hasp_logout, and only when the variable is set:
 *   RAVENGUN_HASP_PEEK=<rva>:<len>       (hex rva, decimal len <= 64) the bytes at that RVA of the
 *                                        process's main module, such as a global dongle verdict;
 *   RAVENGUN_HASP_PEEK_MAP=<name>:<len>  (decimal len <= 4096) the first bytes of a named file
 *                                        mapping the game shares with another process.
 */
#ifdef _WIN32
static void hasp_hexdump(const char *what, const unsigned char *p, unsigned int len)
{
    char hex[32 * 3 + 1];
    for (unsigned int off = 0; off < len; off += 32) {
        unsigned int n = (len - off < 32) ? len - off : 32;
        for (unsigned int i = 0; i < n; i++) snprintf(hex + 3 * i, 4, "%02x ", p[off + i]);
        hasp_log("[ravengun-hasp] peek %s +0x%03x: %s\n", what, off, hex);
    }
}
#endif

static void hasp_peek(void)
{
#ifdef _WIN32
    const char *e = getenv("RAVENGUN_HASP_PEEK");
    if (e && *e) {
        char *end = NULL, what[48];
        unsigned long long rva = strtoull(e, &end, 16);
        unsigned int len = (end && *end == ':') ? (unsigned int)strtoul(end + 1, NULL, 10) : 16;
        if (len == 0 || len > 64) len = 16;
        const unsigned char *base = (const unsigned char *)GetModuleHandleA(NULL);
        snprintf(what, sizeof(what), "rva 0x%llx", rva);
        if (base) hasp_hexdump(what, base + rva, len);
    }
    const char *m = getenv("RAVENGUN_HASP_PEEK_MAP");
    if (m && *m) {
        char name[128];
        snprintf(name, sizeof(name), "%s", m);
        char *colon = strrchr(name, ':');
        unsigned int len = 256;
        if (colon) { *colon = '\0'; len = (unsigned int)strtoul(colon + 1, NULL, 10); }
        if (len == 0 || len > 4096) len = 256;
        HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, name);
        if (!h) { hasp_log("[ravengun-hasp] peek map %s: OpenFileMapping failed (%lu)\n", name, GetLastError()); return; }
        const unsigned char *v = (const unsigned char *)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
        if (v) {
            MEMORY_BASIC_INFORMATION mi;
            if (VirtualQuery(v, &mi, sizeof(mi)) && mi.RegionSize < len) len = (unsigned int)mi.RegionSize;
            hasp_hexdump(name, v, len);
            UnmapViewOfFile(v);
        }
        CloseHandle(h);
    }
#endif
}

int hasp_logout(void *handle)
{
    hasp_peek();
    hasp_trace("hasp_logout", NULL);
    (void)handle;
    return HASP_STATUS_OK;
}

int hasp_get_info(void *handle, const char *format, const char *scope, char **info)
{
    (void)handle; (void)format; (void)scope;
    hasp_trace("hasp_get_info", NULL);
    if (!g_installed) return HASP_STATUS_NOT_FOUND;
    if (!info) return HASP_STATUS_INVALID_PARAMETER;

    char buf[8192];
    int n = dongle_get_info_xml(&g_active, 0, buf, sizeof(buf));
    if (n < 0) return HASP_STATUS_INSUFFICIENT_BUFFER;

    char *out = (char *)malloc((size_t)n + 1);
    if (!out) return HASP_STATUS_INSUFFICIENT_BUFFER;
    memcpy(out, buf, (size_t)n + 1);
    *info = out;
    return HASP_STATUS_OK;
}

int hasp_get_sessioninfo(void *handle, const char *format, char **info)
{
    (void)handle; (void)format;
    hasp_trace("hasp_get_sessioninfo", NULL);
    if (!g_installed) return HASP_STATUS_NOT_FOUND;
    if (!info) return HASP_STATUS_INVALID_PARAMETER;

    char buf[8192];
    int n = dongle_get_info_xml(&g_active, 1, buf, sizeof(buf));
    if (n < 0) return HASP_STATUS_INSUFFICIENT_BUFFER;

    char *out = (char *)malloc((size_t)n + 1);
    if (!out) return HASP_STATUS_INSUFFICIENT_BUFFER;
    memcpy(out, buf, (size_t)n + 1);
    *info = out;
    return HASP_STATUS_OK;
}

int hasp_get_size(void *handle, unsigned int fileid, unsigned int *size)
{
    char detail[80];
    snprintf(detail, sizeof(detail), "(fileid=%u)", fileid);
    hasp_trace("hasp_get_size", detail);
    (void)handle;
    /* De grootte komt uit de beschrijving. De bestaande ondersteuning gaf hier altijd 0;
     * een echte dongle geeft de werkelijke grootte, en een spel dat op 0 controleert zou
     * onze laag dan niet herkennen. Onbekende fileid blijft 0. */
    if (size) *size = g_installed ? dongle_memory_size(&g_active, fileid) : 0;
    return HASP_STATUS_OK;
}

int hasp_read(void *handle, unsigned int fileid, unsigned int offset, unsigned int length, void *buffer)
{
    char detail[160];
    int rd;
    snprintf(detail, sizeof(detail), "(fileid=%u offset=%u lengte=%u)", fileid, offset, length);
    hasp_trace("hasp_read", detail);

    if (!g_installed)           return HASP_STATUS_NOT_FOUND;
    if (!handle || !buffer)     return HASP_STATUS_INVALID_PARAMETER;   /* 0x1F5, geverifieerd */
    if (length == 0)            return HASP_STATUS_OK;

    rd = dongle_read_at(&g_active, fileid, offset, buffer, length);
    if (rd < 0)                 return HASP_STATUS_NOT_FOUND;
    return HASP_STATUS_OK;
}

int hasp_write(void *handle, unsigned int fileid, unsigned int offset, unsigned int length, const void *buffer)
{
    char detail[160];
    snprintf(detail, sizeof(detail), "(fileid=%u offset=%u lengte=%u)", fileid, offset, length);
    hasp_trace("hasp_write", detail);

    if (!g_installed)           return HASP_STATUS_NOT_FOUND;
    if (!handle || !buffer)     return HASP_STATUS_INVALID_PARAMETER;
    if (length == 0)            return HASP_STATUS_OK;
    if (dongle_write_at(&g_active, fileid, offset, buffer, length) != 0)
        return HASP_STATUS_IO_ERROR;
    return HASP_STATUS_OK;
}

/*
 * OPT-IN antwoordtafel voor encrypt/decrypt; zie dongle.h. Zonder tafel is deze functie een
 * no-op, zodat games die haar niet gebruiken (Halo, Jurassic Park) ongewijzigd blijven.
 */
static void hex_naam(const unsigned char *b, size_t n, char *uit)
{
    static const char H[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) { uit[2 * i] = H[b[i] >> 4]; uit[2 * i + 1] = H[b[i] & 15]; }
    uit[2 * n] = '\0';
}

int dongle_crypto_replay(const dongle_t *d, void *buffer, size_t length, int *gebruikt,
                         char *naam_uit, size_t naam_sz)
{
    static const size_t vormen[2] = { 32, 16 };   /* eerst de lange naam, dan de korte */
    const char *env = getenv("RAVENGUN_HASP_CRYPTO_TABLE");
    const char *bron = (env && *env) ? env : (d ? d->crypto_table : "");
    char dir[600], naam[65], pad[700];
    int i;

    if (gebruikt) *gebruikt = 0;
    if (naam_uit && naam_sz) naam_uit[0] = '\0';
    if (!buffer || !bron || !bron[0]) return 0;
    if (gebruikt) *gebruikt = 1;
    dongle_path(dir, sizeof(dir), bron);
    for (i = 0; i < 2; i++) {
        size_t k = vormen[i];
        if (length < k) continue;
        hex_naam((const unsigned char *)buffer, k, naam);
        /* De laatst geprobeerde naam onthouden voor het spoor: op een treffer is dat de
         * naam die trof, op een mis de korte (16-byte) naam -- precies de naam die in de
         * tafel gezocht moet worden. */
        if (naam_uit && naam_sz) { snprintf(naam_uit, naam_sz, "%s", naam); }
        snprintf(pad, sizeof(pad), "%s/%s", dir, naam);
        FILE *f = fopen(pad, "rb");
        if (!f) continue;
        {
            unsigned char *tmp = (unsigned char *)malloc(length);
            size_t rd = tmp ? fread(tmp, 1, length, f) : 0;
            fclose(f);
            if (tmp && rd > 0) { memcpy(buffer, tmp, rd); free(tmp); return 1; }
            free(tmp);
        }
    }
    return 0;
}

/*
 * Encrypt en decrypt werken in de buffer zelf (greep, buffer, lengte), niet met een aparte
 * uitvoerbuffer. Standaard laten ze de buffer ongemoeid: de dongle-image wordt niet
 * ontsleuteld maar één-op-één doorgegeven, precies zoals de bestaande ondersteuning doet.
 * Staat er een antwoordtafel ingesteld (dongle_crypto_replay), dan komt het antwoord daaruit.
 */
int hasp_encrypt(void *handle, void *buffer, unsigned int length)
{
    char detail[160], naam[65] = "";
    int gebruikt = 0, hit = dongle_crypto_replay(g_installed ? &g_active : NULL, buffer, length,
                                                 &gebruikt, naam, sizeof(naam));
    (void)handle;
    snprintf(detail, sizeof(detail), "(lengte=%u%s%s%s)", length,
             gebruikt ? (hit ? " tafel=treffer" : " tafel=MIS") : "",
             (gebruikt && naam[0]) ? " sleutel=" : "", (gebruikt && naam[0]) ? naam : "");
    hasp_trace("hasp_encrypt", detail);
    return HASP_STATUS_OK;
}

int hasp_decrypt(void *handle, void *buffer, unsigned int length)
{
    char detail[160], naam[65] = "";
    int gebruikt = 0, hit = dongle_crypto_replay(g_installed ? &g_active : NULL, buffer, length,
                                                 &gebruikt, naam, sizeof(naam));
    (void)handle;
    snprintf(detail, sizeof(detail), "(lengte=%u%s%s%s)", length,
             gebruikt ? (hit ? " tafel=treffer" : " tafel=MIS") : "",
             (gebruikt && naam[0]) ? " sleutel=" : "", (gebruikt && naam[0]) ? naam : "");
    hasp_trace("hasp_decrypt", detail);
    return HASP_STATUS_OK;
}

int hasp_free(void *p)
{
    hasp_trace("hasp_free", NULL);
    free(p);
    return HASP_STATUS_OK;
}

int hasp_get_version(void *handle, unsigned int *version)
{
    hasp_trace("hasp_get_version", NULL);
    (void)handle;
    if (version) *version = DONGLE_HASP_VERSION;
    return HASP_STATUS_OK;
}

int hasp_detach(void *handle)
{
    hasp_trace("hasp_detach", NULL);
    (void)handle;
    return HASP_STATUS_OK;
}

/* De rest van de namen uit de runtime: ze doen niets en melden succes. Ze zijn nooit
 * aangeroepen in de gemeten games; ze staan er zodat de bibliotheek een volledige
 * vervanging is (een ontbrekende naam laat het laden van het spel mislukken). */
int hasp_update(void *handle, const char *scope) { (void)handle; (void)scope; return HASP_STATUS_OK; }
int hasp_get_rtc(void *handle, void *time) { (void)handle; (void)time; return HASP_STATUS_OK; }
int hasp_get_trace(void *handle, char **trace) { (void)handle; if (trace) *trace = NULL; return HASP_STATUS_OK; }
int hasp_enable_trace(void *handle, unsigned int enable) { (void)handle; (void)enable; return HASP_STATUS_OK; }
int hasp_legacy_encrypt(void *handle, void *buffer, unsigned int length) { return hasp_encrypt(handle, buffer, length); }
int hasp_legacy_decrypt(void *handle, void *buffer, unsigned int length) { return hasp_decrypt(handle, buffer, length); }
int hasp_legacy_set_idletime(void *handle, unsigned int seconds) { (void)handle; (void)seconds; return HASP_STATUS_OK; }
int hasp_legacy_set_rtc(void *handle, const void *time) { (void)handle; (void)time; return HASP_STATUS_OK; }
int hasp_datetime_to_hasptime(const void *datetime, void *hasptime)
{
    if (datetime && hasptime && datetime != hasptime) memcpy(hasptime, datetime, sizeof(unsigned int));
    return HASP_STATUS_OK;
}
int hasp_hasptime_to_datetime(const void *hasptime, void *datetime)
{
    if (hasptime && datetime && hasptime != datetime) memcpy(datetime, hasptime, sizeof(unsigned int));
    return HASP_STATUS_OK;
}

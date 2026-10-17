/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_macaw.c - rg-macaw.exe, onze eigen lader voor Konami's AVS2-games (Silent Scope: Bone Eater, arkndd.dll, 64 bit).
 * Vervangt TeknoMacaw64.exe uit de TeknoParrot-map. Gebruik: rg-macaw.exe <pad naar modules\arkndd.dll>
 *
 * Wat TeknoMacaw64 deed (uit zijn eigen logboek tp_log.txt en uit de meldingen op stdout; de exe zelf is gepakt):
 *   werkmap = de map boven modules\, DLL-zoekpad = modules\; AVS booten; prop\ea3-config.xml lezen, eigen waarden zetten,
 *   het spel laden ("Loaded game dll"); "EA3 boot successful"; daarna draait het spel. Daarnaast een Patreon-abonnements-
 *   controle ("[SUBSCRIPTION MODE]") en eigen libacio-haken. Beide zitten hier niet in: libacio.dll is de vervanger
 *   (rg_libacio.c), een abonnement bestaat niet.
 *
 * De volgorde is die van de gepubliceerde spice2x-lader en van bemanitools: avs_boot -> app-config -> dll_entry_init
 * -> ea3_boot -> dll_entry_main. De AVS-functies worden bij NAAM uit libavs-win64.dll gehaald (de namen zijn index-
 * namen, rgm_avs_216 in rg_macaw_logic.h) en elke stap logt zijn uitkomst in rg-macaw.log; een naam die niet bestaat
 * of een stap die faalt stopt de lader met een duidelijke regel in plaats van door te gaan.
 *
 * Wat NIET bewezen is (er is nog niets gestart; zie docs/research/losse/SilentScopeBoneEater.md): de handtekeningen van
 * avs_boot / property_* / ea3_boot zijn die uit de spice2x-bron; het logboek laat bij de eerste start zien welke stap
 * afwijkt.
 *
 * Aanpassingen aan de configuratie gebeuren in een tijdelijke kopie (rg-macaw\*.xml in de werkmap), nooit op het
 * bestand zelf: de games staan op de NAS.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "rg_macaw_logic.h"

static FILE *g_log;

static void lg(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void die(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    lg("FOUT: %s", msg);
    fprintf(stderr, "rg-macaw: %s\n", msg);
    exit(3);
}

/* ---- de AVS-functies ------------------------------------------------------------------------------------------------- */

typedef void *(*fn_property_create)(int flags, void *buf, uint32_t size);
typedef int (*fn_reader)(uint32_t context, void *bytes, size_t nbytes);
typedef int (*fn_property_read_query_memsize)(fn_reader reader, uint32_t context, uint32_t *a, uint32_t *b);
typedef int (*fn_property_insert_read)(void *prop, void *node, fn_reader reader, uint32_t context);
typedef void *(*fn_property_search)(void *prop, void *node, const char *path);
typedef intptr_t (*fn_fs_open)(const char *path, int mode, int flags);
typedef int (*fn_fs_close)(intptr_t file);
typedef int (*fn_boot)(void *config_node, void *heap, size_t heap_size, void *avs_heap, void *log_cb, HANDLE log_file);
typedef int (*fn_ea3_boot)(void *ea3_node);
typedef int (*fn_entry_init)(const char *sid, void *app_params);
typedef int (*fn_entry_main)(void);

static struct {
    HMODULE core, ea3;
    fn_property_create property_create;
    fn_property_read_query_memsize query_memsize;
    fn_property_insert_read insert_read;
    fn_property_search search;
    fn_fs_open fs_open;
    fn_fs_close fs_close;
    void *fs_read;
    fn_boot boot;
    fn_ea3_boot ea3_boot;
} A;

static FARPROC need(HMODULE m, const char *name, const char *what)
{
    FARPROC p = GetProcAddress(m, name);
    if (!p) die("%s ontbreekt: export %s niet in de AVS-bibliotheek (versie die we niet kennen?)", what, name);
    lg("ok: %s = %s", what, name);
    return p;
}

#define PROPERTY_FLAGS_RWCA 0xF   /* lezen|schrijven|aanmaken|toevoegen (bemanitools property.h) */

/* De lezer voor property_read_query_memsize/property_insert_read: (context, buffer, aantal) -> gelezen bytes. Wij lezen uit een
 * buffer in het geheugen, zoals bemanitools (boot_property_fread) met stdio leest: avs_fs_read kan voor avs_boot nog niets lezen
 * (gemeten 2026-10-17: property_read_query_memsize met avs_fs_read gaf 0x80092182 op een geldig bestand). */
static struct { const char *buf; size_t len, pos; } g_mem[4];

static int mem_read(uint32_t ctx, void *bytes, size_t n)
{
    size_t left;
    if (ctx >= sizeof g_mem / sizeof g_mem[0] || !g_mem[ctx].buf) return -1;
    left = g_mem[ctx].len - g_mem[ctx].pos;
    if (n > left) n = left;
    memcpy(bytes, g_mem[ctx].buf + g_mem[ctx].pos, n);
    g_mem[ctx].pos += n;
    return (int)n;
}

static char *slurp(const char *path, size_t *len, size_t extra);

/* Een XML-bestand als AVS-property: grootte opvragen, de property maken en inlezen, telkens met mem_read als lezer. */
static void *read_property(const char *path, int bufextra)
{
    static uint32_t next;
    uint32_t slot = next++ % (uint32_t)(sizeof g_mem / sizeof g_mem[0]);
    int size;
    uint32_t q1 = 0, q2 = 0;
    void *buf, *prop;
    size_t len = 0;
    char *xml = slurp(path, &len, 0);
    if (!xml) die("kan %s niet lezen", path);
    g_mem[slot].buf = xml; g_mem[slot].len = len; g_mem[slot].pos = 0;
    size = A.query_memsize(mem_read, slot, &q1, &q2);
    if (size <= 0) die("property_read_query_memsize(%s) = %d (0x%08x)", path, size, (unsigned)size);
    size += bufextra;
    buf = calloc(1, (size_t)size);
    prop = A.property_create(PROPERTY_FLAGS_RWCA, buf, (uint32_t)size);
    if (!prop) die("property_create(%s) faalde", path);
    g_mem[slot].pos = 0;
    if (A.insert_read(prop, NULL, mem_read, slot) < 0) die("property_insert_read(%s) faalde", path);
    g_mem[slot].buf = NULL;
    free(xml);
    lg("property gelezen: %s (%d bytes)", path, size);
    return prop;
}

/* ---- bestanden ------------------------------------------------------------------------------------------------------- */

static char *slurp(const char *path, size_t *len, size_t extra)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = calloc(1, (size_t)n + extra + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (size_t)n;
    return b;
}

static int spit(const char *path, const char *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(data, 1, n, f);
    fclose(f);
    return 1;
}

/* Leest src, past aan (zie hieronder) en schrijft het naar rg-macaw\<naam>; geeft het pad van de kopie. De kopie wordt
 * gelezen door AVS, dat de padnaam met schuine strepen wil. */
static const char *patched_copy(const char *src, const char *name, int kind, char *dst, size_t dstcap)
{
    size_t len = 0, cap;
    char *buf = slurp(src, &len, 4096);
    if (!buf) die("kan %s niet lezen", src);
    cap = len + 4096;
    if (kind == 1) {          /* avs-config: geen NTP (sntp/ea_on = 0) zodat het opstarten niet op het netwerk wacht */
        if (rgm_text_replace(buf, len, cap, "<ea_on __type=\"bool\">1</ea_on>", "<ea_on __type=\"bool\">0</ea_on>") != (size_t)-1)
            lg("%s: sntp/ea_on = 0", name);
        /* log/level staat er als __type="string"; dat type kent AVS niet ("str"), het inlezen gaf 0x80092182 (2026-10-17) */
        len = strlen(buf);
        if (rgm_text_replace(buf, len, cap, "__type=\"string\"", "__type=\"str\"") != (size_t)-1)
            lg("%s: __type=\"string\" -> \"str\"", name);
    } else if (kind == 2) {   /* ark-config: geen USB-sleutel nodig (/config/nousbkey, "niet 0 = geen sleutel") */
        size_t n = rgm_text_replace(buf, len, cap, "<nousbkey __type=\"s32\">0</nousbkey>", "<nousbkey __type=\"s32\">1</nousbkey>");
        if (n != (size_t)-1) { lg("%s: nousbkey = 1", name); }
    } else if (kind == 3) {   /* ea3-config: softid ontbreekt in de dump; zet de standaardwaarde van spice2x/bemanitools */
        if (!strstr(buf, "<softid")) {
            size_t n = rgm_text_insert_before(buf, len, cap, "</id>", "<softid __type=\"str\">012199999999</softid>");
            if (n != (size_t)-1) lg("%s: softid toegevoegd", name);
        }
    }
    CreateDirectoryA("rg-macaw", NULL);
    snprintf(dst, dstcap, "rg-macaw/%s", name);
    if (!spit(dst, buf, strlen(buf))) die("kan %s niet schrijven", dst);
    free(buf);
    return dst;
}

static int getenv_int(const char *name, int def)
{
    char b[32];
    DWORD n = GetEnvironmentVariableA(name, b, sizeof b);
    return (n > 0 && n < sizeof b) ? atoi(b) : def;
}

/* De bestandsversie van een DLL ("2.16.3,r7106"), via de versiebron; leeg als die er niet is. */
static int file_version(const char *path, char *out, size_t cap)
{
    DWORD h = 0, sz = GetFileVersionInfoSizeA(path, &h);
    void *blob;
    struct { WORD lang, cp; } *tr;
    UINT l = 0;
    char key[64];
    char *val = NULL;
    if (!sz) return 0;
    blob = malloc(sz);
    if (!GetFileVersionInfoA(path, 0, sz, blob)) { free(blob); return 0; }
    if (!VerQueryValueA(blob, "\\VarFileInfo\\Translation", (void **)&tr, &l) || l < 4) { free(blob); return 0; }
    snprintf(key, sizeof key, "\\StringFileInfo\\%04x%04x\\FileVersion", tr->lang, tr->cp);
    if (!VerQueryValueA(blob, key, (void **)&val, &l) || !val) { free(blob); return 0; }
    snprintf(out, cap, "%s", val);
    free(blob);
    return 1;
}

int main(int argc, char **argv)
{
    char root[MAX_PATH], modules[MAX_PATH], path[MAX_PATH], ver[64] = "", sid[96], tmp[MAX_PATH];
    const struct rgm_avs_syms *S;
    char *ea3xml;
    size_t n = 0;
    void *cfg, *cfgnode, *ea3prop, *ea3node, *appprop, *appnode;
    HMODULE game;
    fn_entry_init init;
    fn_entry_main main_;
    void *heap;
    size_t heap_sz = (size_t)getenv_int("RG_MACAW_HEAP_MB", 64) << 20;

    if (argc < 2) { fprintf(stderr, "gebruik: rg-macaw.exe <pad naar modules\\arkndd.dll>\n"); return 2; }
    if (!rgm_module_root(argv[1], root, sizeof root)) die("kan de spelmap niet afleiden uit %s", argv[1]);
    if (!SetCurrentDirectoryA(root)) die("SetCurrentDirectory(%s) faalde", root);
    snprintf(path, sizeof path, "%s/rg-macaw.log", ".");
    g_log = fopen(path, "w");
    lg("rg-macaw start; spelmap %s; module %s", root, argv[1]);
    snprintf(modules, sizeof modules, "%s\\modules", root);
    SetDllDirectoryA(modules);
    lg("DLL-zoekpad: %s", modules);

    /* 1. AVS laden en de naamtabel kiezen */
    snprintf(path, sizeof path, "%s\\libavs-win64.dll", modules);
    if (!file_version(path, ver, sizeof ver)) die("geen versie in %s", path);
    lg("libavs-win64.dll versie %s", ver);
    S = rgm_avs_syms_for(ver);
    if (!S) die("AVS-versie %s wordt niet ondersteund (alleen 2.16.3 t/m 2.16.8)", ver);
    A.core = LoadLibraryA(path);
    if (!A.core) die("LoadLibrary(%s) faalde (%lu)", path, GetLastError());
    A.property_create = (fn_property_create)need(A.core, S->property_create, "property_create");
    A.query_memsize = (fn_property_read_query_memsize)need(A.core, S->property_read_query_memsize, "property_read_query_memsize");
    A.insert_read = (fn_property_insert_read)need(A.core, S->property_insert_read, "property_insert_read");
    A.search = (fn_property_search)need(A.core, S->property_search, "property_search");
    A.fs_open = (fn_fs_open)need(A.core, S->fs_open, "avs_fs_open");
    A.fs_read = (void *)need(A.core, S->fs_read, "avs_fs_read");
    A.fs_close = (fn_fs_close)need(A.core, S->fs_close, "avs_fs_close");
    A.boot = (fn_boot)need(A.core, S->boot, "avs_boot");

    /* 2. de mappen die avs-config.xml verwacht (conf\nvram, conf\raw) */
    CreateDirectoryA("conf", NULL);
    CreateDirectoryA("conf\\nvram", NULL);
    CreateDirectoryA("conf\\raw", NULL);

    /* 3. AVS booten */
    patched_copy("prop/avs-config.xml", "avs-config.xml", 1, tmp, sizeof tmp);
    cfg = read_property(tmp, 0x2000);
    cfgnode = A.search(cfg, NULL, "/config");
    if (!cfgnode) die("geen /config in avs-config.xml");
    heap = calloc(1, heap_sz);
    if (!heap) die("geen geheugen voor de AVS-heap (%zu bytes)", heap_sz);
    lg("avs_boot (heap %zu MB)", heap_sz >> 20);
    if (A.boot(cfgnode, heap, heap_sz, NULL, NULL, NULL) < 0) die("avs_boot gaf een fout");
    lg("AVS gebooted");
    Sleep(100);

    /* 4. ea3- en app-configuratie */
    snprintf(path, sizeof path, "%s\\libavs-win64-ea3.dll", modules);
    A.ea3 = LoadLibraryA(path);
    if (!A.ea3) die("LoadLibrary(%s) faalde (%lu)", path, GetLastError());
    A.ea3_boot = (fn_ea3_boot)need(A.ea3, S->ea3_boot, "ea3_boot");
    ea3xml = slurp("prop/ea3-config.xml", &n, 0);
    if (!ea3xml) die("kan prop/ea3-config.xml niet lezen");
    if (rgm_soft_code(ea3xml, sid, sizeof sid) < 0) die("ea3-config.xml mist model/dest/spec/rev/ext");
    lg("softwarecode: %s", sid);
    patched_copy("prop/ea3-config.xml", "ea3-config.xml", 3, tmp, sizeof tmp);
    ea3prop = read_property(tmp, 0x4000);
    ea3node = A.search(ea3prop, NULL, "/ea3");
    if (!ea3node) die("geen /ea3 in ea3-config.xml");
    patched_copy("prop/ark-config.xml", "ark-config.xml", 2, tmp, sizeof tmp);
    appprop = read_property(tmp, 0x4000);
    appnode = A.search(appprop, NULL, "/config");
    if (!appnode) die("geen /config in ark-config.xml");

    /* 5. het spel laden en starten */
    game = LoadLibraryA(argv[1]);
    if (!game) die("LoadLibrary(%s) faalde (%lu)", argv[1], GetLastError());
    lg("spel geladen: %s", argv[1]);
    init = (fn_entry_init)GetProcAddress(game, "dll_entry_init");
    main_ = (fn_entry_main)GetProcAddress(game, "dll_entry_main");
    if (!init || !main_) die("dll_entry_init/dll_entry_main ontbreken in %s", argv[1]);
    lg("dll_entry_init(%s)", sid);
    if (!init(sid, appnode)) die("dll_entry_init gaf false");
    lg("ea3_boot");
    if (A.ea3_boot(ea3node) < 0) die("ea3_boot gaf een fout");
    lg("EA3 gebooted; dll_entry_main");
    {
        int r = main_();
        lg("dll_entry_main gaf %d", r);
    }
    return 0;
}

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
 * Diagnose (2026-10-17, ea3_boot liet het proces stil verdwijnen): AVS logt standaard NIET (de standaard-schrijver in
 * libavs-win64.dll is een kale "ret", 0x18009ed40) en log_fatal beeindigt het proces zonder iets te tonen. Daarom geeft de lader
 * avs_boot een eigen log-schrijver mee (de AVS-meldingen komen als "AVS: ..." in rg-macaw.log), leidt hij stdout/stderr van het
 * spel naar rg-macaw-stdio.log, en meldt hij elke ExitProcess/RtlExitUserProcess/TerminateProcess met de aanroepketen (module +
 * offset) en elke ernstige uitzondering. Zo zegt het logboek zelf wie het proces stopt.
 *
 * Wat NIET bewezen is (zie docs/research/losse/SilentScopeBoneEater.md): de handtekeningen van property_* en ea3_boot zijn die
 * uit de spice2x-bron; avs_boot is wel gemeten (disassemblage van 0x180030f90: 5e argument = log-schrijver, 6e = zijn context).
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
static CRITICAL_SECTION g_lock;       /* AVS logt vanuit eigen threads; een regel mag niet door een andere onderbroken worden */

static void lgv(const char *fmt, va_list ap)
{
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

static void lg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    lgv(fmt, ap);
    va_end(ap);
}

/* avs_log_writer_t (bemanitools, spice2x): void (*)(const char *chars, uint32_t nchars, void *ctx). Op x64 is er een aanroepconventie
 * (Microsoft); MinGW gebruikt die standaard, dus een gewone C-functie volstaat. De tekst is een stuk logregel(s) van AVS, niet
 * per se met een eindnul en met of zonder "\n". Wij knippen op regels en zetten er "AVS: " voor. */
static volatile LONG g_avs_lines;

static void avs_log_writer(const char *chars, uint32_t nchars, void *ctx)
{
    char line[1024];
    size_t n, i, o = 0;
    (void)ctx;
    if (!chars) return;
    n = nchars;
    if (n == 0 || n > 8192) n = 8192;          /* geen geloofwaardige lengte: val terug op de eindnul */
    n = strnlen(chars, n);
    for (i = 0; i <= n; i++) {
        char c = i < n ? chars[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            if (o) { line[o] = 0; lg("AVS: %s", line); InterlockedIncrement(&g_avs_lines); }
            o = 0;
        } else if (o + 1 < sizeof line) {
            line[o++] = c;
        }
    }
}

/* ---- wie stopt het proces? ------------------------------------------------------------------------------------------ */

/* "module.dll+0x1234" voor een adres, of het kale adres als er geen module bij hoort. */
static void describe(void *addr, char *out, size_t cap)
{
    HMODULE hm = NULL;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &hm) &&
        GetModuleFileNameA(hm, path, sizeof path)) {
        const char *b = strrchr(path, '\\');
        snprintf(out, cap, "%s+0x%llx", b ? b + 1 : path, (unsigned long long)((uintptr_t)addr - (uintptr_t)hm));
    } else {
        snprintf(out, cap, "%p", addr);
    }
}

static void log_backtrace(const char *what)
{
    void *fr[24];
    USHORT n = RtlCaptureStackBackTrace(1, 24, fr, NULL), i;
    lg("%s (thread %lu), aanroepketen:", what, GetCurrentThreadId());
    for (i = 0; i < n; i++) {
        char d[300];
        describe(fr[i], d, sizeof d);
        lg("    #%u %s", i, d);
    }
}

/* Een haak op de eerste 12 bytes van een functie: "mov rax, handler; jmp rax". De haak logt, haalt zichzelf weg en roept het
 * origineel. Alleen bedoeld om te loggen; hij verandert niets aan het gedrag. */
struct hook { void *target; unsigned char orig[12]; };

static int hook_on(struct hook *h, void *target, void *handler)
{
    unsigned char patch[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
    DWORD old;
    if (!target) return 0;
    if (!VirtualProtect(target, sizeof patch, PAGE_EXECUTE_READWRITE, &old)) return 0;
    h->target = target;
    memcpy(h->orig, target, sizeof patch);
    memcpy(patch + 2, &handler, 8);
    memcpy(target, patch, sizeof patch);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof patch);
    return 1;
}

static void hook_off(struct hook *h)
{
    if (!h->target) return;
    memcpy(h->target, h->orig, sizeof h->orig);
    FlushInstructionCache(GetCurrentProcess(), h->target, sizeof h->orig);
}

static struct hook H_exit, H_rtlexit, H_term;

static VOID WINAPI hk_ExitProcess(UINT code)
{
    char w[64];
    snprintf(w, sizeof w, "ExitProcess(%u)", code);
    log_backtrace(w);
    hook_off(&H_exit);
    ((VOID(WINAPI *)(UINT))H_exit.target)(code);
}

static VOID NTAPI hk_RtlExitUserProcess(ULONG code)
{
    char w[64];
    snprintf(w, sizeof w, "RtlExitUserProcess(0x%lx)", code);
    log_backtrace(w);
    hook_off(&H_rtlexit);
    ((VOID(NTAPI *)(ULONG))H_rtlexit.target)(code);
}

static BOOL WINAPI hk_TerminateProcess(HANDLE h, UINT code)
{
    if (h == GetCurrentProcess() || h == (HANDLE)(intptr_t)-1) {
        char w[64];
        snprintf(w, sizeof w, "TerminateProcess(zichzelf, %u)", code);
        log_backtrace(w);
    }
    hook_off(&H_term);
    {
        BOOL r = ((BOOL(WINAPI *)(HANDLE, UINT))H_term.target)(h, code);
        hook_on(&H_term, H_term.target, (void *)hk_TerminateProcess);
        return r;
    }
}

static LONG g_exc_lines;

static LONG CALLBACK veh(EXCEPTION_POINTERS *e)
{
    DWORD c = e->ExceptionRecord->ExceptionCode;
    /* alleen de ernstige: C++-uitzonderingen, DbgPrint en threadnamen komen veel en zeggen niets */
    if (c == 0xC0000005 || c == 0xC000001D || c == 0xC00000FD || c == 0xC0000094 || c == 0xC0000374 || c == 0xC0000409 ||
        c == 0xC0000096 || c == 0xC000008C) {
        if (InterlockedIncrement(&g_exc_lines) <= 40) {
            char d[300];
            describe(e->ExceptionRecord->ExceptionAddress, d, sizeof d);
            lg("uitzondering 0x%08lx op %s (thread %lu), info %llx %llx", c, d, GetCurrentThreadId(),
               (unsigned long long)e->ExceptionRecord->ExceptionInformation[0], (unsigned long long)e->ExceptionRecord->ExceptionInformation[1]);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI last_chance(EXCEPTION_POINTERS *e)
{
    char d[300];
    describe(e->ExceptionRecord->ExceptionAddress, d, sizeof d);
    lg("ONAFGEHANDELDE uitzondering 0x%08lx op %s (thread %lu)", e->ExceptionRecord->ExceptionCode, d, GetCurrentThreadId());
    return EXCEPTION_CONTINUE_SEARCH;
}

static void at_exit(void)
{
    lg("exit() via de C-runtime van de lader (AVS-regels gezien: %ld)", (long)g_avs_lines);
}

static void install_exit_watch(void)
{
    HMODULE k = GetModuleHandleA("kernel32.dll"), n = GetModuleHandleA("ntdll.dll");
    int a = 0, b = 0, c = 0;
    if (k) {
        a = hook_on(&H_exit, (void *)GetProcAddress(k, "ExitProcess"), (void *)hk_ExitProcess);
        c = hook_on(&H_term, (void *)GetProcAddress(k, "TerminateProcess"), (void *)hk_TerminateProcess);
    }
    if (n) b = hook_on(&H_rtlexit, (void *)GetProcAddress(n, "RtlExitUserProcess"), (void *)hk_RtlExitUserProcess);
    AddVectoredExceptionHandler(1, veh);
    SetUnhandledExceptionFilter(last_chance);
    atexit(at_exit);
    lg("uitgangswacht: ExitProcess %s, RtlExitUserProcess %s, TerminateProcess %s", a ? "aan" : "UIT", b ? "aan" : "UIT", c ? "aan" : "UIT");
}

/* stdout/stderr van wat hierna geladen wordt (de CRT van arkndd.dll en de AVS-bibliotheken lezen die bij het laden) naar een
 * bestand, zodat een printf of assert van het spel ook in de view terechtkomt. RG_MACAW_STDIO=0 laat het zoals het was. */
static void redirect_stdio(void)
{
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE f;
    char *v = getenv("RG_MACAW_STDIO");
    if (v && v[0] == '0') { lg("stdout/stderr van het spel: ongemoeid (RG_MACAW_STDIO=0)"); return; }
    f = CreateFileA("rg-macaw-stdio.log", FILE_APPEND_DATA | FILE_GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { lg("rg-macaw-stdio.log aanmaken faalde (%lu); stdout/stderr ongemoeid", GetLastError()); return; }
    SetStdHandle(STD_OUTPUT_HANDLE, f);
    SetStdHandle(STD_ERROR_HANDLE, f);
    lg("stdout/stderr van het spel -> rg-macaw-stdio.log");
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
/* avs_boot (gemeten in libavs-win64.dll 2.16.3, 0x180030f90): (config, std_heap, std_heap_grootte, ?, log_schrijver, schrijver_context).
 * Het 4e argument gaat naar een intern beheerblok en mag NULL (zo bootte het al); zonder log_schrijver gebruikt AVS een lege functie. */
typedef void (*fn_avs_log_writer)(const char *chars, uint32_t nchars, void *ctx);
typedef int (*fn_boot)(void *config_node, void *heap, size_t heap_size, void *avs_heap, fn_avs_log_writer log_writer, void *log_ctx);
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
    } else if (kind == 3) {   /* ea3-config: ontbrekende velden aanvullen, zie rgm_ea3_prepare */
        struct rgm_ea3_opts o;
        char report[512];
        const char *ev;
        memset(&o, 0, sizeof o);
        o.softid = "012199999999";                                   /* standaardwaarde van spice2x/bemanitools */
        o.accountid = (ev = getenv("RG_MACAW_ACCOUNTID")) && ev[0] ? ev : "0000000000000001";
        if (strcmp(o.accountid, "none") == 0) o.accountid = NULL;     /* proef: het veld weglaten zoals de dump (geeft "bad account ID") */
        /* services: standaard de URL uit de dump (zoals onder TeknoParrot). "offline" = een loopback-adres waar niets luistert:
         * ea3 meldt dan "connection fail" en boot gewoon door, zonder een e-amusement-server te benaderen. */
        o.services = (ev = getenv("RG_MACAW_SERVICES")) && ev[0] ? ev : NULL;
        if (o.services && strcmp(o.services, "offline") == 0) o.services = "http://127.0.0.1:1/";
        o.pcbid = (ev = getenv("RG_MACAW_PCBID")) && ev[0] ? ev : NULL;
        o.hardid = (ev = getenv("RG_MACAW_HARDID")) && ev[0] ? ev : NULL;
        len = rgm_ea3_prepare(buf, len, cap, &o, report, sizeof report);
        if (report[0]) {
            char *l = report, *nl;
            while (*l && (nl = strchr(l, '\n')) != NULL) { *nl = 0; lg("%s: %s", name, l); l = nl + 1; }
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

    InitializeCriticalSection(&g_lock);
    if (argc < 2) { fprintf(stderr, "gebruik: rg-macaw.exe <pad naar modules\\arkndd.dll>\n"); return 2; }
    if (!rgm_module_root(argv[1], root, sizeof root)) die("kan de spelmap niet afleiden uit %s", argv[1]);
    if (!SetCurrentDirectoryA(root)) die("SetCurrentDirectory(%s) faalde", root);
    snprintf(path, sizeof path, "%s/rg-macaw.log", ".");
    g_log = fopen(path, "w");
    lg("rg-macaw start; spelmap %s; module %s", root, argv[1]);
    install_exit_watch();
    redirect_stdio();
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
    lg("avs_boot (heap %zu MB, log-schrijver %p)", heap_sz >> 20, (void *)avs_log_writer);
    if (A.boot(cfgnode, heap, heap_sz, NULL, avs_log_writer, NULL) < 0) die("avs_boot gaf een fout");
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

    /* 5. het spel laden en starten (RG_MACAW_NOGAME=1: alleen AVS + ea3, zonder arkndd.dll te laden; voor proeven zonder spel) */
    if (getenv_int("RG_MACAW_NOGAME", 0)) {
        int secs = getenv_int("RG_MACAW_NOGAME_SECS", 15);
        int r;
        lg("NOGAME: het spel wordt niet geladen; ea3_boot(%p)", ea3node);
        r = A.ea3_boot(ea3node);
        lg("NOGAME: ea3_boot teruggekeerd (waarde %d, 0x%x); %d s wachten op AVS/ea3", r, (unsigned)r, secs);
        Sleep((DWORD)secs * 1000);
        lg("NOGAME: klaar (AVS-regels gezien: %ld)", (long)g_avs_lines);
        return 0;
    }
    game = LoadLibraryA(argv[1]);
    if (!game) die("LoadLibrary(%s) faalde (%lu)", argv[1], GetLastError());
    lg("spel geladen: %s", argv[1]);
    init = (fn_entry_init)GetProcAddress(game, "dll_entry_init");
    main_ = (fn_entry_main)GetProcAddress(game, "dll_entry_main");
    if (!init || !main_) die("dll_entry_init/dll_entry_main ontbreken in %s", argv[1]);
    lg("dll_entry_init(%s)", sid);
    if (!init(sid, appnode)) die("dll_entry_init gaf false");
    lg("dll_entry_init gelukt; ea3_boot");
    {
        /* de teruggave van ea3_boot is niet bevestigd (void of bool, afhankelijk van de AVS-versie): alleen loggen */
        int r = A.ea3_boot(ea3node);
        lg("EA3 gebooted (ea3_boot gaf %d, 0x%x); dll_entry_main", r, (unsigned)r);
    }
    {
        int r = main_();
        lg("dll_entry_main gaf %d", r);
    }
    return 0;
}

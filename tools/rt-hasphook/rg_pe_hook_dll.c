/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_pe_hook_dll.c — de Windows-kant van de generieke PE-inline-hook: een DLL die in het spelproces
 * geladen wordt en daar de per-game lijst toepast op het BEELD VAN HET SPEL.
 *
 * WAAROM EEN DLL EN GEEN ELF-PRELOAD
 * ----------------------------------
 * Luigi's Mansion is een PE (VACUUM.exe) onder Wine. Uit B0 (patches/LuigisMansion/B0-LEGE-DLL.md)
 * is gemeten dat de lader een DLL in het spelproces laadt (TP_PRELOAD_DLL, precies EEN per run) en
 * dat is dus het bewezen kanaal. Alle kennis zit in het ini-bestand, niet hier: deze DLL bevat geen
 * enkele game-naam en geen game-adres.
 *
 * WAT DE DLL DOET (alles in DllMain, op de hoofdthread, VOOR het spel zijn entry point uitvoert)
 * ------------------------------------------------------------------------------------------------
 *  1. de lijst lezen en het logboek openen (met een run-scheiding: tijd, pid, tid);
 *  2. [uitgang]: kernel32!ExitProcess omleiden naar een handler die voor bekende terugkeeradressen
 *     naar de aanroeper TERUGKEERT in plaats van het proces te beeindigen (zie rg_exit_handler);
 *  3. trigger = protect: ntdll!NtProtectVirtualMemory omleiden. Op het moment dat de pakker de
 *     geconfigureerde sectie verzegelt (nieuwe bescherming = trigger_prot) past de omleiding ALLE
 *     regels (functiehaken en byte-patches) toe, NOG VOOR de aanroep wordt doorgegeven. Dat gebeurt
 *     op de thread van de pakker, synchroon: er is geen race met het spel en niet gepold.
 *  4. een eigen thread die in trigger-modus NIETS toepast voordat de trigger kwam of de terugval
 *     (wacht_s seconden) verstreken is, en in poll-modus de oude wachtroute doet. Eenmalig: de
 *     eerste die rg_pe_claim() wint past toe, de rest doet niets.
 *
 * OMGEVING
 *   RG_HOOK_INI         het ini-bestand (verplicht; zonder dit gebeurt er niets)
 *   RG_HOOK_LOG         pad voor het logboek (standaard rg-pe-hook.log in de werkmap)
 *   RG_HOOK_BYTES_LOG   pad van het bewijsartefact;  RG_HOOK_IMAGE  naam van het beeldbestand
 *
 * BOUWEN (de mingw-wrapper van de eigenaar; geen container)
 *   /home/deck/.cache/ravengun-mingw/mingw-gcc -shared -O2 -o rg-pe-hook.dll \
 *       tools/rt-hasphook/rg_pe_hook_dll.c tools/rt-hasphook/rg_pe_hook.c tools/rt-hasphook/rg_capcom_board.c \
 *       -Wl,--image-base,0x180000000 -Wl,--no-insert-timestamp
 */
#if defined(_WIN32)

#include "rg_pe_hook.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>

static rg_pe_ctx G;
static void rg_w_nu(void);   /* [wacht]: bestaande hardware-watchpoints nu zetten */
static FILE *G_log;
static HANDLE G_uit_h = INVALID_HANDLE_VALUE;   /* ongebufferd logboek (WriteFile) voor handlers */
static char G_logpad[MAX_PATH];
static ULONG_PTR G_basis;
static char G_modnaam[64] = "hoofdmodule";
static int G_trigger_ok;

typedef LONG (NTAPI *nt_protect_t)(HANDLE, PVOID *, PSIZE_T, ULONG, PULONG);
typedef void (NTAPI *rtl_exit_t)(NTSTATUS);
static nt_protect_t G_real_protect;
static rtl_exit_t G_rtl_exit;

/* ------------------------------------------------------------------ logboek */

static void rg_meld(const char *fmt, ...)
{
    va_list ap;
    if (!G_log)
        return;
    fprintf(G_log, "[rg-pe-hook] ");
    va_start(ap, fmt);
    vfprintf(G_log, fmt, ap);
    va_end(ap);
    fputc('\n', G_log);
    fflush(G_log);
}

/* Een regel direct in het bestand (een enkele append; niets blijft in een buffer hangen). */
static void rg_regel_uit(const char *s)
{
    DWORD n;
    if (G_uit_h == INVALID_HANDLE_VALUE)
        return;
    WriteFile(G_uit_h, s, (DWORD)strlen(s), &n, NULL);
}

/* "VACUUM.exe+0xf1374d" als het adres in de hoofdmodule ligt, anders het ruwe adres. */
static void rg_adres_tekst(char *uit, size_t groot, uint64_t a)
{
    if (G_basis && a >= G_basis && a < G_basis + G.beeld_n)
        snprintf(uit, groot, "%s+0x%llx", G_modnaam, (unsigned long long)(a - G_basis));
    else
        snprintf(uit, groot, "0x%llx (buiten de hoofdmodule)", (unsigned long long)a);
}

static void rg_hex16(char *uit, size_t groot, const unsigned char *b)
{
    size_t k = 0;
    int i;
    uit[0] = 0;
    for (i = 0; i < 16 && k + 4 < groot; i++)
        k += (size_t)snprintf(uit + k, groot - k, "%s%02x", i ? " " : "", b[i]);
}

/* Wordt door TODO-stubs aangeroepen: de index komt van de stub zelf. */
static void rg_todo_melder(int idx)
{
    if (G_log) {
        fprintf(G_log, "[rg-pe-hook]   [stub] %s: ANTWOORD NOG NIET GEMETEN (TODO) -- geeft 0 terug\n",
                (idx >= 0 && idx < G.n) ? G.regels[idx].naam : "?");
        fflush(G_log);
    }
}

/* antwoord = log0: tick, thread, naam, de vier argumentregisters en het terugkeeradres. */
static void rg_log_melder(int idx, const uint64_t *a, uint64_t ra)
{
    char buf[640], wie[120];
    rg_adres_tekst(wie, sizeof wie, ra);
    snprintf(buf, sizeof buf,
             "[rg-pe-hook]   [aanroep] tick %llu tid %lu %s(rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx) "
             "terugkeer=%s\n",
             (unsigned long long)GetTickCount64(), (unsigned long)GetCurrentThreadId(),
             (idx >= 0 && idx < G.n) ? G.regels[idx].naam : "?", (unsigned long long)a[0],
             (unsigned long long)a[1], (unsigned long long)a[2], (unsigned long long)a[3], wie);
    rg_regel_uit(buf);
}

/* ------------------------------------------------------------------ [uitgang]: ExitProcess */

/*
 * De handler wordt met `jmp` bereikt (12-byte sprong op de ingang van kernel32!ExitProcess), dus de
 * stapel is exact zoals bij de ingang van het echte ExitProcess: [rsp] is het terugkeeradres van de
 * aanroeper met 32 bytes schaduwruimte erboven. Een gewone x64-functie met een argument (rcx)
 * verwacht dat; `return` gaat naar de aanroeper. Het echte ExitProcess leest alleen ecx, dus de
 * volle rcx (0x140415820 is gemeten) wordt gelogd en alleen de onderste 32 bits gaan naar
 * ntdll!RtlExitUserProcess, net als in het origineel.
 *
 * De handler leest alleen wat in DllMain is klaargezet (G.uitgang_*, G_basis, G_rtl_exit): hij hangt
 * niet af van de ini-regels, van het ontpakken of van de trigger. Het logboek is een Win32-handle
 * met een WriteFile per regel (geen CRT-slot, geen buffer).
 */
static volatile LONG G_ex_aantal;      /* alle ExitProcess-aanroepen */

__attribute__((noinline, used))
static void WINAPI rg_exit_handler(ULONG_PTR rcx)
{
    ULONG_PTR ra = (ULONG_PTR)__builtin_return_address(0);
    LONG nr = InterlockedIncrement(&G_ex_aantal);
    int in_exe = (G_basis && ra >= G_basis && ra < G_basis + G.beeld_n);
    int terug = in_exe && rg_pe_uitgang_besluit(&G, (uint64_t)(ra - G_basis), (long)G.uitgang_terug);
    char wie[120], buf[640];
    LONG tn = 0;

    rg_adres_tekst(wie, sizeof wie, (uint64_t)ra);
    if (terug)
        tn = InterlockedIncrement(&G.uitgang_terug);
    else
        InterlockedIncrement(&G.uitgang_door);
    snprintf(buf, sizeof buf,
             "[rg-pe-hook] [uitgang] tick %llu tid %lu ExitProcess #%ld: rcx=0x%llx (laag32=0x%x) "
             "terugkeeradres=%s -> %s",
             (unsigned long long)GetTickCount64(), (unsigned long)GetCurrentThreadId(), (long)nr,
             (unsigned long long)rcx, (unsigned)rcx, wie, "");
    /* de uitkomst erachter, zodat de regel in een keer in het bestand komt */
    if (terug)
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "TERUG #%ld (naar de aanroeper, niet "
                 "beeindigd; hoogstens %d)\n", (long)tn, G.uitgang_max);
    else
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf),
                 "DOORGELATEN (ntdll!RtlExitUserProcess(0x%x))%s\n", (unsigned)rcx,
                 in_exe ? "" : " [terugkeeradres niet in de hoofdmodule]");
    rg_regel_uit(buf);
    if (terug && getenv("RG_HOOK_STACKWALK")) {
        /* Een ruwe stapelscan: elke waarde die in de hoofdmodule ligt is een kandidaat-terugkeeradres.
           Zo is te zien welke keten de exit bereikt heeft, ook als de frames niet kloppen. */
        const uint64_t *p = (const uint64_t *)__builtin_frame_address(0);
        int i, k = 0;
        char b[512];
        snprintf(b, sizeof b, "[rg-pe-hook] [uitgang] stapelscan vanaf %p:", (const void *)p);
        rg_regel_uit(b);
        for (i = 0; i < 200 && k < 14; i++) {
            uint64_t w = p[i];
            if (G_basis && w >= G_basis && w < G_basis + G.beeld_n) {
                snprintf(b, sizeof b, " +0x%llx", (unsigned long long)(w - G_basis));
                rg_regel_uit(b);
                k++;
            }
        }
        rg_regel_uit("\n");
    }
    if (terug)
        return;                 /* ExitProcess keert normaal niet terug: geen rax-contract */
    if (G_rtl_exit)
        G_rtl_exit((NTSTATUS)(UINT)rcx);
    /* Hoort niet te gebeuren; een proces dat nu nog leeft zou een onzichtbare fout zijn. */
    rg_regel_uit("[rg-pe-hook] [uitgang] RtlExitUserProcess keerde TERUG -- TerminateProcess\n");
    TerminateProcess(GetCurrentProcess(), (UINT)rcx);
}

/* Installeert de omleiding. Weigert luid (logregel, geen haak) als iets niet is zoals gemeten. */
static void rg_installeer_uitgang(void)
{
    char mod[64], fn[64], hex_voor[96], hex_na[96];
    const char *bang = strchr(G.uitgang_export, '!');
    HMODULE m, nt;
    unsigned char *f, voor[16], na[16], haak[12];
    int i;

    if (!bang || (size_t)(bang - G.uitgang_export) >= sizeof mod || strlen(bang + 1) >= sizeof fn) {
        rg_meld("uitgang: GEEN HAAK: `export = %s` is niet van de vorm module.dll!Naam", G.uitgang_export);
        return;
    }
    memcpy(mod, G.uitgang_export, (size_t)(bang - G.uitgang_export));
    mod[bang - G.uitgang_export] = 0;
    snprintf(fn, sizeof fn, "%s", bang + 1);
    m = GetModuleHandleA(mod);
    nt = GetModuleHandleA("ntdll.dll");
    f = m ? (unsigned char *)(void *)GetProcAddress(m, fn) : NULL;
    G_rtl_exit = nt ? (rtl_exit_t)(void *)GetProcAddress(nt, "RtlExitUserProcess") : NULL;
    if (!f || !G_rtl_exit) {
        rg_meld("uitgang: GEEN HAAK: %s op %p, ntdll!RtlExitUserProcess op %p -- niet te vinden", G.uitgang_export,
                (void *)f, (void *)G_rtl_exit);
        return;
    }
    memcpy(voor, f, 16);
    rg_hex16(hex_voor, sizeof hex_voor, voor);
    rg_meld("uitgang: %s op %p, ntdll!RtlExitUserProcess op %p, handler op %p", G.uitgang_export, (void *)f,
            (void *)G_rtl_exit, (void *)&rg_exit_handler);
    rg_meld("uitgang: eerste 16 bytes VOOR het haken: %s", hex_voor);
    if (!rg_pe_exit_stub_ok(voor)) {
        rg_meld("uitgang: GEEN HAAK: het begin is niet het verwachte (48 83 ec 28 ff 15 ... en bytes 10..15 "
                "opvulling 90/cc); 12 bytes zouden een buurfunctie of een eerdere haak raken");
        return;
    }
    rg_pe_maak_haak12(haak, (uint64_t)(uintptr_t)&rg_exit_handler);
    if (!rg_pe_schrijf(&G, (uintptr_t)f, haak, sizeof haak, "uitgang")) {
        rg_meld("uitgang: GEEN HAAK: schrijven mislukt (zie de regel hierboven)");
        return;
    }
    memcpy(na, f, 16);
    rg_hex16(hex_na, sizeof hex_na, na);
    G.uitgang_staat = memcmp(na, haak, sizeof haak) == 0;
    for (i = 0; i < G.uitgang_terug_n; i++)
        rg_meld("uitgang: terug_bij[%d] = RVA 0x%x (%s+0x%x)", i, G.uitgang_terug_bij[i], G_modnaam,
                G.uitgang_terug_bij[i]);
    rg_meld("uitgang: eerste 16 bytes NA het haken:  %s  (%s)", hex_na,
            G.uitgang_staat ? "haak staat" : "!!!! WIJKT AF VAN WAT GESCHREVEN IS");
    rg_meld("uitgang: haak %s; max %d keer TERUG per terug_bij-adres-aanroep, daarna doorlaten",
            G.uitgang_staat ? "staat" : "staat niet", G.uitgang_max);
}

/* ------------------------------------------------------------------ trigger: NtProtectVirtualMemory */

/*
 * De omleiding. Bereikt via een 14-byte absolute sprong op de ingang van ntdll!NtProtectVirtualMemory;
 * de originele 16 bytes (drie hele instructies zonder RIP-relatieve operand) staan in een
 * trampoline. Op de trigger wordt ALLES toegepast voor de aanroep wordt doorgegeven: de sectie is
 * dan nog schrijfbaar (de pakker maakt hem juist nu leesbaar/uitvoerbaar) en de inhoud is
 * compleet. Geneste aanroepen (de schrijfhulp roept zelf VirtualProtect aan) komen hier ook langs;
 * ze passen niet op de trigger (andere bescherming) en de eenmalig-vlag staat dan op 1, dus ze
 * worden altijd doorgegeven.
 */
static LONG NTAPI rg_det_protect(HANDLE h, PVOID *base, PSIZE_T size, ULONG prot, PULONG oud)
{
    if (G.toegepast == 0 && G.trigger_base && base && prot == G.trigger_prot &&
        (uintptr_t)*base == G.trigger_base && h == (HANDLE)(LONG_PTR)-1) {
        int n;
        rg_meld("trigger: NtProtectVirtualMemory(proces -1, base 0x%llx, grootte 0x%llx, nieuw 0x%lx) -- "
                "sectie %s wordt verzegeld; ik pas NU alles toe, voor de aanroep wordt doorgegeven",
                (unsigned long long)(uintptr_t)*base, (unsigned long long)(size ? *size : 0),
                (unsigned long)prot, G.trigger_sectie);
        n = rg_pe_pas_toe(&G, "trigger");
        rg_w_nu();
        rg_meld("resultaat (trigger): %d van %d functie(s) aangehaakt; de aanroep wordt nu doorgegeven", n, G.n);
    }
    return G_real_protect(h, base, size, prot, oud);
}

static int rg_installeer_protect(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    unsigned char *f = nt ? (unsigned char *)(void *)GetProcAddress(nt, "NtProtectVirtualMemory") : NULL;
    unsigned char *zw = nt ? (unsigned char *)(void *)GetProcAddress(nt, "ZwProtectVirtualMemory") : NULL;
    unsigned char voor[16], na[16], sprong[14], *tr;
    char hex[96];
    if (!f) {
        rg_meld("trigger: GEEN TRIGGER: ntdll!NtProtectVirtualMemory niet gevonden");
        return 0;
    }
    memcpy(voor, f, 16);
    rg_hex16(hex, sizeof hex, voor);
    rg_meld("trigger: ntdll!NtProtectVirtualMemory op %p; eerste 16 bytes VOOR het haken: %s", (void *)f, hex);
    if (zw && zw != f)
        rg_meld("trigger: LET OP: ZwProtectVirtualMemory (%p) is een ander adres en wordt NIET omgeleid", (void *)zw);
    if (!rg_pe_nt_stub_ok(voor)) {
        rg_meld("trigger: GEEN TRIGGER: de bytes zijn niet de verwachte (4c 8b d1 b8 <nr> f6 04 25 08 03 fe 7f 01); "
                "een andere Wine-bouw? Er wordt niets omgeleid");
        return 0;
    }
    tr = (unsigned char *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) {
        rg_meld("trigger: GEEN TRIGGER: VirtualAlloc voor de trampoline mislukte, GetLastError %lu",
                (unsigned long)GetLastError());
        return 0;
    }
    rg_pe_maak_trampoline(tr, voor, (uint64_t)(uintptr_t)(f + 16));
    FlushInstructionCache(GetCurrentProcess(), tr, 30);
    G_real_protect = (nt_protect_t)(void *)tr;
    rg_pe_maak_sprong14(sprong, (uint64_t)(uintptr_t)&rg_det_protect);
    if (!rg_pe_schrijf(&G, (uintptr_t)f, sprong, sizeof sprong, "NtProtectVirtualMemory-omleiding")) {
        rg_meld("trigger: GEEN TRIGGER: schrijven mislukt (zie de regel hierboven)");
        return 0;
    }
    memcpy(na, f, 16);
    rg_hex16(hex, sizeof hex, na);
    rg_meld("trigger: eerste 16 bytes NA het haken:  %s  (%s)", hex,
            memcmp(na, sprong, sizeof sprong) == 0 ? "omleiding staat" : "!!!! WIJKT AF");
    return memcmp(na, sprong, sizeof sprong) == 0;
}

/* ------------------------------------------------------------------ de eigen thread */

/* ------------------------------------------------------------------ [wacht]: hardware watchpoints */

/*
 * RG_HOOK_WATCH=<rva>[:<len>[:<modus>]],... (hoogstens 4, hex-rva's in de hoofdmodule)
 *
 * Een MEETINSTRUMENT, geen haak: per thread worden DR0..DR3 als hardware-breekpunten gezet en elke
 * treffer wordt gelogd met de RIP en de registers. `modus` is `x` (uitvoeren), `w` (schrijven) of
 * `rw` (lezen en schrijven, de standaard; `len` is dan 1, 2, 4 of 8, standaard 4). Uit zonder de
 * variabele. Gebruikt om te zien WIE een stuk spelgeheugen leest of schrijft -- bijvoorbeeld of de
 * pakker zijn eigen (door ons omgezette) code nog keurt, of welke waarde een beslissing bepaalt.
 *
 * Het breekpunt staat per thread, dus een eigen thread zet het op elke bestaande thread en daarna
 * elke 200 ms op elke nieuwe, tot RG_HOOK_WATCH_S seconden (standaard 30) voorbij zijn. De VEH
 * logt en gaat door (RF in EFlags), dus het spel merkt niets van de meting.
 */
#define RG_W_MAX 4
typedef struct {
    uint64_t rva;
    int len;
    char modus;                     /* 'x', 'w' of 'r' (lezen en schrijven) */
    volatile long treffers;
} rg_wacht;
static rg_wacht G_w[RG_W_MAX];
static int G_w_n;
static void *G_w_veh;
static volatile LONG G_w_aan;

/* De DR7-waarde voor deze vier punten. */
static unsigned long rg_w_dr7(void)
{
    unsigned long v = 0;
    int i;
    for (i = 0; i < G_w_n; i++) {
        unsigned type = G_w[i].modus == 'x' ? 0u : (G_w[i].modus == 'w' ? 1u : 3u);
        unsigned len = 3u;                                  /* 11 = 4 bytes */
        if (G_w[i].modus == 'x')
            len = 0u;
        else if (G_w[i].len == 1)
            len = 0u;
        else if (G_w[i].len == 2)
            len = 1u;
        else if (G_w[i].len == 8)
            len = 2u;
        v |= 1u << (2 * i);                                 /* L0..L3: lokaal aan */
        v |= (type | (len << 2)) << (16 + 4 * i);           /* RWn/LENn */
    }
    return v;
}

/* Zet de punten op één thread (idempotent). */
static int rg_w_zet_thread(HANDLE h)
{
    CONTEXT c;
    int i, ok;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(h, &c))
        return 0;
    c.Dr0 = G_w_n > 0 ? (DWORD64)(G_basis + G_w[0].rva) : 0;
    c.Dr1 = G_w_n > 1 ? (DWORD64)(G_basis + G_w[1].rva) : 0;
    c.Dr2 = G_w_n > 2 ? (DWORD64)(G_basis + G_w[2].rva) : 0;
    c.Dr3 = G_w_n > 3 ? (DWORD64)(G_basis + G_w[3].rva) : 0;
    c.Dr6 = 0;
    c.Dr7 = rg_w_dr7();
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    ok = SetThreadContext(h, &c) != 0;
    (void)i;
    return ok;
}

/* Alle threads van dit proces (behalve deze) langs. Geeft het aantal gelukte threads. */
static int rg_w_alle_threads(void)
{
    HANDLE snap;
    THREADENTRY32 te;
    int n = 0;
    DWORD ik = GetCurrentThreadId(), pid = GetCurrentProcessId();
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te))
        do {
            HANDLE h;
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == ik)
                continue;
            h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE,
                           te.th32ThreadID);
            if (!h)
                continue;
            SuspendThread(h);
            if (rg_w_zet_thread(h))
                n++;
            ResumeThread(h);
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    return n;
}

/* De VEH: loggen en doorgaan. */
static LONG CALLBACK rg_w_veh(PEXCEPTION_POINTERS ep)
{
    CONTEXT *c;
    unsigned long dr6;
    int i;
    if (!G_w_n || !ep || ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    c = ep->ContextRecord;
    dr6 = (unsigned long)c->Dr6;
    if (!dr6)
        return EXCEPTION_CONTINUE_SEARCH;
    for (i = 0; i < G_w_n; i++) {
        char buf[512], wie[128];
        long n;
        uint64_t a;
        if (!(dr6 & (1u << i)))
            continue;
        n = InterlockedIncrement(&G_w[i].treffers);
        if (n > 60 && (n % 2000) != 0) {                /* niet vollopen in een lus */
            continue;
        }
        a = (uint64_t)(G_basis + G_w[i].rva);
        rg_adres_tekst(wie, sizeof wie, (uint64_t)c->Rip);
        snprintf(buf, sizeof buf,
                 "[rg-pe-hook] [wacht] rva 0x%llx #%ld tid %lu rip=%s waarde=0x%llx "
                 "rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx rax=0x%llx\n",
                 (unsigned long long)G_w[i].rva, n, (unsigned long)GetCurrentThreadId(), wie,
                 (unsigned long long)*(volatile uint64_t *)a, (unsigned long long)c->Rcx,
                 (unsigned long long)c->Rdx, (unsigned long long)c->R8, (unsigned long long)c->R9,
                 (unsigned long long)c->Rax);
        rg_regel_uit(buf);
    }
    c->Dr6 = 0;
    c->EFlags |= 0x10000;                               /* RF: de instructie niet nog eens */
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* Leest RG_HOOK_WATCH en zet het instrument aan. */
static void rg_start_wacht(void)
{
    const char *s = getenv("RG_HOOK_WATCH");
    const char *s0 = s;
    if (!s || !*s || !G_basis)
        return;
    while (*s && G_w_n < RG_W_MAX) {
        char *e;
        unsigned long long rva = strtoull(s, &e, 16);
        int len = 4;
        char modus = 'r';
        if (e == s)
            break;
        s = e;
        if (*s == ':') {
            len = (int)strtol(s + 1, &e, 10);
            s = e;
        }
        if (*s == ':') {
            int breed = 2;
            if (s[1] == 'x' || s[1] == 'X') modus = 'x';
            else if (s[1] == 'w' || s[1] == 'W') modus = 'w';
            else { modus = 'r'; if (s[2] == 'w' || s[2] == 'W') breed = 3; }
            s += breed;
        }
        if (len != 1 && len != 2 && len != 4 && len != 8)
            len = 4;
        G_w[G_w_n].rva = rva;
        G_w[G_w_n].len = len;
        G_w[G_w_n].modus = modus;
        G_w[G_w_n].treffers = 0;
        G_w_n++;
        while (*s == ',' || *s == ' ')
            s++;
    }
    if (!G_w_n)
        return;
    rg_meld("[wacht] %d hardware-watchpoint(s) uit RG_HOOK_WATCH = %s; ze worden gezet zodra de "
            "regels zijn toegepast (niet eerder: een breekpunt op de pakker verandert zijn route)", G_w_n, s0);
    {
        int i;
        for (i = 0; i < G_w_n; i++)
            rg_meld("[wacht]   slot %d: rva 0x%llx len %d modus %c", i, (unsigned long long)G_w[i].rva,
                    G_w[i].len, G_w[i].modus);
    }
}

/* Zet de punten NU: de VEH, de huidige thread en alle andere threads. Aangeroepen direct na het
   toepassen van de regels (dus op de thread van de pakker, die ook de exit bereikt). */
/* Een hulpthread: die zet de punten op DEZE thread. Je kunt je eigen context niet lezen of
   schrijven (SuspendThread op jezelf gevolgd door SetThreadContext laat het proces hard
   vastlopen -- gemeten 2026-10-22: 2 threads, 0,1 % cpu), een andere thread kan dat wel. */
typedef struct {
    HANDLE doel;                    /* dup van de thread die de punten moet krijgen */
    volatile LONG klaar;
} rg_w_klus;

static DWORD WINAPI rg_w_hulp(LPVOID arg)
{
    rg_w_klus *k = (rg_w_klus *)arg;
    if (!SuspendThread(k->doel)) {
        rg_w_zet_thread(k->doel);
        ResumeThread(k->doel);
    }
    k->klaar = 1;
    return 0;
}

static void rg_w_nu(void)
{
    rg_w_klus klus;
    HANDLE h;
    int wacht;
    if (!G_w_n || G_w_aan)
        return;
    G_w_veh = (void *)AddVectoredExceptionHandler(1, rg_w_veh);
    G_w_aan = 1;
    /*
     * De pakker, de start van het spel en de exit draaien op de thread die ons hier aanroept
     * (zelfde tid in het logboek), dus die krijgt de punten als eerste. RG_HOOK_WATCH_ALL=1 zet ze
     * ook op de andere threads -- dat stilleggen van alles bleek de game te laten vastlopen, dus het
     * staat uit tenzij je het vraagt.
     */
    klus.doel = NULL;
    klus.klaar = 0;
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &klus.doel, 0,
                        FALSE, DUPLICATE_SAME_ACCESS)) {
        h = CreateThread(NULL, 0, rg_w_hulp, &klus, 0, NULL);
        if (h) {
            for (wacht = 0; wacht < 400 && !klus.klaar; wacht++)
                Sleep(1);
            CloseHandle(h);
        }
        CloseHandle(klus.doel);
    }
    if (getenv("RG_HOOK_WATCH_ALL"))
        rg_w_alle_threads();
    {
        char b[256];
        snprintf(b, sizeof b, "[rg-pe-hook] [wacht] gezet: %s; DR7 0x%lx\n",
                 klus.klaar ? "deze thread" : "MISLUKT (deze thread)", rg_w_dr7());
        rg_regel_uit(b);
    }
}

static DWORD WINAPI rg_werk_thread(LPVOID arg)
{
    int n;
    (void)arg;
    if (G_trigger_ok) {
        uint64_t t0 = GetTickCount64();
        int ws = G.wacht_s > 0 ? G.wacht_s : 10;
        rg_meld("poll-thread: trigger-modus; ik pas NIETS toe voor de trigger, of voor de terugval na %d s", ws);
        while (!G.toegepast && GetTickCount64() - t0 < (uint64_t)ws * 1000u)
            Sleep(20);
        if (G.toegepast) {
            rg_meld("poll-thread: de trigger heeft de regels toegepast; de terugval is niet nodig");
            return 0;
        }
        rg_meld("poll-thread: de trigger kwam NIET binnen %d s; TERUGVAL naar de poll-route", ws);
        G.fallback = 1;
    } else {
        rg_meld("poll-thread: geen trigger; de wachtroute loopt op deze eigen thread, de hoofdthread gaat verder");
    }
    n = rg_pe_hook(&G);
    rg_meld("resultaat: %d van %d aangehaakt", n, G.n);
    rg_w_nu();
    return 0;
}

/* ------------------------------------------------------------------ optional image dump (diagnostics) */

/*
 * RG_HOOK_DUMP=<file>, RG_HOOK_DUMP_S=<seconds, default 60>: after that many seconds, write the main
 * module's image (SizeOfImage bytes from the image base; pages that are not readable stay zero) to
 * <file>, plus <file>.regions with one line per region (rva, size, state, protect). Off without the
 * variable. Used to read the unpacked game code, which is empty on disk (packed .text).
 */
static char G_dump_pad[MAX_PATH];
static DWORD G_dump_s = 60;

static DWORD WINAPI rg_dump_thread(LPVOID arg)
{
    unsigned char *b = (unsigned char *)G_basis;
    size_t n = G.beeld_n, off = 0;
    unsigned char *kopie;
    char rpad[MAX_PATH + 16];
    FILE *f, *r;
    (void)arg;
    Sleep(G_dump_s * 1000u);
    if (!b || !n)
        return 0;
    kopie = (unsigned char *)calloc(1, n);
    if (!kopie)
        return 0;
    snprintf(rpad, sizeof rpad, "%s.regions", G_dump_pad);
    r = fopen(rpad, "w");
    while (off < n) {
        MEMORY_BASIC_INFORMATION mi;
        size_t len;
        if (!VirtualQuery(b + off, &mi, sizeof mi))
            break;
        len = (size_t)((unsigned char *)mi.BaseAddress + mi.RegionSize - (b + off));
        if (off + len > n)
            len = n - off;
        if (r)
            fprintf(r, "rva 0x%08zx size 0x%08zx state 0x%lx protect 0x%lx\n", off, len,
                    (unsigned long)mi.State, (unsigned long)mi.Protect);
        if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mi.Protect)
            memcpy(kopie + off, b + off, len);
        off += len;
    }
    if (r)
        fclose(r);
    f = fopen(G_dump_pad, "wb");
    if (f) {
        fwrite(kopie, 1, n, f);
        fclose(f);
    }
    free(kopie);
    rg_meld("dump: %zu bytes of the main module written to %s after %lu s", n, G_dump_pad,
            (unsigned long)G_dump_s);
    return 0;
}

static void rg_start_dump(void)
{
    const char *d = getenv("RG_HOOK_DUMP");
    const char *ds = getenv("RG_HOOK_DUMP_S");
    HANDLE h;
    if (!d || !*d)
        return;
    snprintf(G_dump_pad, sizeof G_dump_pad, "%s", d);
    if (ds && atoi(ds) > 0)
        G_dump_s = (DWORD)atoi(ds);
    h = CreateThread(NULL, 0, rg_dump_thread, NULL, 0, NULL);
    if (h)
        CloseHandle(h);
    rg_meld("dump: armed (%s after %lu s)", G_dump_pad, (unsigned long)G_dump_s);
}

/* ------------------------------------------------------------------ DllMain */

void rg_capcom_board_start(void);   /* rg_capcom_board.c: off unless RG_CAPCOM_BOARD is set */

static void rg_init(void)
{
    const char *ini = getenv("RG_HOOK_INI");
    const char *logpad = getenv("RG_HOOK_LOG");
    HMODULE spel;
    int v;
    static int teller;

    rg_capcom_board_start();
    if (!ini || !*ini) {
        /* Niets doen is de veilige kant: zonder lijst weten we niet wat we mogen aanraken. */
        return;
    }
    snprintf(G_logpad, sizeof G_logpad, "%s", logpad && *logpad ? logpad : "rg-pe-hook.log");
    G_log = fopen(G_logpad, "a");
    if (!G_log) {
        snprintf(G_logpad, sizeof G_logpad, "C:\\rg-pe-hook.log");
        G_log = fopen(G_logpad, "a");
    }
    if (!G_log)
        return;
    G_uit_h = CreateFileA(G_logpad, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);

    spel = GetModuleHandleW(NULL);            /* het spel, niet deze DLL */
    memset(&G, 0, sizeof G);
    G.log = G_log;
    G.pid = (unsigned long)GetCurrentProcessId();
    G.tid = (unsigned long)GetCurrentThreadId();
    G.proc_uptime_ms = 0;
    G.init_teller = ++teller;
    rg_pe_scheiding(&G);                       /* de run-scheiding komt als EERSTE regel */
    if (!spel) {
        rg_meld("geen hoofdmodule gevonden");
        return;
    }
    {
        char pad[MAX_PATH];
        DWORD k = GetModuleFileNameA(spel, pad, sizeof pad);
        if (k > 0 && k < sizeof pad) {
            const char *s = pad, *q;
            for (q = pad; *q; q++)
                if (*q == '\\' || *q == '/')
                    s = q + 1;
            snprintf(G_modnaam, sizeof G_modnaam, "%.63s", s);
        }
    }
    G_basis = (ULONG_PTR)spel;
    rg_meld("DllMain: initialisatie op de hoofdthread (pid %lu, tid %lu); hoofdprogramma %s op %p",
            G.pid, G.tid, G_modnaam, (void *)spel);

    G.lijst_pad = ini;
    G.beeld = (unsigned char *)spel;
    G.todo_melder = rg_todo_melder;
    G.log_melder = rg_log_melder;
    {
        /* Het bewijsartefact: het pad komt uit de omgeving, zodat de drop-in het in de view kan
           leggen waar de volgende partij het kan ophalen. */
        const char *bl = getenv("RG_HOOK_BYTES_LOG");
        const char *bn = getenv("RG_HOOK_IMAGE");
        G.bytes_pad = (bl && *bl) ? bl : "rg-pe-hook-bytes.txt";
        G.beeld_naam = (bn && *bn) ? bn : "(hoofdmodule van dit proces)";
        rg_meld("bewijsartefact: %s   (beeldbestand: %s)", G.bytes_pad, G.beeld_naam);
    }
    {
        rg_pe_kop kop;
        if (!rg_pe_lees_kop((const unsigned char *)spel, 0x1000, &kop)) {
            rg_meld("het hoofdprogramma op %p heeft geen leesbare PE-kop", (void *)spel);
            return;
        }
        G.beeld_n = (size_t)kop.grootte_beeld;
        rg_meld("hoofdprogramma op %p, ImageBase 0x%llx, SizeOfImage 0x%x", (void *)spel,
                (unsigned long long)kop.imagebase, kop.grootte_beeld);
    }
    rg_start_dump();
    rg_start_wacht();

    v = rg_pe_voorbereid(&G);
    if (v < 0) {
        rg_meld("voorbereiding mislukt; er wordt niets aangehaakt");
        return;
    }
    if (v == 1) {
        rg_meld("de lijst is AFGEKEURD (verkeerde build): geen haken, geen trigger, geen uitgang");
        return;
    }

    if (G.uitgang_aanwezig && G.uitgang_actief)
        rg_installeer_uitgang();
    else if (G.uitgang_aanwezig)
        rg_meld("uitgang: UIT (actief = 0); ExitProcess blijft ongemoeid");

    if (G.trigger[0])
        G_trigger_ok = rg_installeer_protect();
    if (G.trigger[0] && !G_trigger_ok)
        rg_meld("trigger gevraagd maar NIET geinstalleerd: de poll-thread past zelf toe (route `poll`)");

    {
        HANDLE h = CreateThread(NULL, 0, rg_werk_thread, NULL, 0, NULL);
        if (h)
            CloseHandle(h);
        else
            rg_meld("kon de eigen thread niet starten; er is geen terugval");
    }
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst;
    if (reason == DLL_PROCESS_ATTACH) {
        /*
         * Dit draait op de HOOFDTHREAD in de aanhechtingsfase, voordat het spel zijn entry point
         * uitvoert. NIET op iets wachten hier: het spel kan pas ontpakken nadat de lader de
         * hoofdthread hervat, en dat gebeurt pas als deze functie terugkeert (wachten = deadlock,
         * gemeten: 16,2 s levensduur, niets aangehaakt). Alles hieronder is synchroon en kort:
         * een bestand lezen, twee korte schrijfacties in ntdll/kernel32, een thread starten.
         */
        rg_init();
    } else if (reason == DLL_PROCESS_DETACH && G.uitgang_aanwezig && G_uit_h != INVALID_HANDLE_VALUE) {
        char buf[256];
        snprintf(buf, sizeof buf,
                 "[rg-pe-hook] [uitgang] DLL_PROCESS_DETACH (%s) tick %llu: uitgang: haak %s; TERUG %ld; "
                 "doorgelaten %ld\n", reserved ? "proceseinde" : "FreeLibrary",
                 (unsigned long long)GetTickCount64(), G.uitgang_staat ? "staat" : "staat niet",
                 (long)G.uitgang_terug, (long)G.uitgang_door);
        rg_regel_uit(buf);
    }
    return TRUE;
}

/* Dwingt een .reloc af, zodat de DLL verplaatsbaar is en LoadLibraryW niet op een bezet ImageBase
   stukloopt (dezelfde les als in B0-LEGE-DLL.md). */
void *const rg_pe_hook_ankers[2] = { (void *)&DllMain, (void *)&rg_pe_hook_ankers };

#endif /* _WIN32 */

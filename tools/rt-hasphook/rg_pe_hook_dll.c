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
 *       tools/rt-hasphook/rg_pe_hook_dll.c tools/rt-hasphook/rg_pe_hook.c \
 *       -Wl,--image-base,0x180000000 -Wl,--no-insert-timestamp
 */
#if defined(_WIN32)

#include "rg_pe_hook.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static rg_pe_ctx G;
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
    return 0;
}

/* ------------------------------------------------------------------ DllMain */

static void rg_init(void)
{
    const char *ini = getenv("RG_HOOK_INI");
    const char *logpad = getenv("RG_HOOK_LOG");
    HMODULE spel;
    int v;
    static int teller;

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

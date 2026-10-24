/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_exit_proef.c -- diagnostische proef (GEEN oplossing): haakt kernel32!ExitProcess in het spelproces
 * van Luigi's Mansion Arcade, logt elke aanroep, en kan (optioneel) NAAR DE AANROEPER TERUGKEREN in
 * plaats van het proces te beeindigen.
 *
 * WAAROM
 * ------
 * VACUUM.exe (64-bit PE onder Wine, gepakt/gevirtualiseerd; secties `.capcom0`/`.capcom1`) sterft op
 * ONZE kern doordat gevirtualiseerde code aan het begin van wWinMain `kernel32!ExitProcess` aanroept
 * (argument 0x140415820, terugkeeradres VACUUM.exe+0xf1374d in `.capcom0`), ongeveer 1 ms nadat het
 * ontpakken klaar is (EXITBESLUIT.md). De GESLOTEN kern haakt ExitProcess in dit spel; wat zijn haak
 * doet weten we niet. De proef: op ONZE kern doen wij hetzelfde en kijken wat het spel daarna doet
 * als het antwoord een `ret` is.
 *
 * OMGEVING
 *   RG_EXITPROEF_LOG     pad van het logboek (standaard rg-exit-proef.log in de werkmap)
 *   RG_EXITPROEF_MODE    `log` (standaard: loggen, daarna echt stoppen) of `terug` (terugkeren naar de
 *                        aanroeper als het terugkeeradres in de sectie ligt, zie hieronder)
 *   RG_EXITPROEF_SECTIE  naam van de sectie van de HOOFDMODULE waarvan het bereik telt als
 *                        "gevirtualiseerde spelcode" (standaard `.capcom0`)
 *   RG_EXITPROEF_MAX     hoeveel aanroepen hoogstens terugkeren (standaard 8); daarna doorlaten
 *
 * HOE HET HAAKT
 * -------------
 * De ingang van kernel32!ExitProcess krijgt een absolute sprong van 12 bytes: `48 b8 <imm64>` (mov rax,
 * handler) + `ff e0` (jmp rax). Er is GEEN trampoline naar het origineel: om echt te stoppen roept de
 * handler zelf `ntdll!RtlExitUserProcess(code)` aan -- wat het origineel in Wine ook doet.
 *
 * PASSEN 12 BYTES? In het Wine van het project (proton-11.0-2) is ExitProcess
 *     48 83 ec 28            sub rsp,0x28
 *     ff 15 <rel32>          call [__imp_RtlExitUserProcess]
 *     90 90 90 90 90 90      opvulling tot de 16-bytegrens
 * ofwel 10 bytes code in een slot van 16. De 12 bytes passen dus ZONDER een buurfunctie te raken
 * (bytes 10..11 zijn opvulling). Omdat dat een eigenschap van DEZE Wine-bouw is, controleert DllMain
 * het vooraf: komt het begin niet overeen of is de opvulling geen 0x90/0xcc, dan wordt er NIET
 * gehaakt en zegt het logboek waarom.
 *
 * ABI (Windows x64)
 *   - De handler wordt met `jmp` bereikt, dus de stapel is exact zoals bij de ingang van het echte
 *     ExitProcess: rsp is 8 mod 16, [rsp] is het terugkeeradres van de aanroeper, 32 bytes
 *     schaduwruimte staat boven dat adres. Een gewone C-functie (WINAPI = de x64-conventie) verwacht
 *     precies dat. `return` gaat dus naar de aanroeper; niets in de handler verandert de stapel.
 *   - rcx is het argument. Het echte ExitProcess leest alleen ecx (UINT); de bovenste helft van rcx is
 *     dan ongedefinieerd. Daarom neemt de handler een ULONG_PTR en logt de VOLLE rcx (0x140415820 was
 *     gemeten); naar RtlExitUserProcess gaat de onderste helft, net als in het origineel.
 *   - Niet-vluchtige registers (rbx, rbp, rdi, rsi, r12-r15) bewaart de compiler zelf.
 *
 * LOGBOEK: een Win32-bestandshandle met FILE_APPEND_DATA en een WriteFile per regel. Dat is
 * ongebufferd (niets blijft in een CRT-buffer hangen als het proces plotseling sterft) en een
 * regel is een enkele atomaire append, ook vanaf meerdere threads. Geen CRT-FILE, geen slot.
 *
 * GEEN CRT. De mingw van de image is UCRT-gebaseerd en een gewone bouw importeert dan zeven
 * api-ms-win-crt-*.dll's. Voor een proef die in de aanhechtingsfase draait is elke extra DLL een
 * variabele die we niet willen: deze bron gebruikt alleen kernel32 (en zoekt ntdll!RtlExitUserProcess
 * met GetProcAddress), maakt zijn regels met een eigen mini-opmaak, en wordt gebouwd met -nostdlib
 * -Wl,-e,DllMain -lkernel32. Roept de compiler tegen de verwachting een memcpy/memset aan, dan faalt
 * het linken luid in plaats van dat er stil een CRT-import bijkomt.
 *
 * Geen threads, geen andere haken.
 *
 * BOUWEN: patches/LuigisMansion/probe/bouw-exit-proef.sh
 */
#if defined(_WIN32)

#include <windows.h>

#define HAAK_LEN   12          /* mov rax,imm64 (10) + jmp rax (2) */
#define SLOT_LEN   16          /* ExitProcess zit in een slot van 16 bytes */

typedef void (NTAPI *RtlExitUserProcess_t)(NTSTATUS);

static HANDLE   G_log = INVALID_HANDLE_VALUE;
static int      G_terug_modus;                 /* 1 = mode terug */
static LONG     G_max = 8;
static volatile LONG G_aantal;                 /* alle aanroepen */
static volatile LONG G_terug;                  /* aanroepen die met een `return` beantwoord zijn */
static ULONG_PTR G_basis;                      /* hoofdmodule */
static ULONG_PTR G_grootte;                    /* SizeOfImage */
static ULONG_PTR G_sec_van, G_sec_tot;         /* [van, tot) van de sectie; 0,0 = niet gevonden */
static char     G_naam[64] = "exe";            /* bestandsnaam van de hoofdmodule */
static RtlExitUserProcess_t G_rtl_exit;

/* ---- kleine hulpjes zonder CRT ---------------------------------------------------------------- */

static int tekst_gelijk(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int geheugen_gelijk(const unsigned char *a, const unsigned char *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static void geheugen_kopieer(unsigned char *naar, const unsigned char *van, unsigned n)
{
    volatile unsigned char *d = naar;      /* volatile: de compiler maakt er geen memcpy-aanroep van */
    unsigned i;
    for (i = 0; i < n; i++) d[i] = van[i];
}

static void tekst_kopieer(char *naar, unsigned groot, const char *van)
{
    unsigned i = 0;
    while (van[i] && i + 1 < groot) { naar[i] = van[i]; i++; }
    naar[i] = 0;
}

/* ---- een logregel: opbouwen in een buffer op de stapel, dan EEN WriteFile --------------------- */

typedef struct { char b[640]; unsigned n; } regel;

__attribute__((noinline)) static void r_s(regel *r, const char *s)
{
    while (*s && r->n < sizeof r->b - 2) r->b[r->n++] = *s++;
}
__attribute__((noinline)) static void r_n(regel *r, const char *s, unsigned max)    /* hoogstens `max` tekens (sectienamen) */
{
    unsigned i;
    for (i = 0; i < max && s[i] && r->n < sizeof r->b - 2; i++) r->b[r->n++] = s[i];
}
__attribute__((noinline)) static void r_x(regel *r, unsigned long long v)           /* 0x... zonder voorloopnullen */
{
    char t[16]; int i = 0;
    r_s(r, "0x");
    do { t[i++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
    while (i) { i--; if (r->n < sizeof r->b - 2) r->b[r->n++] = t[i]; }
}
__attribute__((noinline)) static void r_u(regel *r, unsigned long long v)           /* decimaal */
{
    char t[24]; int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i) { i--; if (r->n < sizeof r->b - 2) r->b[r->n++] = t[i]; }
}
__attribute__((noinline)) static void r_b(regel *r, unsigned char c)                /* twee hexcijfers */
{
    if (r->n + 2 < sizeof r->b - 2) {
        r->b[r->n++] = "0123456789abcdef"[c >> 4];
        r->b[r->n++] = "0123456789abcdef"[c & 15];
    }
}
__attribute__((noinline)) static void r_16(regel *r, const unsigned char *b)        /* 16 bytes als "48 83 ec 28 ..." */
{
    int i;
    for (i = 0; i < 16; i++) { if (i) r_s(r, " "); r_b(r, b[i]); }
}
__attribute__((noinline)) static void r_uit(regel *r)
{
    DWORD geschreven;
    if (G_log == INVALID_HANDLE_VALUE) return;
    r->b[r->n++] = '\n';
    WriteFile(G_log, r->b, r->n, &geschreven, NULL);      /* ongebufferd: wat hier terugkomt staat in het bestand */
}

/* Een regel die uit een of twee vaste stukken bestaat. */
static void meld(const char *a)                          { regel r; r.n = 0; r_s(&r, a); r_uit(&r); }

/* ---------------------------------------------------------------------------------------------- */
/* De handler: bereikt via `jmp`, dus een ingang als van het echte ExitProcess. */

__attribute__((noinline, used))
static void WINAPI rg_exit_handler(ULONG_PTR rcx)
{
    ULONG_PTR ra = (ULONG_PTR)__builtin_return_address(0);
    LONG nr = InterlockedIncrement(&G_aantal);
    int in_exe = (G_basis && ra >= G_basis && ra < G_basis + G_grootte);
    int in_sec = (G_sec_tot && ra >= G_sec_van && ra < G_sec_tot);
    int terug = 0;
    LONG tn = 0;
    regel r;

    if (G_terug_modus && in_sec) {
        tn = InterlockedIncrement(&G_terug);
        if (tn <= G_max)
            terug = 1;
    }

    r.n = 0;
    r_s(&r, "["); r_u(&r, GetTickCount64()); r_s(&r, "] thread "); r_u(&r, GetCurrentThreadId());
    r_s(&r, " ExitProcess #"); r_u(&r, (unsigned long long)nr);
    r_s(&r, ": rcx="); r_x(&r, rcx);
    r_s(&r, " (code="); r_x(&r, (unsigned)rcx);
    r_s(&r, ") terugkeeradres="); r_x(&r, ra);
    r_s(&r, " = ");
    if (in_exe) { r_s(&r, G_naam); r_s(&r, "+"); r_x(&r, ra - G_basis); }
    else        r_s(&r, "(buiten de hoofdmodule)");
    r_s(&r, " in-sectie="); r_s(&r, in_sec ? "ja" : "nee");
    r_s(&r, " -> "); r_s(&r, terug ? "TERUG" : "DOORGELATEN");
    r_uit(&r);

    if (terug) {
        r.n = 0;
        r_s(&r, "   TERUG #"); r_u(&r, (unsigned long long)tn);
        r_s(&r, " (niet beeindigd): ret naar "); r_x(&r, ra);
        r_s(&r, ", hoogstens "); r_u(&r, (unsigned long long)G_max);
        r_uit(&r);
        return;       /* naar de aanroeper; ExitProcess keert normaal niet terug, dus geen rax-contract */
    }
    r.n = 0;
    r_s(&r, "   DOORGELATEN: ntdll!RtlExitUserProcess("); r_x(&r, (unsigned)rcx); r_s(&r, ")");
    r_uit(&r);
    G_rtl_exit((NTSTATUS)(UINT)rcx);
    /* Hoort niet te gebeuren; een proces dat nu nog leeft zou een onzichtbare fout zijn. */
    meld("   RtlExitUserProcess keerde TERUG -- TerminateProcess");
    TerminateProcess(GetCurrentProcess(), (UINT)rcx);
}

/* ---------------------------------------------------------------------------------------------- */

static void rg_zoek_sectie(const char *gevraagd)
{
    const unsigned char *b = (const unsigned char *)G_basis;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)b;
    const IMAGE_NT_HEADERS64 *nt;
    const IMAGE_SECTION_HEADER *sh;
    unsigned i, n, k;
    regel r;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        meld("  !! hoofdmodule heeft geen MZ-kop; sectie niet te zoeken");
        return;
    }
    nt = (const IMAGE_NT_HEADERS64 *)(b + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        meld("  !! hoofdmodule is geen PE32+; sectie niet te zoeken");
        return;
    }
    G_grootte = nt->OptionalHeader.SizeOfImage;
    r.n = 0;
    r_s(&r, "  hoofdmodule: base="); r_x(&r, G_basis);
    r_s(&r, " SizeOfImage="); r_x(&r, G_grootte);
    r_s(&r, " ImageBase(kop)="); r_x(&r, nt->OptionalHeader.ImageBase);
    r_s(&r, " naam="); r_s(&r, G_naam);
    r_uit(&r);

    sh = IMAGE_FIRST_SECTION(nt);
    n = nt->FileHeader.NumberOfSections;
    for (i = 0; i < n; i++) {
        ULONG_PTR van = G_basis + sh[i].VirtualAddress;
        ULONG_PTR len = sh[i].Misc.VirtualSize ? sh[i].Misc.VirtualSize : sh[i].SizeOfRawData;
        r.n = 0;
        r_s(&r, "  sectie "); r_n(&r, (const char *)sh[i].Name, 8);
        r_s(&r, " ["); r_x(&r, van); r_s(&r, ", "); r_x(&r, van + len); r_s(&r, ")");
        r_s(&r, " rva="); r_x(&r, sh[i].VirtualAddress);
        r_s(&r, " vsize="); r_x(&r, sh[i].Misc.VirtualSize);
        r_s(&r, " rawsize="); r_x(&r, sh[i].SizeOfRawData);
        r_uit(&r);
        if (!G_sec_tot) {
            /* naam is 8 bytes, niet noodzakelijk NUL-afgesloten: vergelijk tot 8 tekens */
            int gelijk = 1;
            for (k = 0; k < 8; k++) {
                char g = gevraagd[k], h = (char)sh[i].Name[k];
                if (g != h) { gelijk = 0; break; }
                if (!g) break;
            }
            if (gelijk) {
                G_sec_van = van;
                G_sec_tot = van + len;
            }
        }
    }
    r.n = 0;
    if (G_sec_tot) {
        r_s(&r, "  SECTIE "); r_s(&r, gevraagd);
        r_s(&r, ": ["); r_x(&r, G_sec_van); r_s(&r, ", "); r_x(&r, G_sec_tot); r_s(&r, ")");
    } else {
        r_s(&r, "  !!!! SECTIE "); r_s(&r, gevraagd); r_s(&r, " NIET GEVONDEN in ");
        r_u(&r, n); r_s(&r, " secties -- gedraagt zich als mode `log`");
    }
    r_uit(&r);
}

static void rg_installeer(void)
{
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    unsigned char *f;
    unsigned char voor[SLOT_LEN], na[SLOT_LEN];
    DWORD oud, dummy;
    unsigned char haak[HAAK_LEN];
    ULONG_PTR doel = (ULONG_PTR)&rg_exit_handler;
    int i;
    regel r;

    G_rtl_exit = nt ? (RtlExitUserProcess_t)(void *)GetProcAddress(nt, "RtlExitUserProcess") : NULL;
    f = k32 ? (unsigned char *)(void *)GetProcAddress(k32, "ExitProcess") : NULL;
    if (!G_rtl_exit || !f) {
        r.n = 0;
        r_s(&r, "  !!!! GEEN HAAK: ntdll!RtlExitUserProcess="); r_x(&r, (ULONG_PTR)G_rtl_exit);
        r_s(&r, " kernel32!ExitProcess="); r_x(&r, (ULONG_PTR)f); r_s(&r, " niet te vinden");
        r_uit(&r);
        return;
    }
    r.n = 0;
    r_s(&r, "  kernel32!ExitProcess op "); r_x(&r, (ULONG_PTR)f);
    r_s(&r, "   ntdll!RtlExitUserProcess op "); r_x(&r, (ULONG_PTR)G_rtl_exit);
    r_s(&r, "   handler op "); r_x(&r, doel);
    r_uit(&r);
    geheugen_kopieer(voor, f, SLOT_LEN);
    r.n = 0;
    r_s(&r, "  ExitProcess eerste 16 bytes VOOR het haken: "); r_16(&r, voor);
    r_uit(&r);

    /* Past het? 48 83 ec 28 ff 15 = het verwachte begin; bytes 10..15 moeten opvulling zijn. */
    {
        static const unsigned char begin[6] = { 0x48, 0x83, 0xec, 0x28, 0xff, 0x15 };
        int opvulling = 1;
        for (i = HAAK_LEN - 2; i < SLOT_LEN; i++)
            if (voor[i] != 0x90 && voor[i] != 0xcc)
                opvulling = 0;
        if (!geheugen_gelijk(voor, begin, sizeof begin) || !opvulling) {
            r.n = 0;
            r_s(&r, "  !!!! GEEN HAAK: het begin van ExitProcess is niet het verwachte (48 83 ec 28 ff 15 ...) "
                    "of de bytes 10..15 zijn geen opvulling (opvulling=");
            r_u(&r, (unsigned)opvulling);
            r_s(&r, "); 12 bytes zouden een buurfunctie of een eerdere haak raken");
            r_uit(&r);
            return;
        }
    }

    haak[0] = 0x48; haak[1] = 0xb8;                       /* mov rax, imm64 */
    for (i = 0; i < 8; i++) haak[2 + i] = (unsigned char)(doel >> (8 * i));
    haak[10] = 0xff; haak[11] = 0xe0;                     /* jmp rax */

    if (!VirtualProtect(f, SLOT_LEN, PAGE_EXECUTE_READWRITE, &oud)) {
        r.n = 0;
        r_s(&r, "  !!!! GEEN HAAK: VirtualProtect(RWX) mislukt, fout "); r_u(&r, GetLastError());
        r_uit(&r);
        return;
    }
    geheugen_kopieer(f, haak, HAAK_LEN);
    VirtualProtect(f, SLOT_LEN, oud, &dummy);
    FlushInstructionCache(GetCurrentProcess(), f, SLOT_LEN);

    geheugen_kopieer(na, f, SLOT_LEN);
    r.n = 0;
    r_s(&r, "  ExitProcess eerste 16 bytes NA het haken:  "); r_16(&r, na);
    r_s(&r, geheugen_gelijk(na, haak, HAAK_LEN) ? "  (haak staat)" : "  (!!!! WIJKT AF VAN WAT GESCHREVEN IS)");
    r_uit(&r);
}

static void rg_start(void)
{
    char pad[MAX_PATH], modus[32], sec[32], max[32], vol[MAX_PATH];
    HMODULE exe = GetModuleHandleW(NULL);                /* het spel, niet deze DLL */
    DWORD n;
    regel r;

    n = GetEnvironmentVariableA("RG_EXITPROEF_LOG", pad, sizeof pad);
    if (n == 0 || n >= sizeof pad)
        tekst_kopieer(pad, sizeof pad, "rg-exit-proef.log");
    G_log = CreateFileA(pad, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (G_log == INVALID_HANDLE_VALUE) {
        /* Uitwijkpad, zoals rg-pe-hook: een bekende plek als de werkmap niet schrijfbaar is. */
        G_log = CreateFileA("C:\\rg-exit-proef.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (G_log == INVALID_HANDLE_VALUE)
            return;
    }

    n = GetEnvironmentVariableA("RG_EXITPROEF_MODE", modus, sizeof modus);
    if (n == 0 || n >= sizeof modus)
        tekst_kopieer(modus, sizeof modus, "log");
    G_terug_modus = tekst_gelijk(modus, "terug");
    n = GetEnvironmentVariableA("RG_EXITPROEF_SECTIE", sec, sizeof sec);
    if (n == 0 || n >= sizeof sec)
        tekst_kopieer(sec, sizeof sec, ".capcom0");
    n = GetEnvironmentVariableA("RG_EXITPROEF_MAX", max, sizeof max);
    if (n > 0 && n < sizeof max) {
        LONG v = 0; const char *p = max;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        G_max = v;
    }

    G_basis = (ULONG_PTR)exe;
    n = GetModuleFileNameA(exe, vol, sizeof vol);
    if (n > 0 && n < sizeof vol) {
        const char *s = vol, *p;
        for (p = vol; *p; p++)
            if (*p == '\\' || *p == '/') s = p + 1;
        tekst_kopieer(G_naam, sizeof G_naam, s);
    }

    meld("==================== rg-exit-proef (run) ====================");
    r.n = 0;
    r_s(&r, "pid="); r_u(&r, GetCurrentProcessId());
    r_s(&r, " GetTickCount64="); r_u(&r, GetTickCount64());
    r_s(&r, " mode="); r_s(&r, G_terug_modus ? "terug" : "log");
    if (!G_terug_modus && !tekst_gelijk(modus, "log")) {
        r_s(&r, " (onbekende waarde `"); r_s(&r, modus); r_s(&r, "` -> log)");
    }
    r_s(&r, " sectie="); r_s(&r, sec);
    r_s(&r, " max="); r_u(&r, (unsigned long long)G_max);
    r_s(&r, " log="); r_s(&r, pad);
    r_uit(&r);
    if (!exe) {
        meld("  !!!! geen hoofdmodule (GetModuleHandleW(NULL) gaf NULL) -- GEEN HAAK");
        return;
    }
    rg_zoek_sectie(sec);
    if (!G_sec_tot)
        G_terug_modus = 0;                    /* luid gemeld; zonder sectie kunnen we geen onderscheid maken */
    rg_installeer();
    r.n = 0;
    r_s(&r, "klaar: mode effectief="); r_s(&r, G_terug_modus ? "terug" : "log");
    r_uit(&r);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst;
    if (reason == DLL_PROCESS_ATTACH) {
        rg_start();
    } else if (reason == DLL_PROCESS_DETACH) {
        /* reserved != NULL: het proces eindigt (geen FreeLibrary). Bewijst een nette afsluiting. */
        regel r;
        r.n = 0;
        r_s(&r, "["); r_u(&r, GetTickCount64()); r_s(&r, "] DLL_PROCESS_DETACH (");
        r_s(&r, reserved ? "proceseinde" : "FreeLibrary");
        r_s(&r, "), ExitProcess-aanroepen="); r_u(&r, (unsigned long long)G_aantal);
        r_s(&r, ", terug="); r_u(&r, (unsigned long long)G_terug);
        r_uit(&r);
    }
    return TRUE;
}

/* Dwingt een .reloc af, zodat de DLL verplaatsbaar is (dezelfde les als rg_pe_hook_dll.c). */
void *const rg_exit_proef_ankers[2] = { (void *)&DllMain, (void *)&rg_exit_proef_ankers };

#endif /* _WIN32 */

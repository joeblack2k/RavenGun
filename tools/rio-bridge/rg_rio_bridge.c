/* RavenGun: invoerbrug voor het RIO-lightgun-bord van native Raw Thrills-games op Unity (NERF Arcade).
 *
 * Probleem: de game laadt zijn eigen bibliotheek voor het bord (librio.so, met dlopen door Unity/Mono) en leest daaruit de
 * kastschakelaars en de lightguns: RIO_SW_Count(id) geeft per schakelaar een teller van overgangen, RIO_ADC_Val(kanaal) en
 * RIO_ADC_Val_Ex(kanaal, &delta) geven de stand van een lightgun-as. Zonder bord leest de bibliotheek niets (geen USB-apparaat
 * 0c70:0780) en meldt RIO_Connected "niet verbonden". Een gewone muis of een virtueel lichtgeweer helpt niet: de game vraagt
 * het bord, niet de invoerapparaten van het systeem. LD_PRELOAD kan de functies niet vervangen: Mono zoekt ze op in de handle
 * van librio.so zelf, niet in de globale symbolen.
 *
 * Deze bibliotheek is invoer en niets anders. Zij leest het bordbeeld van de daemon (de IO-brug, route tp-shm: 64 bytes, een
 * 32-bits bitveld met de knoppen en een byte per as; standaard /dev/shm/ravengun_JvsState) en laat de genoemde functies van
 * librio.so de waarden uit dat beeld teruggeven. Dat gebeurt in het geheugen van de game, direct nadat de game librio.so heeft
 * geladen (dlopen wordt onderschept; een zoeker in een eigen thread vangt een lading die daarbuiten om gaat). Het bestand op
 * de NAS blijft onaangeroerd. De hulp emuleert geen USB-apparaat, geen dongle en omzeilt geen controle van de game.
 *
 * WAT waarheen gaat staat in een TABEL per game (patches/<Game>/rio-invoer.ini), niet in deze code. Een regel per ding,
 * `soort sleutel=waarde ...`; # of ; begint commentaar:
 *
 *   bibliotheek naam=librio.so                         welke geladen bibliotheek (pad eindigt op /naam) aangepast wordt
 *   proces      naam=Nerf.x86_64                       alleen in dit proces (basisnaam van /proc/self/exe); weglaten = overal
 *   beeld       bits=8                                 waar het 32-bits bitveld in het bordbeeld staat (standaard 8)
 *   functie     naam=RIO_SW_Count rol=schakelaar sig=8d47dc83f8277738
 *               rol: verbonden (geeft 0 = verbonden), schakelaar (teller per id), adc (waarde per kanaal),
 *               adc_ex (waarde per kanaal + delta via de tweede parameter). sig: de eerste bytes van de functie in DEZE build.
 *               Kloppen de bytes niet, dan wordt die bibliotheek in zijn geheel NIET aangepast (melding op stderr).
 *   schakelaar  bit=7 id=7 [min=300]                   bit uit het beeld -> schakelaar id; meer bits op een id = OF. min = minimale
 *               druktijd in ms: een kortere druk blijft zo lang staan (gemeten op NERF: START van 0,12 s telt niet, 0,35 s wel)
 *   kanaal      bron=12 ch=19 min=0 max=255 inverteer=nee
 *               asbyte bron (0..255 uit het beeld, links/boven = 0) -> kanaal ch: round(min + f*(max-min)), daarna
 *               eventueel inverteren (min + max - v). Zonder beeld staat een kanaal in het midden.
 *
 * Een schakelaar is in dit bord een teller van overgangen (gemeten, patches/NERF/bewijs-rio-20261014/inputs-map-20261014):
 * indrukken maakt de teller oneven, loslaten weer even; de game leest zowel de stand (oneven = ingedrukt) als de stappen.
 * De hulp telt in haar eigen thread (elke milliseconde een blik op het beeld), zodat ook een korte druk een stap geeft.
 *
 * TWEEDE AANHECHTING: HET BORD ZELF, OVER USB (Jurassic Park). Een game kan het bord ook als gebeurtenissen nemen: dan
 * zit de RIO-bibliotheek in het spelbeeld zelf, opent een eigen leesdraad het USB-apparaat (pmrt_usb_open(0x0c70, 0x0780)),
 * leest 64-bytes-rapporten (pmrt_usb_interrupt_read), ontsleutelt ze met de eigen XTEA-variant van de bibliotheek en zet
 * elke verandering als gebeurtenis in een wachtrij; gepold wordt er niets (gemeten 2026-10-14,
 * patches/JurassicPark/bewijs-bord-20261014/). Daar antwoordt deze hulp als het BORD: de vier pmrt_usb-functies van het
 * spelbeeld krijgen een sprong naar de hulp, het bord 0c70:0780 wordt een eigen handvat, en elke lezing krijgt op het
 * ritme van het bord een rapport type 2 uit dezelfde tellers en kanalen als hierboven, met de controlesom en versleuteld
 * met de EIGEN functie van de game (die het ook ontsleutelt; de hulp toetst bij de start dat heen en terug klopt). Alle
 * andere apparaten (kaartlezers, het UMC-bord) gaan via een trampoline naar de echte functies. Regels in de tabel:
 *
 *   usb         vid=0c70 pid=0780 interval=4 type=2 lengte=64 som=63      het bord, ms per rapport, rapporttype, controlesom-byte
 *   haak        naam=pmrt_usb_open rol=open adres=0845f595 sig=5589e583ec68
 *               rol: open, lees, schrijf, sluit. adres: in het spelbeeld. sig: de eerste HELE instructies (samen minstens
 *               5 bytes); die worden in de trampoline uitgevoerd. Pas als ALLE haken en de versleuteling hun bytes hebben
 *               (het beeld is dan door de lader gezet) wordt er iets veranderd; anders niets.
 *   versleuteling adres=083715dd sig=5589e5575653 sleutel=08b26d5c       xrtea(buf, +1 versleutelen / -1 ontsleutelen, sleutel)
 *   versleuteling geen                                                   een OUDERE bibliotheek zonder xrtea (Aliens Armageddon,
 *               Walking Dead: RIO_GetReport leest het rapport en geeft het onversleuteld door; gemeten met objdump, er is
 *               geen aanroep na pmrt_usb_interrupt_read en de XTEA-sleutel van JP/NERF staat niet in het beeld). Dan gaat het
 *               rapport zoals het is; zonder een `versleuteling`-regel wordt een usb-bord nog steeds afgekeurd.
 *   veld       id=7 byte=8 deel=hoog                                    schakelaar id -> 4-bits teller in een nibble van het rapport
 *   adcveld     ch=19 hoog=14 deel=hoog laag=16                          12-bits kanaal: hoge 4 bits in een nibble, lage 8 in een byte
 *
 * Gebruik: LD_PRELOAD=/pad/librg_rio_bridge.so, en de daemon met de IO-brug (ravengunctl iobridge start <Game>).
 * Omgeving: RG_RIO_TABLE=pad (de tabel; verplicht), RG_RIO_SHM=pad (standaard /dev/shm/ravengun_JvsState),
 *           RG_RIO_LOG=1 (meldingen en elke 5 s een regel met beeld, tellers, kanalen en het aantal aanroepen op stderr).
 * Onderzoek: RG_RIO_PROBE=pad (bestand met regels "bit id", live gelezen: extra schakelaars bovenop de tabel; om een
 *           onbekende knop te vinden zonder de game opnieuw te starten).
 * Eigen code.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define RG_BEELD 64
#define RG_MAX_ID 128
#define RG_MAX_CH 64
#define RG_MAX_SW 64
#define RG_MAX_KANAAL 16
#define RG_MAX_FUNCTIE 8
#define RG_MAX_KOPIE 8

enum { ROL_VERBONDEN, ROL_SCHAKELAAR, ROL_ADC, ROL_ADC_EX };

struct rg_functie { char naam[64]; int rol; unsigned char sig[16]; int nsig; };
struct rg_sw { int bit; int id; int min_ms; };
struct rg_kanaal { int bron; int ch; double min, max; int inverteer; };

static struct {
    char bib[64], proces[64];
    int beeld_bits;
    struct rg_functie f[RG_MAX_FUNCTIE]; int nf;
    struct rg_sw sw[RG_MAX_SW]; int nsw;
    struct rg_kanaal k[RG_MAX_KANAAL]; int nk;
} T;

enum { HAAK_OPEN, HAAK_LEES, HAAK_SCHRIJF, HAAK_SLUIT, HAAK_N };
struct rg_haak { int gezet; uintptr_t adres; unsigned char sig[16]; int nsig; void *trampoline; };
struct rg_veld { int id, byte, hoog; };
struct rg_adcveld { int ch, hbyte, hhoog, lbyte; };
static struct {
    int aan, vid, pid, interval, type, lengte, som;
    struct rg_haak h[HAAK_N];
    uintptr_t xrtea, sleutel; unsigned char xsig[16]; int nxsig;
    int zonder;                     /* `versleuteling geen`: de bibliotheek van de game versleutelt niet */
    struct rg_veld v[RG_MAX_SW]; int nv;
    struct rg_adcveld a[RG_MAX_KANAAL]; int na;
} U;
static _Atomic int usb_klaar;
static _Atomic uint32_t n_open, n_lees, n_schrijf, n_sluit;

static int actief, log_on;
static _Atomic uint32_t teller[RG_MAX_ID];
static int minhold[RG_MAX_ID];          /* minimale druktijd per id (ms), uit de tabel */
static uint64_t druk_ms[RG_MAX_ID];      /* wanneer de huidige druk begon */
static _Atomic int32_t kwaarde[RG_MAX_CH], kvorig[RG_MAX_CH];
static _Atomic uint64_t n_sw, n_adc, n_adcex, n_verb;
static _Atomic uint32_t laatste_bits;
static _Atomic int beeld_ok;

static pthread_mutex_t patch_mu = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t gedaan[RG_MAX_KOPIE]; static int ngedaan;
static void *(*echte_dlopen)(const char *, int);

static void meld(const char *s) { if (log_on) { fprintf(stderr, "[rg_rio_bridge] %s\n", s); fflush(stderr); } }

/* --- de vervangers: dit is wat de game aanroept --------------------------------------------------------------------- */
static int rg_verbonden(void) { atomic_fetch_add(&n_verb, 1); return 0; }
static uint32_t rg_sw_count(int id) {
    atomic_fetch_add(&n_sw, 1);
    return (id >= 0 && id < RG_MAX_ID) ? atomic_load(&teller[id]) : 0;
}
static int rg_adc(int ch) {
    atomic_fetch_add(&n_adc, 1);
    return (ch >= 0 && ch < RG_MAX_CH) ? atomic_load(&kwaarde[ch]) : 0;
}
static int rg_adc_ex(int ch, int *delta) {
    atomic_fetch_add(&n_adcex, 1);
    if (ch < 0 || ch >= RG_MAX_CH) { if (delta) *delta = 0; return 0; }
    int v = atomic_load(&kwaarde[ch]);
    if (delta) *delta = v - atomic_load(&kvorig[ch]);
    return v;
}
static uintptr_t vervanger(int rol) {   /* als getal: geen omzetting functie- naar objectpointer */
    switch (rol) {
    case ROL_VERBONDEN: return (uintptr_t)rg_verbonden;
    case ROL_SCHAKELAAR: return (uintptr_t)rg_sw_count;
    case ROL_ADC: return (uintptr_t)rg_adc;
    default: return (uintptr_t)rg_adc_ex;
    }
}

/* --- de tabel ------------------------------------------------------------------------------------------------------- */
static int hexbytes(const char *s, unsigned char *out, int max) {
    int n = 0;
    while (s[0] && s[1] && n < max) {
        if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) return -1;
        char b[3] = {s[0], s[1], 0}; out[n++] = (unsigned char)strtoul(b, 0, 16); s += 2;
    }
    return s[0] ? -1 : n;
}
static const char *waarde(char **kv, int nkv, const char *sleutel) {
    size_t l = strlen(sleutel);
    for (int i = 0; i < nkv; i++) if (!strncmp(kv[i], sleutel, l) && kv[i][l] == '=') return kv[i] + l + 1;
    return 0;
}
static int getal(const char *s, double *out) { if (!s || !*s) return 0; char *e; *out = strtod(s, &e); return *e == 0; }

static int lees_tabel(const char *pad) {
    FILE *f = fopen(pad, "r");
    if (!f) { meld("geen tabel (RG_RIO_TABLE): de hulp doet niets"); return 0; }
    memset(&T, 0, sizeof T); T.beeld_bits = 8; memset(&U, 0, sizeof U); memset(minhold, 0, sizeof minhold);
    char regel[512]; int nr = 0, fout = 0;
    while (fgets(regel, sizeof regel, f)) {
        nr++;
        char *c = strpbrk(regel, "#;"); if (c) *c = 0;
        char *tok[24]; int nt = 0;
        for (char *p = strtok(regel, " \t\r\n"); p && nt < 24; p = strtok(0, " \t\r\n")) tok[nt++] = p;
        if (!nt) continue;
        char **kv = tok + 1; int nkv = nt - 1; double a, b;
        const char *s = tok[0];
        if (!strcmp(s, "bibliotheek")) { const char *n = waarde(kv, nkv, "naam"); if (!n) fout = nr; else snprintf(T.bib, sizeof T.bib, "%s", n); }
        else if (!strcmp(s, "proces")) { const char *n = waarde(kv, nkv, "naam"); if (!n) fout = nr; else snprintf(T.proces, sizeof T.proces, "%s", n); }
        else if (!strcmp(s, "beeld")) { if (!getal(waarde(kv, nkv, "bits"), &a) || a < 0 || a > RG_BEELD - 4) fout = nr; else T.beeld_bits = (int)a; }
        else if (!strcmp(s, "functie") && T.nf < RG_MAX_FUNCTIE) {
            struct rg_functie *fu = &T.f[T.nf];
            const char *n = waarde(kv, nkv, "naam"), *r = waarde(kv, nkv, "rol"), *g = waarde(kv, nkv, "sig");
            if (!n || !r || !g) { fout = nr; continue; }
            snprintf(fu->naam, sizeof fu->naam, "%s", n);
            if (!strcmp(r, "verbonden")) fu->rol = ROL_VERBONDEN; else if (!strcmp(r, "schakelaar")) fu->rol = ROL_SCHAKELAAR;
            else if (!strcmp(r, "adc")) fu->rol = ROL_ADC; else if (!strcmp(r, "adc_ex")) fu->rol = ROL_ADC_EX; else { fout = nr; continue; }
            fu->nsig = hexbytes(g, fu->sig, sizeof fu->sig);
            if (fu->nsig < 4) { fout = nr; continue; }
            T.nf++;
        } else if (!strcmp(s, "schakelaar") && T.nsw < RG_MAX_SW) {
            if (!getal(waarde(kv, nkv, "bit"), &a) || !getal(waarde(kv, nkv, "id"), &b) || a < 0 || a > 31 || b < 0 || b >= RG_MAX_ID) { fout = nr; continue; }
            double mn = 0;
            if (waarde(kv, nkv, "min") && (!getal(waarde(kv, nkv, "min"), &mn) || mn < 0 || mn > 5000)) { fout = nr; continue; }
            T.sw[T.nsw].bit = (int)a; T.sw[T.nsw].id = (int)b; T.sw[T.nsw].min_ms = (int)mn; T.nsw++;
            if ((int)mn > minhold[(int)b]) minhold[(int)b] = (int)mn;
        } else if (!strcmp(s, "kanaal") && T.nk < RG_MAX_KANAAL) {
            struct rg_kanaal *k = &T.k[T.nk]; double ch;
            const char *inv = waarde(kv, nkv, "inverteer");
            if (!getal(waarde(kv, nkv, "bron"), &a) || !getal(waarde(kv, nkv, "ch"), &ch) || !getal(waarde(kv, nkv, "min"), &k->min) ||
                !getal(waarde(kv, nkv, "max"), &k->max) || a < 0 || a >= RG_BEELD || ch < 0 || ch >= RG_MAX_CH) { fout = nr; continue; }
            k->bron = (int)a; k->ch = (int)ch; k->inverteer = inv && (!strcmp(inv, "ja") || !strcmp(inv, "1"));
            T.nk++;
        } else if (!strcmp(s, "usb")) {
            const char *v = waarde(kv, nkv, "vid"), *p = waarde(kv, nkv, "pid");
            if (!v || !p) { fout = nr; continue; }
            U.vid = (int)strtol(v, 0, 16); U.pid = (int)strtol(p, 0, 16);
            U.interval = getal(waarde(kv, nkv, "interval"), &a) && a >= 1 && a <= 100 ? (int)a : 4;
            U.type = getal(waarde(kv, nkv, "type"), &a) ? (int)a : 2;
            U.lengte = getal(waarde(kv, nkv, "lengte"), &a) && a >= 8 && a <= 64 ? (int)a : 64;
            U.som = getal(waarde(kv, nkv, "som"), &a) && a >= 1 && a < U.lengte ? (int)a : U.lengte - 1;
            U.aan = 1;
        } else if (!strcmp(s, "haak")) {
            const char *r = waarde(kv, nkv, "rol"), *ad = waarde(kv, nkv, "adres"), *g = waarde(kv, nkv, "sig");
            int rol = !r ? -1 : !strcmp(r, "open") ? HAAK_OPEN : !strcmp(r, "lees") ? HAAK_LEES : !strcmp(r, "schrijf") ? HAAK_SCHRIJF
                    : !strcmp(r, "sluit") ? HAAK_SLUIT : -1;
            if (rol < 0 || !ad || !g) { fout = nr; continue; }
            struct rg_haak *h = &U.h[rol];
            h->adres = (uintptr_t)strtoul(ad, 0, 16); h->nsig = hexbytes(g, h->sig, sizeof h->sig);
            if (!h->adres || h->nsig < 5) { fout = nr; continue; }
            h->gezet = 1;
        } else if (!strcmp(s, "versleuteling") && nkv == 1 && !strcmp(kv[0], "geen")) {
            U.zonder = 1; U.xrtea = 0; U.nxsig = 0;
        } else if (!strcmp(s, "versleuteling")) {
            const char *ad = waarde(kv, nkv, "adres"), *g = waarde(kv, nkv, "sig"), *k = waarde(kv, nkv, "sleutel");
            if (!ad || !g || !k) { fout = nr; continue; }
            U.xrtea = (uintptr_t)strtoul(ad, 0, 16); U.sleutel = (uintptr_t)strtoul(k, 0, 16); U.nxsig = hexbytes(g, U.xsig, sizeof U.xsig);
            if (!U.xrtea || !U.sleutel || U.nxsig < 4) { fout = nr; continue; }
            U.zonder = 0;
        } else if (!strcmp(s, "veld") && U.nv < RG_MAX_SW) {
            const char *d = waarde(kv, nkv, "deel");
            if (!getal(waarde(kv, nkv, "id"), &a) || !getal(waarde(kv, nkv, "byte"), &b) || !d || a < 0 || a >= RG_MAX_ID || b < 0 || b >= 64) { fout = nr; continue; }
            U.v[U.nv].id = (int)a; U.v[U.nv].byte = (int)b; U.v[U.nv].hoog = !strcmp(d, "hoog"); U.nv++;
        } else if (!strcmp(s, "adcveld") && U.na < RG_MAX_KANAAL) {
            double h, l; const char *d = waarde(kv, nkv, "deel");
            if (!getal(waarde(kv, nkv, "ch"), &a) || !getal(waarde(kv, nkv, "hoog"), &h) || !getal(waarde(kv, nkv, "laag"), &l) || !d ||
                a < 0 || a >= RG_MAX_CH || h < 0 || h >= 64 || l < 0 || l >= 64) { fout = nr; continue; }
            U.a[U.na].ch = (int)a; U.a[U.na].hbyte = (int)h; U.a[U.na].hhoog = !strcmp(d, "hoog"); U.a[U.na].lbyte = (int)l; U.na++;
        } else fout = nr;
    }
    fclose(f);
    char b[200];
    int usb_ok = U.aan && U.h[HAAK_OPEN].gezet && U.h[HAAK_LEES].gezet && U.h[HAAK_SCHRIJF].gezet && U.h[HAAK_SLUIT].gezet && (U.xrtea || U.zonder);
    if (fout || (U.aan ? !usb_ok : (!T.bib[0] || !T.nf))) {
        snprintf(b, sizeof b, "tabel %s afgekeurd (regel %d, of geen bibliotheek/functie, of een usb-bord zonder vier haken en versleuteling): "
                 "de hulp doet niets", pad, fout); meld(b);
        U.aan = 0;
        return 0;
    }
    return 1;
}

/* --- aanpassen van een geladen librio.so ---------------------------------------------------------------------------- */
static int is_bib(const char *pad) {
    if (!pad || !T.bib[0]) return 0;
    size_t lp = strlen(pad), lb = strlen(T.bib);
    return lp >= lb && !strcmp(pad + lp - lb, T.bib) && (lp == lb || pad[lp - lb - 1] == '/');
}
static void schrijf_sprong(void *waar, uintptr_t naar) {
#if defined(__i386__)
    unsigned char code[5] = {0xe9};                               /* jmp rel32 */
    int32_t rel = (int32_t)(naar - ((uintptr_t)waar + 5)); memcpy(code + 1, &rel, 4);
#else
    unsigned char code[14] = {0xff, 0x25, 0, 0, 0, 0};          /* jmp [rip+0] ; .quad naar */
    memcpy(code + 6, &naar, 8);
#endif
    long pg = sysconf(_SC_PAGESIZE);
    uintptr_t begin = (uintptr_t)waar & ~(uintptr_t)(pg - 1), eind = ((uintptr_t)waar + sizeof code + pg - 1) & ~(uintptr_t)(pg - 1);
    mprotect((void *)begin, eind - begin, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy(waar, code, sizeof code);
    mprotect((void *)begin, eind - begin, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)waar, (char *)waar + sizeof code);
}
/* Past de bibliotheek achter handle h aan. Alles of niets: klopt een handtekening niet, dan blijft de hele kopie zoals hij was. */
static void pas_aan(void *h, const char *pad) {
    void *adres[RG_MAX_FUNCTIE]; char b[300];
    Dl_info di;
    if (!h) return;
    void *eerste = dlsym(h, T.f[0].naam);
    if (!eerste || !dladdr(eerste, &di)) return;
    pthread_mutex_lock(&patch_mu);
    for (int i = 0; i < ngedaan; i++) if (gedaan[i] == (uintptr_t)di.dli_fbase) { pthread_mutex_unlock(&patch_mu); return; }
    for (int i = 0; i < T.nf; i++) {
        adres[i] = dlsym(h, T.f[i].naam);
        if (!adres[i] || memcmp(adres[i], T.f[i].sig, T.f[i].nsig) != 0) {
            snprintf(b, sizeof b, "%s: %s %s -- deze kopie wordt NIET aangepast", pad, T.f[i].naam,
                     adres[i] ? "heeft andere bytes dan de tabel (andere build?)" : "niet gevonden");
            meld(b);
            if (ngedaan < RG_MAX_KOPIE) gedaan[ngedaan++] = (uintptr_t)di.dli_fbase;   /* niet opnieuw proberen */
            pthread_mutex_unlock(&patch_mu); return;
        }
    }
    for (int i = 0; i < T.nf; i++) schrijf_sprong(adres[i], vervanger(T.f[i].rol));
    if (ngedaan < RG_MAX_KOPIE) gedaan[ngedaan++] = (uintptr_t)di.dli_fbase;
    pthread_mutex_unlock(&patch_mu);
    snprintf(b, sizeof b, "aangepast: %s (basis %p): %d functies", pad, di.dli_fbase, T.nf); meld(b);
}

static void zoek_echte_dlopen(void) {
    void *p = dlsym(RTLD_NEXT, "dlopen");          /* via memcpy: POSIX' manier om een dlsym-resultaat als functie te nemen */
    memcpy(&echte_dlopen, &p, sizeof p);
}

/* dlopen onderscheppen: de aanpassing gebeurt voordat de game de handle terugkrijgt. */
__attribute__((visibility("default"))) void *dlopen(const char *bestand, int modus) {
    if (!echte_dlopen) zoek_echte_dlopen();
    void *h = echte_dlopen(bestand, modus);
    if (h && actief && is_bib(bestand)) pas_aan(h, bestand);
    return h;
}

/* De zoeker: een lading die niet via onze dlopen ging (of al gebeurd was) alsnog vinden. */
struct gevonden { char pad[RG_MAX_KOPIE][512]; int n; };
static int zoek_cb(struct dl_phdr_info *info, size_t sz, void *arg) {
    (void)sz; struct gevonden *g = arg;
    if (g->n < RG_MAX_KOPIE && is_bib(info->dlpi_name)) snprintf(g->pad[g->n++], 512, "%s", info->dlpi_name);
    return 0;
}
static void zoek_en_pas_aan(void) {
    struct gevonden g = {0};
    dl_iterate_phdr(zoek_cb, &g);                  /* eerst verzamelen: dlopen binnen de callback kan vastlopen */
    if (!echte_dlopen) zoek_echte_dlopen();
    for (int i = 0; i < g.n; i++) {
        void *h = echte_dlopen(g.pad[i], RTLD_NOW | RTLD_NOLOAD);
        if (h) { pas_aan(h, g.pad[i]); dlclose(h); }
    }
}

/* --- de USB-aanhechting: het bord zelf ------------------------------------------------------------------------------ */
/* Een functie van het spel met tot vijf argumenten (cdecl); wie er minder kreeg, leest hier wat er toevallig op de stapel
   van de aanroeper staat, en geeft dat ongebruikt door. */
typedef intptr_t (*rg_fn5)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
typedef void (*rg_xrtea)(void *, int, void *);
static unsigned char nep_dev[256];                         /* het handvat van ons bord; de bibliotheek kijkt er niet in */
static int memfd = -1;

static void xrtea(void *buf, int richting) {
    if (U.zonder) return;                                  /* `versleuteling geen`: het rapport gaat zoals het is */
    rg_xrtea f; uintptr_t a = U.xrtea; memcpy(&f, &a, sizeof f);
    f(buf, richting, (void *)U.sleutel);
}
static rg_fn5 echt(int rol) { rg_fn5 f; memcpy(&f, &U.h[rol].trampoline, sizeof f); return f; }

static uint64_t nu_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }

/* De versleuteling van de game zelf: heen en terug moet het origineel geven, en heen moet iets veranderen. Pas getoetst bij de
   eerste opening van het bord: dan draait de game. Eerder (in de kijker-thread, zodra de bytes kloppen) kan de lader het beeld
   nog niet uitvoerbaar gemaakt hebben (gemeten: exit 139 toen deze toets daar stond). */
static int toets_versleuteling(void) {
    static int uitslag;                                    /* 0 nog niet, 1 goed, -1 fout */
    if (uitslag) return uitslag > 0;
    if (U.zonder) { uitslag = 1; meld("usb: geen versleuteling (tabel: versleuteling geen), rapporten gaan onversleuteld"); return 1; }
    uint8_t p[64], x[64];
    for (int i = 0; i < 64; i++) p[i] = (uint8_t)(i * 37 + 11);
    memcpy(x, p, 64); xrtea(x, 1);
    int veranderd = memcmp(x, p, 64) != 0;
    xrtea(x, -1);
    uitslag = veranderd && memcmp(x, p, 64) == 0 ? 1 : -1;
    meld(uitslag > 0 ? "usb: versleuteling van de game heen en terug in orde" : "usb: de versleuteling van de game klopt niet heen en terug -- het bord wordt NIET nagedaan");
    return uitslag > 0;
}

static intptr_t usb_open(intptr_t vid, intptr_t pid, intptr_t c, intptr_t d, intptr_t e) {
    if ((vid & 0xffff) == U.vid && (pid & 0xffff) == U.pid && toets_versleuteling()) {
        if (atomic_fetch_add(&n_open, 1) < 8) { char b[100]; snprintf(b, sizeof b, "usb: bord %04x:%04x geopend (ons handvat)", U.vid, U.pid); meld(b); }
        return (intptr_t)nep_dev;
    }
    return echt(HAAK_OPEN)(vid, pid, c, d, e);
}
/* Een lezing van het bord: op het ritme van het bord (de leesdraad van de game wacht op de echte kast ook op het volgende
   rapport), een rapport uit de tellers en kanalen, controlesom, versleuteld. 0 = gelukt, zoals pmrt_usb_interrupt_read. */
static intptr_t usb_lees(intptr_t dev, intptr_t buf, intptr_t len, intptr_t d, intptr_t e) {
    if (dev != (intptr_t)nep_dev) return echt(HAAK_LEES)(dev, buf, len, d, e);
    static uint64_t volgende; static uint16_t volgnr;
    uint64_t nu = nu_us();
    if (!volgende || nu > volgende + 100000) volgende = nu;
    volgende += (uint64_t)U.interval * 1000;
    if (volgende > nu) usleep((useconds_t)(volgende - nu));
    uint8_t r[64] = {0};
    r[0] = (uint8_t)U.type; r[1] = (uint8_t)volgnr; r[2] = (uint8_t)(volgnr >> 8); volgnr++;
    for (int i = 0; i < U.nv; i++) {
        uint8_t nib = (uint8_t)(atomic_load(&teller[U.v[i].id]) & 0xf);
        r[U.v[i].byte] |= U.v[i].hoog ? (uint8_t)(nib << 4) : nib;
    }
    for (int i = 0; i < U.na; i++) {
        int32_t v = atomic_load(&kwaarde[U.a[i].ch]); if (v < 0) v = 0; if (v > 4095) v = 4095;
        uint8_t hoog = (uint8_t)((v >> 8) & 0xf);
        r[U.a[i].hbyte] |= U.a[i].hhoog ? (uint8_t)(hoog << 4) : hoog;
        r[U.a[i].lbyte] = (uint8_t)(v & 0xff);
    }
    uint8_t som = 0; for (int i = 0; i < U.som; i++) som += r[i];
    r[U.som] = som;
    xrtea(r, 1);
    memcpy((void *)buf, r, (size_t)(len > 0 && len < U.lengte ? len : U.lengte));
    atomic_fetch_add(&n_lees, 1);
    return 0;
}
/* Wat de game naar het bord stuurt (lampen, geweermodus, muntteller): aangenomen. De eerste paar ontsleuteld in het logboek. */
static intptr_t usb_schrijf(intptr_t dev, intptr_t buf, intptr_t len, intptr_t d, intptr_t e) {
    if (dev != (intptr_t)nep_dev) return echt(HAAK_SCHRIJF)(dev, buf, len, d, e);
    uint32_t n = atomic_fetch_add(&n_schrijf, 1);
    if (log_on && n < 12 && len > 0) {
        uint8_t k[64] = {0}; memcpy(k, (void *)buf, (size_t)(len < 64 ? len : 64)); xrtea(k, -1);
        char b[200]; int o = snprintf(b, sizeof b, "usb: de game stuurt (%d bytes%s):", (int)len, U.zonder ? "" : ", ontsleuteld");
        for (int i = 0; i < 24 && o < 190; i++) o += snprintf(b + o, sizeof b - o, " %02x", k[i]);
        meld(b);
    }
    return 0;
}
static intptr_t usb_sluit(intptr_t dev, intptr_t b, intptr_t c, intptr_t d, intptr_t e) {
    if (dev != (intptr_t)nep_dev) return echt(HAAK_SLUIT)(dev, b, c, d, e);
    atomic_fetch_add(&n_sluit, 1); meld("usb: de game sluit het bord");
    return 0;
}

static int lees_mem(uintptr_t a, void *b, size_t n) { return memfd >= 0 && pread(memfd, b, n, (off_t)a) == (ssize_t)n; }
/* Haakt aan zodra het spelbeeld er is (de lader zet het pas na deze constructor). Alles of niets. 1 = klaar (gelukt of
   opgegeven), 0 = nog niet. */
static int usb_aanhaken(void) {
    unsigned char b[16];
    if (memfd < 0) memfd = open("/proc/self/mem", O_RDWR | O_CLOEXEC);
    for (int i = 0; i < HAAK_N; i++) if (!lees_mem(U.h[i].adres, b, (size_t)U.h[i].nsig) || memcmp(b, U.h[i].sig, (size_t)U.h[i].nsig)) return 0;
    if (!U.zonder && (!lees_mem(U.xrtea, b, (size_t)U.nxsig) || memcmp(b, U.xsig, (size_t)U.nxsig))) return 0;
    /* Hier wordt geen code van de game uitgevoerd: alleen sprongen geschreven (via /proc/self/mem, dus ongeacht de rechten). */
    unsigned char *tr = mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tr == MAP_FAILED) { meld("usb: geen geheugen voor de trampolines -- het bord wordt NIET nagedaan"); return 1; }
    uintptr_t vervangers[HAAK_N] = {(uintptr_t)usb_open, (uintptr_t)usb_lees, (uintptr_t)usb_schrijf, (uintptr_t)usb_sluit};
    for (int i = 0; i < HAAK_N; i++) {               /* trampoline: de gekopieerde eerste instructies, dan terug in de functie */
        unsigned char *t = tr + 64 * i;
        memcpy(t, U.h[i].sig, (size_t)U.h[i].nsig);
        t[U.h[i].nsig] = 0xe9;
        int32_t rel = (int32_t)((U.h[i].adres + (uintptr_t)U.h[i].nsig) - ((uintptr_t)t + (uintptr_t)U.h[i].nsig + 5));
        memcpy(t + U.h[i].nsig + 1, &rel, 4);
        U.h[i].trampoline = t;
    }
    __builtin___clear_cache((char *)tr, (char *)tr + 4096);
    for (int i = 0; i < HAAK_N; i++) {
        unsigned char j[5] = {0xe9}; int32_t rel = (int32_t)(vervangers[i] - (U.h[i].adres + 5)); memcpy(j + 1, &rel, 4);
        if (pwrite(memfd, j, 5, (off_t)U.h[i].adres) != 5) { meld("usb: een haak kon niet geschreven worden"); }
    }
    atomic_store(&usb_klaar, 1);
    char m[160]; snprintf(m, sizeof m, "usb: bord %04x:%04x aangehaakt (4 haken, %d velden, %d kanalen, elke %d ms)",
                          U.vid, U.pid, U.nv, U.na, U.interval);
    meld(m);
    return 1;
}

/* --- het bordbeeld lezen -------------------------------------------------------------------------------------------- */
static uint64_t nu_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static void zet_kanalen(const uint8_t *beeld) {
    for (int i = 0; i < T.nk; i++) {
        const struct rg_kanaal *k = &T.k[i];
        double f = beeld ? beeld[k->bron] / 255.0 : 0.5;
        double v = floor(k->min + f * (k->max - k->min) + 0.5);
        if (k->inverteer) v = k->min + k->max - v;
        int32_t nieuw = (int32_t)v, oud = atomic_load(&kwaarde[k->ch]);
        if (nieuw != oud) { atomic_store(&kvorig[k->ch], oud); atomic_store(&kwaarde[k->ch], nieuw); }
    }
}
/* Onderzoek: RG_RIO_PROBE=pad naar een bestand met regels "bit id" (live gelezen, elke 250 ms) voegt schakelaars toe aan
   die van de tabel. Een id die ooit een schakelaar had en die kwijtraakt, gaat terug naar "los" (een even teller). */
static struct rg_sw probe[16]; static int nprobe;
static unsigned char ooit[RG_MAX_ID];

static void zet_tellers(uint32_t bits) {
    for (int id = 0; id < RG_MAX_ID; id++) {
        int heeft = 0, stand = 0;
        for (int i = 0; i < T.nsw; i++) if (T.sw[i].id == id) { heeft = 1; if (bits >> T.sw[i].bit & 1) stand = 1; }
        for (int i = 0; i < nprobe; i++) if (probe[i].id == id) { heeft = 1; if (bits >> probe[i].bit & 1) stand = 1; }
        if (heeft) ooit[id] = 1; else if (!ooit[id]) continue;
        uint32_t t = atomic_load(&teller[id]);
        if ((int)(t & 1) == stand) continue;
        if (stand) { druk_ms[id] = nu_ms(); atomic_store(&teller[id], t + 1); }              /* indrukken: meteen */
        else if (!minhold[id] || nu_ms() - druk_ms[id] >= (uint64_t)minhold[id]) atomic_store(&teller[id], t + 1);   /* los: na min */
    }
}
static void lees_probe(const char *pad) {
    struct rg_sw nieuw[16]; int n = 0; int b, id;
    FILE *f = fopen(pad, "r");
    if (f) { while (n < 16 && fscanf(f, "%d %d", &b, &id) == 2) if (b >= 0 && b < 32 && id >= 0 && id < RG_MAX_ID) { nieuw[n].bit = b; nieuw[n].id = id; n++; } fclose(f); }
    if (n == nprobe && !memcmp(nieuw, probe, sizeof(struct rg_sw) * n)) return;
    memcpy(probe, nieuw, sizeof(struct rg_sw) * n); nprobe = n;
    char m[200]; int o = snprintf(m, sizeof m, "probe:");
    for (int i = 0; i < n && o < 180; i++) o += snprintf(m + o, sizeof m - o, " bit %d -> id %d", probe[i].bit, probe[i].id);
    if (!n) snprintf(m + o, sizeof m - o, " (leeg)");
    meld(m);
}
static void *werker(void *arg) {
    (void)arg;
    const char *shm = getenv("RG_RIO_SHM"); if (!shm) shm = "/dev/shm/ravengun_JvsState";
    const char *probepad = getenv("RG_RIO_PROBE");
    uint8_t buf[RG_BEELD]; int fd = -1;
    uint64_t start = nu_ms(), volgende_log = start + 5000, volgende_zoek = 0, volgende_probe = 0;
    int usb_gedaan = !U.aan;
    zet_kanalen(0);
    for (;;) {
        uint64_t t = nu_ms();
        if (T.bib[0] && t >= volgende_zoek) { zoek_en_pas_aan(); volgende_zoek = t + (t - start < 60000 ? 20 : 1000); }
        if (!usb_gedaan) {
            usb_gedaan = usb_aanhaken();
            if (!usb_gedaan && t - start > 120000) { meld("usb: na 120 s geen spelbeeld met deze bytes (ander binair?) -- het bord wordt NIET nagedaan"); usb_gedaan = 1; }
        }
        if (probepad && t >= volgende_probe) { lees_probe(probepad); volgende_probe = t + 250; }
        if (fd < 0) {
            fd = open(shm, O_RDONLY | O_CLOEXEC);           /* pread, geen mmap: een schrijver die afkapt geeft anders SIGBUS */
            if (fd >= 0) { char b[300]; snprintf(b, sizeof b, "bordbeeld gekoppeld: %s", shm); meld(b); }
        }
        if (fd >= 0) {
            ssize_t n = pread(fd, buf, sizeof buf, 0);
            if (n >= T.beeld_bits + 4) {
                uint32_t bits; memcpy(&bits, buf + T.beeld_bits, 4);
                atomic_store(&laatste_bits, bits);
                zet_tellers(bits);
                if (n >= RG_BEELD) zet_kanalen(buf);
                atomic_store(&beeld_ok, 1);
            }
        }
        if (log_on && t >= volgende_log) {
            volgende_log = t + 5000;
            char b[600]; int o = snprintf(b, sizeof b, "beeld=%s bits=0x%08x | tellers",
                                          atomic_load(&beeld_ok) ? "ja" : "nee", atomic_load(&laatste_bits));
            int al[RG_MAX_ID] = {0};
            for (int i = 0; i < T.nsw && o < 400; i++) if (!al[T.sw[i].id]) { al[T.sw[i].id] = 1; o += snprintf(b + o, sizeof b - o, " %d=%u", T.sw[i].id, atomic_load(&teller[T.sw[i].id])); }
            for (int i = 0; i < nprobe && o < 400; i++) if (!al[probe[i].id]) { al[probe[i].id] = 1; o += snprintf(b + o, sizeof b - o, " %d=%u", probe[i].id, atomic_load(&teller[probe[i].id])); }
            o += snprintf(b + o, sizeof b - o, " | kanalen");
            for (int i = 0; i < T.nk && o < 500; i++) o += snprintf(b + o, sizeof b - o, " %d=%d", T.k[i].ch, atomic_load(&kwaarde[T.k[i].ch]));
            if (U.aan)
                snprintf(b + o, sizeof b - o, " | usb %s: open=%u rapporten=%u van-de-game=%u sluit=%u", atomic_load(&usb_klaar) ? "aangehaakt" : "wacht",
                         atomic_load(&n_open), atomic_load(&n_lees), atomic_load(&n_schrijf), atomic_load(&n_sluit));
            else
                snprintf(b + o, sizeof b - o, " | aanroepen verbonden=%llu sw=%llu adc=%llu adc_ex=%llu", (unsigned long long)atomic_load(&n_verb),
                         (unsigned long long)atomic_load(&n_sw), (unsigned long long)atomic_load(&n_adc), (unsigned long long)atomic_load(&n_adcex));
            meld(b);
        }
        usleep(1000);
    }
    return 0;
}

__attribute__((constructor)) static void rg_rio_bridge_init(void) {
    const char *l = getenv("RG_RIO_LOG"); log_on = l && *l == '1';
    const char *pad = getenv("RG_RIO_TABLE");
    if (!pad || !lees_tabel(pad)) return;
    if (T.proces[0]) {
        char exe[512]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n <= 0) return;
        exe[n] = 0; const char *basis = strrchr(exe, '/'); basis = basis ? basis + 1 : exe;
        if (strcmp(basis, T.proces) != 0) return;   /* een hulpprogramma dat de preload erft: niets doen */
    }
    actief = 1;
    char b[300];   /* pas hier melden: een hulpprogramma dat de preload erft (FlushBuffers.sh) blijft zo stil */
    if (U.aan) snprintf(b, sizeof b, "tabel %s: usb-bord %04x:%04x, %d schakelaars, %d kanalen, %d velden, %d kanaalvelden%s", pad, U.vid, U.pid, T.nsw, T.nk, U.nv, U.na,
                       U.zonder ? ", zonder versleuteling" : "");
    else snprintf(b, sizeof b, "tabel %s: bibliotheek %s, %d functies, %d schakelaars, %d kanalen", pad, T.bib, T.nf, T.nsw, T.nk);
    meld(b);
    pthread_t th;
    if (pthread_create(&th, 0, werker, 0) == 0) pthread_detach(th);
}

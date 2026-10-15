/* RavenGun: invoerhulp voor Raw Thrills-games die native op Linux draaien (docs/decisions.md, "Native route").
 *
 * Probleem: de native Raw Thrills-games lezen de kast-schakelaars (munt, start, test/diag, service, volume), de geweerknoppen
 * (trekker, herladen, granaat) en het richtpunt van hun eigen I/O-bord (USB, in de game ingebouwde bibliotheek). Zonder dat bord
 * komt er niets binnen. Een virtueel lichtgeweer (een muis of joystick) helpt niet: de game leest zijn EIGEN bordstructuur, niet de
 * invoerapparaten van het systeem. De vooraf te laden bibliotheek van TeknoParrot (halo.so) doet alleen de dongle, geen invoer.
 *
 * Deze bibliotheek is invoer en niets anders: zij leest het bordbeeld van de daemon (/dev/shm/ravengun_JvsState, geschreven
 * door de IO-brug: een bitveld met de schakelaars en knoppen, en een byte per as) en zet de waarden in het geheugen van de game,
 * op de plek waar de game ze zelf leest. Zij emuleert geen bord, geen beveiligingschip, geen dongle en omzeilt geen controle van
 * de game: de game ziet nog steeds geen bord; alleen de invoer komt binnen.
 *
 * WAT waarheen gaat staat in een TABEL per game, niet in deze code (docs/per-game-patches.md; voorbeeld in
 * tools/rt-input-shim/rt-invoer.voorbeeld.ini). Een regel per ding, `soort sleutel=waarde ...`, # of ; begint commentaar:
 *
 *   build       getter=0x61e890 signatuur=85ff0f88d0050000    alleen op deze build iets doen (de bytes op dat adres moeten kloppen)
 *   tabel       basis=0x180ae80 eerste=0xaea0 per_bord=0x2200 per_schakelaar=0x50   de schakelaartabel van de game
 *   beeld       bits=8                                        waar het 32-bits bitveld in het bordbeeld staat
 *   schakelaar  bit=6 bord=0 k=0 puls=300                     bit uit het beeld -> schakelaar k van bord; puls in ms
 *   schakelaar  bit=7 bord=0 k=9 puls=0                       puls=0: ingedrukt zolang de bit staat (een knop, bv. de trekker)
 *   bit         bit=7 adres=0x1234560 breedte=4 doelbit=0     bit uit het beeld -> een bit in een woord in de game (volgt het niveau)
 *   as          bron=12 adres=0x1234570 soort=u8 min=0 max=255 inverteer=nee marge=2     asbyte uit het beeld -> een waarde in de game
 *   as          bron=12 adres=0x1234580 soort=f32 schaal=0.00392157 plus=0 buiten=-1     ... na de rekenregel: waarde*schaal+plus; buiten beeld een vaste waarde
 *   teller      bit=7 adres=0x1234590 breedte=4 modulo=16     bit uit het beeld -> een overgangsteller in de game (bit 0 = ingedrukt)
 *   zet         adres=0x12345a0 breedte=4 waarde=1            een vaste waarde die de hulp vasthoudt (bv. "er is een geweer")
 *
 * De rekenregel van een as is die van de IO-brug (src/ravengun/iobridge/tp_shm.cpp, tp_shm_render), letterlijk: de fractie f van
 * de as, EERST afronden (round(min + f*(max-min))), DAN inverteren (min + max - v), DAN klemmen tussen min+marge en max-marge --
 * behalve als de speler buiten beeld mikt: dan blijft de uiterste waarde staan. Buiten beeld is in het bordbeeld een asbyte van
 * bron_marge-1 of minder, of 255-(bron_marge-1) of meer (standaard bron_marge=2: 0, 1, 254 en 255).
 * Leest de game een as als kommagetal met een eigen bereik (0..1, -1..1), dan komt dat NA de regel: de uitkomst maal `schaal`, plus
 * `plus`. Met `buiten=` krijgt een as buiten beeld een vaste waarde in plaats van de uiterste (een game die -1 leest als "geen richtpunt").
 *
 * Een teller is hoe sommige games een knop lezen: geen niveau maar een telling van overgangen, die omloopt (modulo) en waarvan
 * bit 0 zegt of de knop nu ingedrukt is. De hulp zet bij elke verandering van de bit een stap: indrukken maakt de teller oneven,
 * loslaten even. Loopt hij ooit uit de pas (iemand anders schreef erin), dan is een stap genoeg om weer te kloppen.
 *
 * Zonder tabel doet de bibliotheek precies wat ze altijd deed: de ingebouwde standaard hieronder (de kastschakelaars van Halo:
 * Fireteam Raven, Game Rev 57529). Een tabel zonder `schakelaar`-regels houdt die ingebouwde schakelaars en voegt alleen toe;
 * een tabel met `schakelaar`-regels vervangt ze. `build` en `tabel` vervangen de ingebouwde adressen. Een tabel die niet klopt
 * (onbekend woord, een getal dat geen getal is, een adres waar niet geschreven kan worden) wordt in zijn geheel afgekeurd, met
 * een melding op stderr, en dan doet de bibliotheek NIETS: liever geen invoer dan schrijven op een verkeerde plek.
 *
 * Een schakelaar is een teller van indrukken in het beeld van de game (de game leest [ingang+4] != 0 als "ingedrukt"); een indruk
 * wordt daarom als puls gezet (per schakelaar 14 tot 300 ms, RG_RT_PULSE_MS overschrijft), zodat een beeld hem een keer ziet.
 *
 * Gebruik: LD_PRELOAD=/pad/librg_rt_input.so:/pm/g7/halo/halo.so ... De daemon moet de IO-brug (tp-shm) draaien.
 * Omgeving: RG_RT_TABLE=pad (de tabel; anders rt-invoer.ini naast deze bibliotheek, anders de ingebouwde standaard),
 *           RG_RT_SHM=pad (standaard /dev/shm/ravengun_JvsState), RG_RT_LOG=1 (meldingen op stderr), RG_RT_PULSE_MS.
 * Onderzoek: RG_RT_PROBE=pad (bestand met "bit,bord,k", live gelezen) en RG_RT_ALLTYPES=1 (alle schakelaars als aanwezig markeren).
 * Eigen code.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* --- de ingebouwde standaard: de kastschakelaars van Halo: Fireteam Raven, Game Rev 57529 (zoals het altijd was) --- */
#define GETTER_ADDR 0x61e890UL
#define TABLE_BASE 0x180ae80UL      /* de io-toestand van de game */
#define TABLE_OFF 0xaea0UL          /* eerste schakelaar van bord 0 */
#define BOARD_STRIDE 0x2200UL
#define ENTRY_STRIDE 0x50UL

static const unsigned char getter_sig[8] = {0x85, 0xff, 0x0f, 0x88, 0xd0, 0x05, 0x00, 0x00};

/* Schakelaar k van bord b: k 0 = start, 1 = volume omhoog, 2 = volume omlaag, 3 = service, 4 = diag (test), 5..8 = munt 0..3. */
struct map { int bit; int board; int k; int ms; };
static const struct map STANDAARD_MAP[] = {
    {6, 0, 0, 300}, {11, 1, 0, 300}, {16, 2, 0, 300}, {21, 3, 0, 300},   /* start speler 1..4 */
    {0, 0, 4, 300}, {0, 1, 4, 300}, {0, 2, 4, 300}, {0, 3, 4, 300},  /* test -> diag van elk bord. De reden die hier stond ("het kalibratiescherm per gun wil een langere indruk") klopt op deze machine niet: zie hieronder */
    {1, 0, 3, 300},                                                 /* service */
    {2, 0, 5, 14}, {3, 1, 5, 14},                                   /* munt 1 en 2: een indruk telt per beeld, dus kort */
    {4, 0, 1, 300}, {5, 0, 2, 300},                                   /* volume omhoog en omlaag */
};
#define NSTANDAARD (int)(sizeof(STANDAARD_MAP) / sizeof(STANDAARD_MAP[0]))
/* CORRECTIE (2026-10-13) op een eerdere bewering: er stond dat TEST de kalibratie van gun 1 overslaat en START die van gun 2.
   Gemeten op de native route is dat niet zo: zes TEST-pulsen op alle vier de borden deden niets (het logboek van de game bleef op
   256 regels, de foto's bleven het kalibratiescherm), en een run met alleen START kwam door de kalibratie en in de attract-lus
   ("attract" om 11:56:32, zonder enige TEST-puls). De test-regels hierboven blijven staan zoals ze waren -- het is de afbeelding van
   de testknop op de diag-schakelaar, en die is voor het testmenu -- maar wie door de kalibratie wil, stuurt START. */

/* --- de tabel ------------------------------------------------------------------------------------------------------ */
#define RG_MAX_SW 96
#define RG_MAX_BIT 64
#define RG_MAX_AS 32
#define RG_MAX_TELLER 64
#define RG_MAX_ZET 64
#define RG_BEELD 64                 /* het bordbeeld is 64 bytes */

enum { RG_U8, RG_U16, RG_U32, RG_I32, RG_F32 };

struct rg_bit { int bit; uintptr_t adres; int breedte; int doelbit; int inverteer; };
struct rg_as { int bron; uintptr_t adres; int soort; double min, max; int inverteer; double marge; int bron_marge;
               int geschaald; double schaal, plus; int heeft_buiten; double buiten; };
struct rg_teller { int bit; uintptr_t adres; int breedte; int modulo; int inverteer; };
struct rg_zet { uintptr_t adres; int breedte; uint32_t waarde; };

struct rg_tabel {
    uintptr_t getter; unsigned char sig[32]; int nsig;
    uintptr_t basis, eerste, per_bord, per_schakelaar;
    int beeld_bits;                 /* offset van het bitveld in het bordbeeld */
    struct map sw[RG_MAX_SW]; int nsw;
    struct rg_bit bit[RG_MAX_BIT]; int nbit;
    struct rg_as as[RG_MAX_AS]; int nas;
    struct rg_teller teller[RG_MAX_TELLER]; int nteller;
    struct rg_zet zet[RG_MAX_ZET]; int nzet;
};

/* De toestand van de pulsen, per schakelaar. */
struct rg_staat {
    uint32_t prev;
    uint64_t until[RG_MAX_SW], rel_until[RG_MAX_SW];
    int active[RG_MAX_SW];
};

/* Alle adressen in de tabel zijn adressen in de game. De test legt er een eigen stuk geheugen onder (rg_mem_basis). */
static uintptr_t rg_mem_basis;
#define RG_MEM(adres) ((void *)(rg_mem_basis + (uintptr_t)(adres)))

static int log_on;
static void logf_(const char *m) { if (log_on) { fputs("rg_rt_input: ", stderr); fputs(m, stderr); fputc('\n', stderr); } }

static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static void rg_tabel_standaard(struct rg_tabel *t) {
    memset(t, 0, sizeof *t);
    t->getter = GETTER_ADDR; memcpy(t->sig, getter_sig, sizeof getter_sig); t->nsig = (int)sizeof getter_sig;
    t->basis = TABLE_BASE; t->eerste = TABLE_OFF; t->per_bord = BOARD_STRIDE; t->per_schakelaar = ENTRY_STRIDE;
    t->beeld_bits = 8;
    memcpy(t->sw, STANDAARD_MAP, sizeof STANDAARD_MAP); t->nsw = NSTANDAARD;
}

static volatile uint32_t *rg_ingang(const struct rg_tabel *t, int board, int k) {
    return (volatile uint32_t *)RG_MEM(t->basis + t->eerste + (uintptr_t)board * t->per_bord + (uintptr_t)k * t->per_schakelaar);
}

/* De rekenregel van een as, letterlijk die van tp_shm_render (src/ravengun/iobridge/tp_shm.cpp):
     double v = std::round(a.min + f * (a.max - a.min));
     if (a.invert) v = a.min + a.max - v;
     if (... offscreen_margin > 0 ...) { if (!out) v = std::min(255.0 - margin, std::max(margin, v)); }
   met f de fractie van de as (0..1) en `out` = de speler mikt buiten beeld. Hier is 255 het maximum van de as (max), en de marge
   geldt dus tussen min+marge en max-marge; voor een byte-as (0..255) is dat exact dezelfde regel. */
static double rg_as_waarde(const struct rg_as *a, double f, int buiten) {
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    double v = round(a->min + f * (a->max - a->min));
    if (a->inverteer) v = a->min + a->max - v;
    if (a->marge > 0 && !buiten) {
        double lo = a->min + a->marge, hi = a->max - a->marge;
        if (v > hi) v = hi;
        if (v < lo) v = lo;
    }
    return v;
}

/* Een asbyte uit het bordbeeld -> (fractie, buiten beeld?). De IO-brug houdt een as binnen het scherm op bron_marge..255-bron_marge;
   alles daarbuiten betekent dat de speler naast het scherm mikt. */
static double rg_as_bron(const struct rg_as *a, uint8_t b, int *buiten) {
    *buiten = a->bron_marge > 0 && ((int)b <= a->bron_marge - 1 || (int)b >= 255 - (a->bron_marge - 1));
    return (double)b / 255.0;
}

static void rg_as_schrijf(const struct rg_as *a, double v) {
    void *p = RG_MEM(a->adres);
    switch (a->soort) {
        case RG_U8:  { if (v < 0) v = 0; if (v > 255) v = 255; uint8_t x = (uint8_t)(unsigned)v; memcpy(p, &x, 1); break; }
        case RG_U16: { if (v < 0) v = 0; if (v > 65535) v = 65535; uint16_t x = (uint16_t)(unsigned)v; memcpy(p, &x, 2); break; }
        case RG_U32: { if (v < 0) v = 0; if (v > 4294967295.0) v = 4294967295.0; uint32_t x = (uint32_t)v; memcpy(p, &x, 4); break; }
        case RG_I32: { if (v < -2147483648.0) v = -2147483648.0; if (v > 2147483647.0) v = 2147483647.0; int32_t x = (int32_t)v; memcpy(p, &x, 4); break; }
        case RG_F32: { float x = (float)v; memcpy(p, &x, 4); break; }
    }
}

/* Een stap: het bordbeeld (`n` gelezen bytes) naar het geheugen van de game. `t` is de tijd in ms; pulse_override > 0 vervangt
   de pulsduur van elke schakelaar die een puls heeft. Dit is de hele werking; de draad hieronder leest alleen het beeld en roept dit aan. */
static void rg_stap(const struct rg_tabel *tb, struct rg_staat *st, const uint8_t *buf, ssize_t n, uint64_t t, int pulse_override) {
    if (n < (ssize_t)tb->beeld_bits + 4) return;   /* beeld (nog) te kort: niets doen */
    const uint8_t *bb = buf + tb->beeld_bits;
    uint32_t bits = (uint32_t)bb[0] | ((uint32_t)bb[1] << 8) | ((uint32_t)bb[2] << 16) | ((uint32_t)bb[3] << 24);
    for (int i = 0; i < tb->nsw; i++) {
        const struct map *m = &tb->sw[i];
        int now = (bits >> m->bit) & 1, was = (st->prev >> m->bit) & 1;
        if (now && !was) {
            st->until[i] = t + (uint64_t)(pulse_override > 0 && m->ms > 0 ? pulse_override : m->ms); st->active[i] = 1;
            volatile uint32_t *e0 = rg_ingang(tb, m->board, m->k); e0[6]++;
            if (log_on) { char b[200]; snprintf(b, sizeof b, "indruk bit %d -> bord %d schakelaar %d; tabel NA de indruk: [0]=%u [3]=%u [4]=%u [5]=%u [6]=%u", m->bit, m->board, m->k, e0[0], e0[3], e0[4], e0[5], e0[6]); logf_(b); }
        }
        volatile uint32_t *e = rg_ingang(tb, m->board, m->k);
        if (!st->active[i] && st->rel_until[i]) { if (t < st->rel_until[i]) e[4] = 1; else { e[4] = 0; st->rel_until[i] = 0; } }
        if (st->active[i]) {
            /* puls: tot de tijd om is. puls=0 (een knop): zolang de bit in het beeld staat. */
            int houd = m->ms > 0 ? (t < st->until[i]) : now;
            if (houd) { e[0] = 1; e[3] = 1; e[5] = 1; }  /* soort 1 = actief; ingang+4 (= e[3]) = indruk in dit beeld; e[5] = vastgehouden */
            else { e[3] = 0; e[5] = 0; e[7]++; st->active[i] = 0; st->rel_until[i] = t + 25;
                   if (log_on) { char b[160]; snprintf(b, sizeof b, "tabel bord %d k %d bij LOSLATEN: [0]=%u [3]=%u [4]=%u [5]=%u [7]=%u", m->board, m->k, e[0], e[3], e[4], e[5], e[7]); logf_(b); } }   /* loslaten: ingang+8 (= e[4]) = loslating in dit beeld */
        }
    }
    /* Losse bits: een bit uit het beeld volgt zijn niveau in een woord van de game; de andere bits van dat woord blijven staan. */
    for (int i = 0; i < tb->nbit; i++) {
        const struct rg_bit *r = &tb->bit[i];
        int aan = (int)((bits >> r->bit) & 1) ^ (r->inverteer ? 1 : 0);
        uint32_t w = 0; memcpy(&w, RG_MEM(r->adres), (size_t)r->breedte);
        uint32_t mask = (uint32_t)1 << r->doelbit;
        uint32_t nieuw = aan ? (w | mask) : (w & ~mask);
        if (nieuw != w) memcpy(RG_MEM(r->adres), &nieuw, (size_t)r->breedte);
    }
    /* Assen: een byte uit het beeld -> een waarde in de game, met de rekenregel van de IO-brug. */
    for (int i = 0; i < tb->nas; i++) {
        const struct rg_as *a = &tb->as[i];
        if (n < (ssize_t)a->bron + 1) continue;   /* het beeld heeft deze as (nog) niet */
        int buiten = 0;
        double f = rg_as_bron(a, buf[a->bron], &buiten);
        double w = rg_as_waarde(a, f, buiten);
        if (a->geschaald) w = w * a->schaal + a->plus;       /* na de regel: het bereik van de game */
        if (buiten && a->heeft_buiten) w = a->buiten;
        rg_as_schrijf(a, w);
    }
    /* Tellers: een knop als telling van overgangen. Bit 0 van de teller is "nu ingedrukt"; klopt dat niet met de bit, dan een stap. */
    for (int i = 0; i < tb->nteller; i++) {
        const struct rg_teller *r = &tb->teller[i];
        uint32_t aan = ((bits >> r->bit) & 1) ^ (r->inverteer ? 1u : 0u);
        uint32_t w = 0; memcpy(&w, RG_MEM(r->adres), (size_t)r->breedte);
        if ((w & 1) != aan) { w = (w + 1) % (uint32_t)r->modulo; memcpy(RG_MEM(r->adres), &w, (size_t)r->breedte); }
    }
    /* Vaste waarden: vasthouden (de game kan zijn eigen toestand bij het opstarten nog eens leegmaken). */
    for (int i = 0; i < tb->nzet; i++) {
        const struct rg_zet *z = &tb->zet[i];
        uint32_t w = 0; memcpy(&w, RG_MEM(z->adres), (size_t)z->breedte);
        if (w != z->waarde) memcpy(RG_MEM(z->adres), &z->waarde, (size_t)z->breedte);
    }
    st->prev = bits;
}

/* --- de tabel lezen -------------------------------------------------------------------------------------------------- */

/* Kan er op [adres, adres+len) geschreven worden? Gelezen uit /proc/self/maps: een adres dat niet in een schrijfbaar stuk
   geheugen ligt zou de game laten omvallen, en dat hoort de tabel te zeggen voordat er iets geschreven wordt. */
static int rg_toegang(uintptr_t adres, size_t len, int schrijven) {
    uintptr_t a = rg_mem_basis + adres, e = a + len;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 1;            /* niet te controleren: dan niet tegenhouden */
    char regel[512]; int ok = 0;
    while (fgets(regel, sizeof regel, f)) {
        unsigned long lo, hi; char perm[8];
        if (sscanf(regel, "%lx-%lx %7s", &lo, &hi, perm) == 3 && a >= lo && e <= hi) { ok = schrijven ? perm[1] == 'w' : perm[0] == 'r'; break; }
    }
    fclose(f);
    return ok;
}
static int rg_schrijfbaar(uintptr_t adres, size_t len) { return rg_toegang(adres, len, 1); }

static int rg_getal(const char *s, double *uit) {
    if (!s || !*s) return 0;
    char *eind = 0;
    if ((s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) { unsigned long long v = strtoull(s, &eind, 16); *uit = (double)v; }
    else *uit = strtod(s, &eind);
    return eind && *eind == 0;
}
static int rg_adres(const char *s, uintptr_t *uit) {
    if (!s || !*s) return 0;
    char *eind = 0; unsigned long long v = strtoull(s, &eind, 0);
    *uit = (uintptr_t)v;
    return eind && *eind == 0;
}
static int rg_janee(const char *s, int *uit) {
    if (!s) return 0;
    if (!strcmp(s, "ja") || !strcmp(s, "1") || !strcmp(s, "true")) { *uit = 1; return 1; }
    if (!strcmp(s, "nee") || !strcmp(s, "0") || !strcmp(s, "false")) { *uit = 0; return 1; }
    return 0;
}

/* Een regel `soort sleutel=waarde ...` opdelen. Geeft het aantal paren, of -1 bij een woord zonder '='. */
struct rg_paar { char *k, *v; int gebruikt; };
static int rg_paren(char *rest, struct rg_paar *p, int max) {
    int n = 0;
    for (char *w = strtok(rest, " \t\r\n"); w; w = strtok(0, " \t\r\n")) {
        char *eq = strchr(w, '=');
        if (!eq || eq == w || n >= max) return -1;
        *eq = 0; p[n].k = w; p[n].v = eq + 1; p[n].gebruikt = 0; n++;
    }
    return n;
}
static const char *rg_zoek(struct rg_paar *p, int n, const char *k) {
    for (int i = 0; i < n; i++) if (!strcmp(p[i].k, k)) { p[i].gebruikt = 1; return p[i].v; }
    return 0;
}

#define RG_FOUT(...) do { snprintf(fout, nfout, __VA_ARGS__); return 0; } while (0)
/* Binnen het lezen: eerst de melding maken (die leest uit de kopie van de tekst), dan pas de kopie vrijgeven. */
#define RG_WEG(...) do { snprintf(fout, nfout, __VA_ARGS__); free(kopie); return 0; } while (0)

/* Leest een tabel bovenop de ingebouwde standaard. Geeft 1, of 0 met de reden in `fout`; dan is `t` niet te gebruiken. */
static int rg_tabel_lees(const char *tekst, struct rg_tabel *t, char *fout, size_t nfout) {
    rg_tabel_standaard(t);
    int eigen_sw = 0, nr = 0;
    char *kopie = strdup(tekst), *bewaar = 0;
    if (!kopie) RG_FOUT("geen geheugen");
    /* regels zelf opdelen: strtok wordt hieronder al voor de woorden gebruikt */
    for (char *regel = kopie; regel && *regel; regel = bewaar) {
        nr++;
        char *nl = strchr(regel, '\n');
        if (nl) { *nl = 0; bewaar = nl + 1; } else bewaar = 0;
        char *c = strpbrk(regel, "#;"); if (c) *c = 0;
        while (*regel && isspace((unsigned char)*regel)) regel++;
        if (!*regel) continue;
        char *rest = regel; while (*rest && !isspace((unsigned char)*rest)) rest++;
        if (*rest) *rest++ = 0;
        struct rg_paar p[16]; int n = rg_paren(rest, p, 16);
        if (n < 0) { RG_WEG("regel %d: na '%s' hoort sleutel=waarde te staan", nr, regel); }
        double d; uintptr_t a; int j;
#define NODIG_GETAL(naam, doel) do { if (!rg_getal(rg_zoek(p, n, naam), &d)) { RG_WEG("regel %d (%s): '%s' ontbreekt of is geen getal", nr, regel, naam); } doel = d; } while (0)
#define NODIG_ADRES(naam, doel) do { if (!rg_adres(rg_zoek(p, n, naam), &a)) { RG_WEG("regel %d (%s): '%s' ontbreekt of is geen adres", nr, regel, naam); } doel = a; } while (0)
        if (!strcmp(regel, "build")) {
            NODIG_ADRES("getter", t->getter);
            const char *s = rg_zoek(p, n, "signatuur"); size_t len = s ? strlen(s) : 0;
            if (!s || len < 2 || len % 2 || len / 2 > sizeof t->sig) { RG_WEG("regel %d (build): 'signatuur' ontbreekt of is geen reeks hex-bytes", nr); }
            for (size_t i = 0; i < len; i += 2) {
                unsigned x; char h[3] = {s[i], s[i + 1], 0};
                if (!isxdigit((unsigned char)h[0]) || !isxdigit((unsigned char)h[1])) { RG_WEG("regel %d (build): 'signatuur' is geen reeks hex-bytes", nr); }
                sscanf(h, "%x", &x); t->sig[i / 2] = (unsigned char)x;
            }
            t->nsig = (int)(len / 2);
        } else if (!strcmp(regel, "tabel")) {
            NODIG_ADRES("basis", t->basis); NODIG_ADRES("eerste", t->eerste);
            NODIG_ADRES("per_bord", t->per_bord); NODIG_ADRES("per_schakelaar", t->per_schakelaar);
        } else if (!strcmp(regel, "beeld")) {
            NODIG_GETAL("bits", j);
            if (j < 0 || j > RG_BEELD - 4) { RG_WEG("regel %d (beeld): 'bits' moet 0 tot %d zijn", nr, RG_BEELD - 4); }
            t->beeld_bits = j;
        } else if (!strcmp(regel, "schakelaar")) {
            if (!eigen_sw) { t->nsw = 0; eigen_sw = 1; }      /* eigen schakelaars vervangen de ingebouwde */
            if (t->nsw >= RG_MAX_SW) { RG_WEG("regel %d: te veel schakelaars (hooguit %d)", nr, RG_MAX_SW); }
            struct map *m = &t->sw[t->nsw];
            NODIG_GETAL("bit", m->bit); NODIG_GETAL("bord", m->board); NODIG_GETAL("k", m->k); NODIG_GETAL("puls", m->ms);
            if (m->bit < 0 || m->bit > 31 || m->board < 0 || m->board > 63 || m->k < 0 || m->k > 255 || m->ms < 0 || m->ms > 60000) { RG_WEG("regel %d (schakelaar): bit 0-31, bord 0-63, k 0-255, puls 0-60000", nr); }
            t->nsw++;
        } else if (!strcmp(regel, "bit")) {
            if (t->nbit >= RG_MAX_BIT) { RG_WEG("regel %d: te veel bits (hooguit %d)", nr, RG_MAX_BIT); }
            struct rg_bit *r = &t->bit[t->nbit];
            NODIG_GETAL("bit", r->bit); NODIG_ADRES("adres", r->adres); NODIG_GETAL("breedte", r->breedte); NODIG_GETAL("doelbit", r->doelbit);
            r->inverteer = 0;
            const char *iv = rg_zoek(p, n, "inverteer");
            if (iv && !rg_janee(iv, &r->inverteer)) { RG_WEG("regel %d (bit): 'inverteer' is ja of nee", nr); }
            if (r->bit < 0 || r->bit > 31 || (r->breedte != 1 && r->breedte != 2 && r->breedte != 4) || r->doelbit < 0 || r->doelbit >= r->breedte * 8) { RG_WEG("regel %d (bit): bit 0-31, breedte 1, 2 of 4, doelbit binnen die breedte", nr); }
            t->nbit++;
        } else if (!strcmp(regel, "as")) {
            if (t->nas >= RG_MAX_AS) { RG_WEG("regel %d: te veel assen (hooguit %d)", nr, RG_MAX_AS); }
            struct rg_as *x = &t->as[t->nas];
            NODIG_GETAL("bron", x->bron); NODIG_ADRES("adres", x->adres);
            const char *so = rg_zoek(p, n, "soort");
            if (!so) so = "u8";
            if (!strcmp(so, "u8")) x->soort = RG_U8; else if (!strcmp(so, "u16")) x->soort = RG_U16; else if (!strcmp(so, "u32")) x->soort = RG_U32;
            else if (!strcmp(so, "i32")) x->soort = RG_I32; else if (!strcmp(so, "f32")) x->soort = RG_F32;
            else { RG_WEG("regel %d (as): 'soort' is u8, u16, u32, i32 of f32", nr); }
            x->min = 0; x->max = 255; x->marge = 0; x->inverteer = 0; x->bron_marge = 2;
            const char *s;
            if ((s = rg_zoek(p, n, "min")) && !rg_getal(s, &x->min)) { RG_WEG("regel %d (as): 'min' is geen getal", nr); }
            if ((s = rg_zoek(p, n, "max")) && !rg_getal(s, &x->max)) { RG_WEG("regel %d (as): 'max' is geen getal", nr); }
            if ((s = rg_zoek(p, n, "marge")) && !rg_getal(s, &x->marge)) { RG_WEG("regel %d (as): 'marge' is geen getal", nr); }
            if ((s = rg_zoek(p, n, "bron_marge"))) { if (!rg_getal(s, &d)) { RG_WEG("regel %d (as): 'bron_marge' is geen getal", nr); } x->bron_marge = (int)d; }
            if ((s = rg_zoek(p, n, "inverteer")) && !rg_janee(s, &x->inverteer)) { RG_WEG("regel %d (as): 'inverteer' is ja of nee", nr); }
            x->geschaald = 0; x->schaal = 1; x->plus = 0; x->heeft_buiten = 0; x->buiten = 0;
            if ((s = rg_zoek(p, n, "schaal"))) { if (!rg_getal(s, &x->schaal) || x->schaal == 0) { RG_WEG("regel %d (as): 'schaal' is geen getal of nul", nr); } x->geschaald = 1; }
            if ((s = rg_zoek(p, n, "plus"))) { if (!rg_getal(s, &x->plus)) { RG_WEG("regel %d (as): 'plus' is geen getal", nr); } x->geschaald = 1; }
            if ((s = rg_zoek(p, n, "buiten"))) { if (!rg_getal(s, &x->buiten)) { RG_WEG("regel %d (as): 'buiten' is geen getal", nr); } x->heeft_buiten = 1; }
            if (x->bron < 0 || x->bron >= RG_BEELD || x->max <= x->min || x->marge < 0 || 2 * x->marge >= x->max - x->min || x->bron_marge < 0 || x->bron_marge > 127) { RG_WEG("regel %d (as): bron 0-%d, max groter dan min, marge kleiner dan de helft van het bereik, bron_marge 0-127", nr, RG_BEELD - 1); }
            t->nas++;
        } else if (!strcmp(regel, "teller")) {
            if (t->nteller >= RG_MAX_TELLER) { RG_WEG("regel %d: te veel tellers (hooguit %d)", nr, RG_MAX_TELLER); }
            struct rg_teller *r = &t->teller[t->nteller];
            NODIG_GETAL("bit", r->bit); NODIG_ADRES("adres", r->adres); NODIG_GETAL("breedte", r->breedte); NODIG_GETAL("modulo", r->modulo);
            r->inverteer = 0;
            const char *iv = rg_zoek(p, n, "inverteer");
            if (iv && !rg_janee(iv, &r->inverteer)) { RG_WEG("regel %d (teller): 'inverteer' is ja of nee", nr); }
            if (r->bit < 0 || r->bit > 31 || (r->breedte != 1 && r->breedte != 2 && r->breedte != 4) || r->modulo < 2 || r->modulo % 2 || r->modulo > 65536
                || (r->breedte == 1 && r->modulo > 256)) { RG_WEG("regel %d (teller): bit 0-31, breedte 1, 2 of 4, modulo even en 2-65536 (en passend in de breedte)", nr); }
            t->nteller++;
        } else if (!strcmp(regel, "zet")) {
            if (t->nzet >= RG_MAX_ZET) { RG_WEG("regel %d: te veel vaste waarden (hooguit %d)", nr, RG_MAX_ZET); }
            struct rg_zet *z = &t->zet[t->nzet];
            NODIG_ADRES("adres", z->adres); NODIG_GETAL("breedte", z->breedte);
            if (!rg_getal(rg_zoek(p, n, "waarde"), &d) || d < 0 || d > 4294967295.0) { RG_WEG("regel %d (zet): 'waarde' ontbreekt of is geen getal van 0 tot 4294967295", nr); }
            z->waarde = (uint32_t)d;
            if ((z->breedte != 1 && z->breedte != 2 && z->breedte != 4) || (z->breedte == 1 && d > 255) || (z->breedte == 2 && d > 65535)) { RG_WEG("regel %d (zet): breedte 1, 2 of 4, en de waarde moet erin passen", nr); }
            t->nzet++;
        } else { RG_WEG("regel %d: onbekend woord '%s' (build, tabel, beeld, schakelaar, bit, as, teller of zet)", nr, regel); }
        for (int i = 0; i < n; i++) if (!p[i].gebruikt) { char k[64]; snprintf(k, sizeof k, "%s", p[i].k); RG_WEG("regel %d (%s): onbekende sleutel '%s'", nr, regel, k); }
    }
    free(kopie);
    /* Overal waar geschreven gaat worden moet geschreven kunnen worden. */
    for (int i = 0; i < t->nsw; i++) if (!rg_schrijfbaar(t->basis + t->eerste + (uintptr_t)t->sw[i].board * t->per_bord + (uintptr_t)t->sw[i].k * t->per_schakelaar, 32))
        RG_FOUT("schakelaar bord %d k %d: op dat adres kan niet geschreven worden", t->sw[i].board, t->sw[i].k);
    for (int i = 0; i < t->nbit; i++) if (!rg_schrijfbaar(t->bit[i].adres, (size_t)t->bit[i].breedte))
        RG_FOUT("bit %d: op adres 0x%lx kan niet geschreven worden", t->bit[i].bit, (unsigned long)t->bit[i].adres);
    for (int i = 0; i < t->nas; i++) if (!rg_schrijfbaar(t->as[i].adres, t->as[i].soort == RG_U8 ? 1 : t->as[i].soort == RG_U16 ? 2 : 4))
        RG_FOUT("as (bron %d): op adres 0x%lx kan niet geschreven worden", t->as[i].bron, (unsigned long)t->as[i].adres);
    for (int i = 0; i < t->nteller; i++) if (!rg_schrijfbaar(t->teller[i].adres, (size_t)t->teller[i].breedte))
        RG_FOUT("teller (bit %d): op adres 0x%lx kan niet geschreven worden", t->teller[i].bit, (unsigned long)t->teller[i].adres);
    for (int i = 0; i < t->nzet; i++) if (!rg_schrijfbaar(t->zet[i].adres, (size_t)t->zet[i].breedte))
        RG_FOUT("zet: op adres 0x%lx kan niet geschreven worden", (unsigned long)t->zet[i].adres);
    return 1;
}

/* Is dit de build waar de tabel voor geschreven is? De bytes op het getter-adres moeten kloppen. Een adres dat in dit proces
   niet bestaat is ook "een andere build": dan niet lezen (dat zou het spel laten omvallen) maar gewoon niets doen. */
static int rg_build_klopt(const struct rg_tabel *t) {
    return t->nsig > 0 && rg_toegang(t->getter, (size_t)t->nsig, 0) && memcmp(RG_MEM(t->getter), t->sig, (size_t)t->nsig) == 0;
}

/* De tabel van deze game: RG_RT_TABLE, anders rt-invoer.ini naast deze bibliotheek, anders de ingebouwde standaard.
   Geeft 1 (te gebruiken) of 0 (afgekeurd: dan doen we niets). Een afgekeurde tabel wordt ALTIJD gemeld, ook zonder RG_RT_LOG. */
static int rg_tabel_kies(struct rg_tabel *t) {
    char pad[1024] = "";
    const char *env = getenv("RG_RT_TABLE");
    if (env && *env) snprintf(pad, sizeof pad, "%s", env);
    else {
        Dl_info info;
        if (dladdr((void *)rg_tabel_kies, &info) && info.dli_fname) {
            const char *s = strrchr(info.dli_fname, '/');
            if (s) { snprintf(pad, sizeof pad, "%.*s/rt-invoer.ini", (int)(s - info.dli_fname), info.dli_fname); if (access(pad, R_OK) != 0) pad[0] = 0; }
        }
    }
    if (!pad[0]) { rg_tabel_standaard(t); logf_("geen tabel: de ingebouwde standaard (alleen de kastschakelaars)"); return 1; }
    FILE *f = fopen(pad, "r");
    if (!f) { fprintf(stderr, "rg_rt_input: FOUT: de tabel %s is niet te lezen; er komt GEEN invoer\n", pad); return 0; }
    static char tekst[65536]; size_t n = fread(tekst, 1, sizeof tekst - 1, f); fclose(f); tekst[n] = 0;
    char fout[256];
    if (!rg_tabel_lees(tekst, t, fout, sizeof fout)) {
        fprintf(stderr, "rg_rt_input: FOUT: de tabel %s klopt niet (%s); er komt GEEN invoer\n", pad, fout);
        return 0;
    }
    if (log_on) { char b[1200]; snprintf(b, sizeof b, "tabel %s: %d schakelaars, %d bits, %d assen, %d tellers, %d vaste waarden", pad, t->nsw, t->nbit, t->nas, t->nteller, t->nzet); logf_(b); }
    return 1;
}

#ifndef RG_RT_INPUT_TEST

static void *worker(void *arg) {
    (void)arg;
    const char *env = getenv("RG_RT_LOG"); log_on = env && *env == '1';
    const char *shm = getenv("RG_RT_SHM"); if (!shm) shm = "/dev/shm/ravengun_JvsState";   /* het bestand van de daemon (iobridge/tp_shm.hpp) */
    const char *pm = getenv("RG_RT_PULSE_MS"); int pulse_override = pm ? atoi(pm) : 0;

    /* Onderzoek: RG_RT_PROBE=pad naar een bestand met "bit,bord,k" zet een schakelaar van de tabel (live, elke 250 ms gelezen) op een andere plek. */
    const char *probe = getenv("RG_RT_PROBE");
    uint64_t probe_t = 0;
    const char *at = getenv("RG_RT_ALLTYPES"); int alltypes = at && *at == '1';

    static struct rg_tabel tb;
    if (!rg_tabel_kies(&tb)) return 0;

    /* Alleen op de bekende build. */
    if (!rg_build_klopt(&tb)) { logf_("andere game-build, geen invoer"); return 0; }

    /* LET OP: hier stond een mmap van het schakelaarbeeld. Dat is gevaarlijk: een schrijver die het
       bestand ook maar even afkapt (open met "wb" doet dat) maakt de mapping ongeldig, en dan sterft
       het spel met SIGBUS op de eerstvolgende lezing. Dat is precies wat in run 4 gebeurde
       (librg_rt_input.so+0x1f44, lezend op st+8). pread kan daar niet op stuklopen. */
    int shm_fd = -1;
    uint8_t buf[RG_BEELD];
    static struct rg_staat st;
    for (;;) {
        if (shm_fd < 0) {
            shm_fd = open(shm, O_RDONLY | O_CLOEXEC);
            if (shm_fd < 0) { usleep(250000); continue; }
            logf_("gedeeld geheugen gekoppeld (pread)");
        }
        if (probe && now_ms() > probe_t + 250) {
            probe_t = now_ms();
            FILE *pf = fopen(probe, "r");
            if (pf) { int b, bd, k; if (fscanf(pf, "%d,%d,%d", &b, &bd, &k) == 3) for (int i = 0; i < tb.nsw; i++) if (tb.sw[i].bit == b) { tb.sw[i].board = bd; tb.sw[i].k = k; } fclose(pf); }
        }
        if (alltypes) for (int b = 0; b < 4; b++) for (int k = 0; k < 13; k++) rg_ingang(&tb, b, k)[0] = 1;   /* onderzoek: alle schakelaars als "aanwezig" markeren */
        ssize_t gelezen = pread(shm_fd, buf, sizeof buf, 0);
        if (gelezen < 12) { usleep(10000); continue; }   /* bestand (nog) te kort: niets lezen */
        rg_stap(&tb, &st, buf, gelezen, now_ms(), pulse_override);
        usleep(1000);
    }
    return 0;
}

__attribute__((constructor)) static void rg_rt_input_init(void) {
    /* Alleen in het proces van de game zelf (de game start hulpprogramma's, bv. sha1sum, die de preload erven). */
    char exe[256]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n < 5) return;
    exe[n] = 0;
    if (strcmp(exe + n - 5, "/game") != 0) return;
    pthread_t th;
    if (pthread_create(&th, 0, worker, 0) == 0) pthread_detach(th);
}
#endif /* RG_RT_INPUT_TEST */

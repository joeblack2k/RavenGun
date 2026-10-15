/* RavenGun: IRTrack-geweren voor Raw Thrills-games die native op Linux draaien (Halo: Fireteam Raven).
 *
 * Probleem: de geweren van de Halo-kast zijn geen lichtgeweren aan het I/O-bord maar USB-camera's met een FTDI-chip
 * ("IRTrack - Rev. 2" van "Raw Thrills, Inc.", VID 0x0403 / PID 0x6001). De game zoekt ze met de libftdi die in de game zelf
 * zit (statisch gelinkt), leest per geweer een stroom ASCII-berichten (X = IR-vlekken, G = knoppen, Y = status) en rekent uit
 * de vlekken van de kast-LED's het richtpunt uit. Zonder die apparaten staat er "P1/P2 GUN NOT CONNECTED" en doet niemand mee.
 * Het protocol staat in patches/Halo/IRTRACK-PROTOCOL.md (statisch gelezen, met adressen).
 *
 * Deze bibliotheek doet de geweren na, in het proces van de game: de libftdi-functies van de game worden vervangen (een sprong
 * aan het begin van de functie) door functies die twee (of meer) IRTrack-apparaten laten zien en per geweer een gezonde
 * berichtenstroom geven. De knoppen komen uit het bordbeeld van de daemon (/dev/shm/ravengun_JvsState) en gaan in het G-bericht,
 * zoals het geweer ze zelf zou sturen: per knop een teller van overgangen (bit 0 = nu ingedrukt), modulo 16. Het richtpunt rekent
 * de game normaal uit de LED-vlekken uit; de opstelling van die LED's is niet bekend, dus het richtpunt komt rechtstreeks: de
 * getter waarmee de geweerdriver het richtpunt (0..1 per as) ophaalt wordt vervangen, en de LED-zoeker meldt "LED's gezien".
 *
 * WAT waar zit staat in een TABEL per game (irtrack.ini, naast rt-invoer.ini), niet in deze code. Een regel per ding:
 *
 *   build      adres=0xffc250 signatuur=415741564989fe41       alleen op deze build iets doen (de bytes op dat adres moeten kloppen)
 *   ftdi       read=0xffc250 write=0xffbe00 ...                 de libftdi-functies van de game (namen hieronder, H_NAAM)
 *   verbinding basis=0x1a7cb20 aantal=32 stap=0x1348 ftdi=0x8 speler=0x3c   de verbindingstabel: welk geweer bij welke speler hoort
 *   geweer     serie=RVG00001                                   een apparaat (in deze volgorde gevonden)
 *   speler     nr=1 a=7 b=8 c=9 x=12 y=13                       knoppen (bits) en assen (bytes) van speler nr in het bordbeeld;
 *                                                               a, b, c zijn de drie knop-nibbles van het G-bericht (a = trekker)
 *   richtpunt  functie=0xdd6c60 inverteer_x=nee inverteer_y=nee de getter (speler, float *x, float *y) die vervangen wordt
 *   leds       functie=0xdd8350 status=0xdd87f0 slot=5          de LED-zoeker (speler, *x, *y, *n): "gezien", en statusslot ok
 *   kasttype   functie=0xff0000 waarde=8                        de functie die de kasttype-byte uit de dongle leest (obj, *byte)
 *
 * Een tabel die niet klopt wordt in zijn geheel afgekeurd, met een melding op stderr, en dan doet de bibliotheek NIETS. Een
 * andere build (de signatuur klopt niet) ook: dan wordt er niets vervangen.
 *
 * Gebruik: LD_PRELOAD=/pad/librg_irtrack.so:... ; RG_IRT_TABLE=pad (anders irtrack.ini naast deze bibliotheek), RG_IRT_SHM=pad
 * (standaard /dev/shm/ravengun_JvsState), RG_IRT_LOG=1 (meldingen op stderr).
 * Eigen code.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* De identiteit van het apparaat: dit IS wat een IRTrack-geweer over USB zegt (de game vergelijkt letterlijk, met de NUL). */
#define IRT_VID 0x0403
#define IRT_PID 0x6001
static const char IRT_FABRIKANT[] = "Raw Thrills, Inc.";
static const char IRT_PRODUCT[] = "IRTrack - Rev. 2";

enum { H_NEW, H_FREE, H_FIND_ALL, H_LIST_FREE, H_GET_STRINGS, H_RESET, H_PURGE, H_CLOSE, H_BAUD, H_OPEN_DEV, H_OPEN_DESC,
       H_WRITE, H_READ, H_BITMODE, H_ERRSTR, H_N };
static const char *const H_NAAM[H_N] = {"new", "free", "find_all", "list_free", "get_strings", "reset", "purge", "close",
                                        "baudrate", "open_dev", "open_desc", "write", "read", "bitmode", "error_string"};

#define IRT_MAX_GEWEER 8
#define IRT_BEELD 64

struct irt_speler { int a, b, c, x, y; };   /* bits (-1 = geen) en asbytes van een speler in het bordbeeld */

struct irt_tabel {
    uintptr_t build; unsigned char sig[32]; int nsig;
    uintptr_t ftdi[H_N];
    int verbinding; uintptr_t vb_basis, vb_stap, vb_ftdi, vb_speler; int vb_aantal;
    char serie[IRT_MAX_GEWEER][32]; int ngeweer;
    struct irt_speler speler[IRT_MAX_GEWEER]; int nspeler;
    uintptr_t richt; int inv_x, inv_y;
    uintptr_t leds, leds_status; int leds_slot;
    uintptr_t kast; int kast_waarde;
};
static struct irt_tabel T;

static int log_on;
static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...) {
    if (!log_on) return;
    va_list ap; va_start(ap, fmt);
    fputs("rg_irtrack: ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

/* --- het bordbeeld van de daemon --------------------------------------------------------------------------------------- */
/* pread, geen mmap: een schrijver die het bestand even afkapt zou een mapping ongeldig maken (SIGBUS in de game). Een lezing
   per 2 ms is genoeg; alle geweren delen hem. */
static pthread_mutex_t beeld_mu = PTHREAD_MUTEX_INITIALIZER;
static uint8_t beeld[IRT_BEELD];
static int beeld_n;
static uint64_t beeld_t;
static int beeld_fd = -1;
static const char *beeld_pad = "/dev/shm/ravengun_JvsState";

static void beeld_lees(uint8_t *uit, int *n) {
    pthread_mutex_lock(&beeld_mu);
    uint64_t t = now_ms();
    if (t >= beeld_t + 2 || !beeld_n) {
        beeld_t = t;
        if (beeld_fd < 0) beeld_fd = open(beeld_pad, O_RDONLY | O_CLOEXEC);
        ssize_t r = beeld_fd >= 0 ? pread(beeld_fd, beeld, sizeof beeld, 0) : -1;
        if (r < 0 && beeld_fd >= 0) { close(beeld_fd); beeld_fd = -1; }
        beeld_n = r > 0 ? (int)r : 0;
    }
    memcpy(uit, beeld, sizeof beeld); *n = beeld_n;
    pthread_mutex_unlock(&beeld_mu);
}
static uint32_t beeld_bits(const uint8_t *b, int n) { return n >= 12 ? (uint32_t)b[8] | ((uint32_t)b[9] << 8) | ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24) : 0; }

/* Het richtpunt van speler p (0-based) als fractie 0..1, of 0 als hij buiten beeld mikt of er geen beeld is. Buiten beeld is in het
   bordbeeld een asbyte van 0, 1, 254 of 255 (de IO-brug houdt een as binnen het scherm op 2..253, iobridge/tp_shm.hpp). */
static int richtpunt(int p, float *fx, float *fy) {
    if (p < 0 || p >= T.nspeler) return 0;
    const struct irt_speler *s = &T.speler[p];
    uint8_t b[IRT_BEELD]; int n; beeld_lees(b, &n);
    if (s->x < 0 || s->y < 0 || n <= s->x || n <= s->y) return 0;
    uint8_t bx = b[s->x], by = b[s->y];
    if (bx <= 1 || bx >= 254 || by <= 1 || by >= 254) return 0;
    float x = bx / 255.0f, y = by / 255.0f;
    *fx = T.inv_x ? 1.0f - x : x;
    *fy = T.inv_y ? 1.0f - y : y;
    return 1;
}

/* --- de geweren ---------------------------------------------------------------------------------------------------------- */
/* Een ftdi-context van de game: wij maken hem (ftdi_new) en houden de toestand hier, per pointer. */
struct irt_ctx {
    void *ftdi;
    int geweer;               /* -1 = niet open */
    int speler;               /* de speler die de game dit geweer gaf (uit de verbindingstabel), -1 = nog niet bekend */
    char uit[128]; int pos, len;
    uint64_t volgende_x, volgende_g, volgende_y;
    uint8_t nib[3]; int was[3];
    uint64_t berichten;
};
#define IRT_MAX_CTX 64
static struct irt_ctx ctxs[IRT_MAX_CTX];
static pthread_mutex_t ctx_mu = PTHREAD_MUTEX_INITIALIZER;
static char geweer_token[IRT_MAX_GEWEER];   /* "libusb_device" per geweer: alleen het adres telt */

static struct irt_ctx *ctx_van(void *ftdi) {
    for (int i = 0; i < IRT_MAX_CTX; i++) if (ctxs[i].ftdi == ftdi && ftdi) return &ctxs[i];
    return 0;
}
static int geweer_van_token(const void *dev) {
    for (int i = 0; i < T.ngeweer; i++) if (dev == &geweer_token[i]) return i;
    return -1;
}

/* Welke speler gaf de game deze verbinding? De verbindingstabel van de game heeft per slot de ftdi-pointer en de speler. */
static int speler_van(struct irt_ctx *c) {
    if (c->speler >= 0) return c->speler;
    if (T.verbinding) {
        for (int i = 0; i < T.vb_aantal; i++) {
            uintptr_t slot = T.vb_basis + (uintptr_t)i * T.vb_stap;
            void *f; memcpy(&f, (void *)(slot + T.vb_ftdi), sizeof f);
            if (f == c->ftdi) {
                int p; memcpy(&p, (void *)(slot + T.vb_speler), sizeof p);
                if (p >= 0 && p < IRT_MAX_GEWEER) { c->speler = p; logf_("geweer %s is speler %d (verbindingsslot %d)", T.serie[c->geweer], p + 1, i); return p; }
            }
        }
    }
    return c->geweer;   /* nog niet in de tabel: de volgorde van de geweren */
}

/* Een bericht achter de uitvoer: tekst + 'z' + controlesom (8-bits som van alles vanaf de soortletter t/m de 'z', 2 hex). */
static void bericht(struct irt_ctx *c, const char *tekst) {
    unsigned som = 0; size_t n = strlen(tekst);
    if ((size_t)c->len + n + 3 >= sizeof c->uit) return;
    for (size_t i = 0; i < n; i++) som += (unsigned char)tekst[i];
    som += 'z';
    memcpy(c->uit + c->len, tekst, n); c->len += (int)n;
    c->len += snprintf(c->uit + c->len, sizeof c->uit - (size_t)c->len, "z%02X", som & 0xff);
    c->berichten++;
}

/* De volgende berichten van een geweer. X (geen vlekken: het richtpunt komt niet uit vlekken) elke 8 ms: de game wil gaten
   van hooguit 100 ms. G bij elke verandering van een knop en anders elke 50 ms (de game wil er een per 5 s). Y elke seconde
   (per 15 s nodig): beeldteller 150 (de game wil 141..159), geen vastzittende bits, firmware 01. */
static void vul(struct irt_ctx *c) {
    uint64_t t = now_ms();
    if (t < c->volgende_x) { uint64_t w = c->volgende_x - t; if (w > 10) w = 10; usleep((useconds_t)w * 1000); t = now_ms(); }
    c->pos = c->len = 0;
    bericht(c, "X00");
    c->volgende_x = t + 8;

    int p = speler_van(c);
    uint8_t b[IRT_BEELD]; int n; beeld_lees(b, &n);
    uint32_t bits = beeld_bits(b, n);
    int veranderd = 0;
    if (p >= 0 && p < T.nspeler) {
        const int bron[3] = {T.speler[p].a, T.speler[p].b, T.speler[p].c};
        for (int k = 0; k < 3; k++) {
            int nu = bron[k] >= 0 && bron[k] < 32 ? (int)((bits >> bron[k]) & 1) : 0;
            if (nu != c->was[k]) { c->nib[k] = (uint8_t)((c->nib[k] + 1) & 0xf); c->was[k] = nu; veranderd = 1; }
        }
    }
    if (veranderd || t >= c->volgende_g) {
        char g[8]; snprintf(g, sizeof g, "G%X%X%X", c->nib[0], c->nib[1], c->nib[2]);
        bericht(c, g);
        c->volgende_g = t + 50;
    }
    if (t >= c->volgende_y) { bericht(c, "Y0096000000000000000100"); c->volgende_y = t + 1000; }
}

/* --- de vervangende libftdi-functies (zelfde handtekening als libftdi 1.x) ------------------------------------------- */
struct irt_lijst { struct irt_lijst *next; void *dev; };   /* struct ftdi_device_list */

static void *irt_new(void) {
    void *f = calloc(1, 0x100);   /* >= sizeof(struct ftdi_context) (0x70); usb_dev (+0x08) blijft NULL: de game slaat zijn USB-reset dan over */
    if (!f) return 0;
    pthread_mutex_lock(&ctx_mu);
    for (int i = 0; i < IRT_MAX_CTX; i++) if (!ctxs[i].ftdi) { memset(&ctxs[i], 0, sizeof ctxs[i]); ctxs[i].ftdi = f; ctxs[i].geweer = -1; ctxs[i].speler = -1; break; }
    pthread_mutex_unlock(&ctx_mu);
    return f;
}
static void irt_free(void *f) {
    if (!f) return;
    pthread_mutex_lock(&ctx_mu);
    struct irt_ctx *c = ctx_van(f);
    if (c) { if (c->geweer >= 0) logf_("geweer %s dicht na %llu berichten", T.serie[c->geweer], (unsigned long long)c->berichten); c->ftdi = 0; }
    pthread_mutex_unlock(&ctx_mu);
    if (c) free(f);
}
static int irt_find_all(void *f, struct irt_lijst **lijst, int vid, int pid) {
    (void)f;
    if (!lijst) return -1;
    *lijst = 0;
    if (vid != IRT_VID || pid != IRT_PID) return 0;
    struct irt_lijst **staart = lijst;
    int n = 0;
    for (int i = 0; i < T.ngeweer; i++) {
        struct irt_lijst *e = calloc(1, sizeof *e);
        if (!e) break;
        e->dev = &geweer_token[i]; *staart = e; staart = &e->next; n++;
    }
    return n;
}
static void irt_list_free(struct irt_lijst **lijst) {
    if (!lijst) return;
    for (struct irt_lijst *e = *lijst, *v; e; e = v) { v = e->next; free(e); }
    *lijst = 0;
}
static int irt_get_strings(void *f, void *dev, char *fab, int nfab, char *prod, int nprod, char *serie, int nserie) {
    (void)f;
    int g = geweer_van_token(dev);
    if (g < 0) return -1;
    if (fab && nfab > 0) snprintf(fab, (size_t)nfab, "%s", IRT_FABRIKANT);
    if (prod && nprod > 0) snprintf(prod, (size_t)nprod, "%s", IRT_PRODUCT);
    if (serie && nserie > 0) snprintf(serie, (size_t)nserie, "%s", T.serie[g]);
    return 0;
}
static int bind(void *f, int g) {
    pthread_mutex_lock(&ctx_mu);
    struct irt_ctx *c = ctx_van(f);
    if (c) { c->geweer = g; c->speler = -1; c->pos = c->len = 0; c->volgende_x = c->volgende_g = c->volgende_y = 0;
             memset(c->nib, 0, sizeof c->nib); memset(c->was, 0, sizeof c->was); }
    pthread_mutex_unlock(&ctx_mu);
    if (!c) return -3;
    logf_("geweer %s geopend", T.serie[g]);
    return 0;
}
static int irt_open_desc(void *f, int vid, int pid, const char *desc, const char *serie) {
    if (!f || vid != IRT_VID || pid != IRT_PID) return -3;
    if (desc && strcmp(desc, IRT_PRODUCT) != 0) return -3;
    for (int i = 0; i < T.ngeweer; i++) if (!serie || !strcmp(serie, T.serie[i])) return bind(f, i);
    return -3;   /* "device not found" */
}
static int irt_open_dev(void *f, void *dev) { int g = geweer_van_token(dev); return g < 0 || !f ? -3 : bind(f, g); }
static int irt_close(void *f) {
    pthread_mutex_lock(&ctx_mu);
    struct irt_ctx *c = ctx_van(f);
    if (c) c->geweer = -1;
    pthread_mutex_unlock(&ctx_mu);
    return 0;
}
static int irt_nul(void *f) { (void)f; return 0; }
static int irt_baud(void *f, int baud) { (void)f; logf_("baudrate %d", baud); return 0; }
static int irt_bitmode(void *f, unsigned char masker, unsigned char modus) { (void)f; (void)masker; (void)modus; return 0; }
static int irt_write(void *f, const unsigned char *buf, int n) {
    if (n <= 0 || !buf) return n;
    pthread_mutex_lock(&ctx_mu);
    struct irt_ctx *c = ctx_van(f);
    if (c && buf[0] == 'W' && n >= 2 && buf[1] == '7') c->volgende_y = 0;   /* "stuur je status": de volgende ronde een Y */
    int g = c ? c->geweer : -1;
    pthread_mutex_unlock(&ctx_mu);
    if (log_on && buf[0] == 'W') logf_("opdracht aan geweer %s: %.*s", g >= 0 ? T.serie[g] : "?", n > 16 ? 16 : n, (const char *)buf);
    return n;   /* ook de FPGA-reset (256 nullen) wil precies n terug */
}
static int irt_read(void *f, unsigned char *buf, int n) {
    if (!buf || n <= 0) return 0;
    pthread_mutex_lock(&ctx_mu);
    struct irt_ctx *c = ctx_van(f);
    int open = c && c->geweer >= 0;
    pthread_mutex_unlock(&ctx_mu);
    if (!open) { usleep(10000); return 0; }
    /* Alleen de leesdraad van deze verbinding komt hier: de context zelf hoeft niet onder het slot. */
    if (c->pos >= c->len) vul(c);
    int k = c->len - c->pos;
    if (k > n) k = n;
    memcpy(buf, c->uit + c->pos, (size_t)k); c->pos += k;
    return k;
}
static const char *irt_errstr(void *f) { (void)f; return "all fine"; }

/* --- de game-functies rond het richtpunt en het kasttype ------------------------------------------------------------- */
/* De getter van de geweerdriver: (speler, *x, *y), 0..1 per as, (-1, -1) = buiten beeld / geen richtpunt. */
static void irt_richt(int p, float *x, float *y) {
    if (!x || !y) return;
    float fx, fy;
    if (richtpunt(p, &fx, &fy)) { *x = fx; *y = fy; } else { *x = -1.0f; *y = -1.0f; }
}
/* De LED-zoeker: (speler, *x, *y, *n) -> 0 = gevonden. Het richtpunt in camerapunten (640 x 480), n = aantal LED's; het
   statusslot "LED's gezien" van de game op ok, zoals de game zelf doet als n > 0. */
static int irt_leds(int p, float *x, float *y, float *n) {
    float fx, fy; int ok = richtpunt(p, &fx, &fy);
    if (x) *x = ok ? fx * 640.0f : -1.0f;
    if (y) *y = ok ? fy * 480.0f : -1.0f;
    if (n) *n = 8.0f;
    if (T.leds_status) ((void (*)(int, unsigned))T.leds_status)(p, (unsigned)T.leds_slot);
    return ok ? 0 : -1;
}
/* De kasttype-byte (dezelfde instelling als CabinetType in het TeknoParrot-profiel): (dongle-object, *byte) -> 0 = gelezen. */
static int irt_kast(void *obj, unsigned char *uit) { (void)obj; if (uit) *uit = (unsigned char)T.kast_waarde; return 0; }

static void *const H_FUNC[H_N] = {
    (void *)irt_new, (void *)irt_free, (void *)irt_find_all, (void *)irt_list_free, (void *)irt_get_strings, (void *)irt_nul,
    (void *)irt_nul, (void *)irt_close, (void *)irt_baud, (void *)irt_open_dev, (void *)irt_open_desc, (void *)irt_write,
    (void *)irt_read, (void *)irt_bitmode, (void *)irt_errstr};

/* --- de tabel lezen -------------------------------------------------------------------------------------------------- */
static int toegang(uintptr_t a, size_t len, char soort) {   /* soort 'r' of 'x' */
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char regel[512]; int ok = 0;
    while (fgets(regel, sizeof regel, f)) {
        unsigned long lo, hi; char perm[8];
        if (sscanf(regel, "%lx-%lx %7s", &lo, &hi, perm) == 3 && a >= lo && a + len <= hi) { ok = soort == 'x' ? perm[2] == 'x' : perm[0] == 'r'; break; }
    }
    fclose(f);
    return ok;
}
static int getal(const char *s, long long *uit) {
    if (!s || !*s) return 0;
    char *e = 0; *uit = strtoll(s, &e, 0);
    return e && *e == 0;
}
static int janee(const char *s, int *uit) {
    if (!s) return 0;
    if (!strcmp(s, "ja") || !strcmp(s, "1")) { *uit = 1; return 1; }
    if (!strcmp(s, "nee") || !strcmp(s, "0")) { *uit = 0; return 1; }
    return 0;
}
struct paar { char *k, *v; int gebruikt; };
static const char *zoek(struct paar *p, int n, const char *k) {
    for (int i = 0; i < n; i++) if (!strcmp(p[i].k, k)) { p[i].gebruikt = 1; return p[i].v; }
    return 0;
}

#define FOUT(...) do { snprintf(fout, nfout, __VA_ARGS__); free(kopie); return 0; } while (0)
static int tabel_lees(const char *tekst, struct irt_tabel *t, char *fout, size_t nfout) {
    memset(t, 0, sizeof *t);
    for (int i = 0; i < IRT_MAX_GEWEER; i++) t->speler[i] = (struct irt_speler){-1, -1, -1, -1, -1};
    int nr = 0;
    char *kopie = strdup(tekst), *volgende = 0;
    if (!kopie) { snprintf(fout, nfout, "geen geheugen"); return 0; }
    for (char *regel = kopie; regel && *regel; regel = volgende) {
        nr++;
        char *nl = strchr(regel, '\n');
        if (nl) { *nl = 0; volgende = nl + 1; } else volgende = 0;
        char *c = strpbrk(regel, "#;"); if (c) *c = 0;
        while (*regel && isspace((unsigned char)*regel)) regel++;
        if (!*regel) continue;
        char *rest = regel; while (*rest && !isspace((unsigned char)*rest)) rest++;
        if (*rest) *rest++ = 0;
        struct paar p[24]; int n = 0;
        for (char *w = strtok(rest, " \t\r"); w; w = strtok(0, " \t\r")) {
            char *eq = strchr(w, '=');
            if (!eq || eq == w || n >= 24) FOUT("regel %d: na '%s' hoort sleutel=waarde te staan", nr, regel);
            *eq = 0; p[n].k = w; p[n].v = eq + 1; p[n].gebruikt = 0; n++;
        }
        long long v;
#define ADRES(naam, doel) do { if (!getal(zoek(p, n, naam), &v) || v <= 0) FOUT("regel %d (%s): '%s' ontbreekt of is geen adres", nr, regel, naam); doel = (uintptr_t)v; } while (0)
#define GETAL(naam, doel, lo, hi) do { if (!getal(zoek(p, n, naam), &v) || v < (lo) || v > (hi)) FOUT("regel %d (%s): '%s' ontbreekt of ligt niet tussen %d en %d", nr, regel, naam, (int)(lo), (int)(hi)); doel = (int)v; } while (0)
        if (!strcmp(regel, "build")) {
            ADRES("adres", t->build);
            const char *s = zoek(p, n, "signatuur"); size_t len = s ? strlen(s) : 0;
            if (!s || len < 2 || len % 2 || len / 2 > sizeof t->sig) FOUT("regel %d (build): 'signatuur' ontbreekt of is geen reeks hex-bytes", nr);
            for (size_t i = 0; i < len; i += 2) {
                char h[3] = {s[i], s[i + 1], 0};
                if (!isxdigit((unsigned char)h[0]) || !isxdigit((unsigned char)h[1])) FOUT("regel %d (build): 'signatuur' is geen reeks hex-bytes", nr);
                t->sig[i / 2] = (unsigned char)strtoul(h, 0, 16);
            }
            t->nsig = (int)(len / 2);
        } else if (!strcmp(regel, "ftdi")) {
            for (int h = 0; h < H_N; h++) { const char *s = zoek(p, n, H_NAAM[h]); if (s) { if (!getal(s, &v) || v <= 0) FOUT("regel %d (ftdi): '%s' is geen adres", nr, H_NAAM[h]); t->ftdi[h] = (uintptr_t)v; } }
        } else if (!strcmp(regel, "verbinding")) {
            ADRES("basis", t->vb_basis); ADRES("stap", t->vb_stap);
            GETAL("aantal", t->vb_aantal, 1, 256); GETAL("ftdi", v, 0, 0x10000); t->vb_ftdi = (uintptr_t)v; GETAL("speler", v, 0, 0x10000); t->vb_speler = (uintptr_t)v;
            t->verbinding = 1;
        } else if (!strcmp(regel, "geweer")) {
            if (t->ngeweer >= IRT_MAX_GEWEER) FOUT("regel %d: te veel geweren (hooguit %d)", nr, IRT_MAX_GEWEER);
            const char *s = zoek(p, n, "serie");
            if (!s || !*s || strlen(s) >= sizeof t->serie[0]) FOUT("regel %d (geweer): 'serie' ontbreekt of is langer dan %d tekens", nr, (int)sizeof t->serie[0] - 1);
            snprintf(t->serie[t->ngeweer++], sizeof t->serie[0], "%s", s);
        } else if (!strcmp(regel, "speler")) {
            int s; GETAL("nr", s, 1, IRT_MAX_GEWEER);
            struct irt_speler *sp = &t->speler[s - 1];
            const char *w;
            if ((w = zoek(p, n, "a"))) { if (!getal(w, &v) || v < -1 || v > 31) FOUT("regel %d (speler): 'a' is een bit 0-31 of -1", nr); sp->a = (int)v; }
            if ((w = zoek(p, n, "b"))) { if (!getal(w, &v) || v < -1 || v > 31) FOUT("regel %d (speler): 'b' is een bit 0-31 of -1", nr); sp->b = (int)v; }
            if ((w = zoek(p, n, "c"))) { if (!getal(w, &v) || v < -1 || v > 31) FOUT("regel %d (speler): 'c' is een bit 0-31 of -1", nr); sp->c = (int)v; }
            GETAL("x", sp->x, 0, IRT_BEELD - 1); GETAL("y", sp->y, 0, IRT_BEELD - 1);
            if (s > t->nspeler) t->nspeler = s;
        } else if (!strcmp(regel, "richtpunt")) {
            ADRES("functie", t->richt);
            const char *w;
            if ((w = zoek(p, n, "inverteer_x")) && !janee(w, &t->inv_x)) FOUT("regel %d (richtpunt): 'inverteer_x' is ja of nee", nr);
            if ((w = zoek(p, n, "inverteer_y")) && !janee(w, &t->inv_y)) FOUT("regel %d (richtpunt): 'inverteer_y' is ja of nee", nr);
        } else if (!strcmp(regel, "leds")) {
            ADRES("functie", t->leds); ADRES("status", t->leds_status); GETAL("slot", t->leds_slot, 0, 63);
        } else if (!strcmp(regel, "kasttype")) {
            ADRES("functie", t->kast); GETAL("waarde", t->kast_waarde, 1, 255);
        } else FOUT("regel %d: onbekend woord '%s' (build, ftdi, verbinding, geweer, speler, richtpunt, leds of kasttype)", nr, regel);
        for (int i = 0; i < n; i++) if (!p[i].gebruikt) FOUT("regel %d (%s): onbekende sleutel '%s'", nr, regel, p[i].k);
    }
    free(kopie);
    if (!t->nsig) { snprintf(fout, nfout, "geen 'build'-regel: zonder signatuur wordt er niets vervangen"); return 0; }
    int nftdi = 0; for (int h = 0; h < H_N; h++) nftdi += t->ftdi[h] != 0;
    if (nftdi && (!t->ftdi[H_NEW] || !t->ftdi[H_FIND_ALL] || !t->ftdi[H_GET_STRINGS] || !t->ftdi[H_READ] || !(t->ftdi[H_OPEN_DESC] || t->ftdi[H_OPEN_DEV]))) {
        snprintf(fout, nfout, "ftdi: new, find_all, get_strings, read en open_desc of open_dev horen er allemaal bij"); return 0; }
    if (nftdi && !t->ngeweer) { snprintf(fout, nfout, "ftdi zonder 'geweer'-regels: er is niets om te laten zien"); return 0; }
    return 1;
}

/* --- vervangen ----------------------------------------------------------------------------------------------------- */
/* Een sprong aan het begin van de functie: jmp *0(%rip) + het doeladres (14 bytes). Alles wat de functie deed vervalt; ze
   moet dus minstens 14 bytes lang zijn (alle functies in de tabel zijn dat, ook ftdi_usb_open_desc met zijn opvulling). */
static int vervang(uintptr_t doel, void *nieuw, const char *naam) {
    if (!toegang(doel, 14, 'x')) { fprintf(stderr, "rg_irtrack: FOUT: %s (0x%lx) ligt niet in uitvoerbare code; niets vervangen\n", naam, (unsigned long)doel); return 0; }
    unsigned char code[14] = {0xff, 0x25, 0, 0, 0, 0};
    memcpy(code + 6, &nieuw, 8);
    uintptr_t pg = doel & ~(uintptr_t)0xfff, eind = (doel + sizeof code + 0xfff) & ~(uintptr_t)0xfff;
    if (mprotect((void *)pg, eind - pg, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) { fprintf(stderr, "rg_irtrack: FOUT: mprotect bij %s\n", naam); return 0; }
    memcpy((void *)doel, code, sizeof code);
    mprotect((void *)pg, eind - pg, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)doel, (char *)doel + sizeof code);
    logf_("%s (0x%lx) vervangen", naam, (unsigned long)doel);
    return 1;
}

static int tabel_kies(char *pad, size_t npad) {
    const char *env = getenv("RG_IRT_TABLE");
    if (env && *env) { snprintf(pad, npad, "%s", env); return 1; }
    Dl_info info;
    if (dladdr((void *)tabel_kies, &info) && info.dli_fname) {
        const char *s = strrchr(info.dli_fname, '/');
        if (s) { snprintf(pad, npad, "%.*s/irtrack.ini", (int)(s - info.dli_fname), info.dli_fname); if (access(pad, R_OK) == 0) return 1; }
    }
    return 0;
}

#ifndef RG_IRTRACK_TEST
__attribute__((constructor)) static void rg_irtrack_init(void) {
    /* Alleen in het proces van de game zelf (de game start hulpprogramma's die de preload erven). */
    char exe[256]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n < 5) return;
    exe[n] = 0;
    if (strcmp(exe + n - 5, "/game") != 0) return;
    const char *e = getenv("RG_IRT_LOG"); log_on = e && *e == '1';
    const char *shm = getenv("RG_IRT_SHM"); if (shm && *shm) beeld_pad = shm;

    char pad[1024];
    if (!tabel_kies(pad, sizeof pad)) { logf_("geen irtrack.ini: niets vervangen"); return; }
    FILE *f = fopen(pad, "r");
    if (!f) { fprintf(stderr, "rg_irtrack: FOUT: de tabel %s is niet te lezen; geen geweren\n", pad); return; }
    static char tekst[32768]; size_t m = fread(tekst, 1, sizeof tekst - 1, f); fclose(f); tekst[m] = 0;
    char fout[256];
    if (!tabel_lees(tekst, &T, fout, sizeof fout)) { fprintf(stderr, "rg_irtrack: FOUT: de tabel %s klopt niet (%s); geen geweren\n", pad, fout); return; }
    if (!toegang(T.build, (size_t)T.nsig, 'r') || memcmp((void *)T.build, T.sig, (size_t)T.nsig) != 0) { logf_("andere game-build: niets vervangen"); return; }

    /* Eerst alles nakijken, dan pas vervangen: half vervangen (een geweer dat er wel is maar niets zegt) is erger dan niets. */
    int ok = 1;
    for (int h = 0; h < H_N; h++) if (T.ftdi[h] && !toegang(T.ftdi[h], 14, 'x')) ok = 0;
    if ((T.richt && !toegang(T.richt, 14, 'x')) || (T.leds && !toegang(T.leds, 14, 'x')) || (T.leds_status && !toegang(T.leds_status, 1, 'x')) || (T.kast && !toegang(T.kast, 14, 'x'))) ok = 0;
    if (T.verbinding && !toegang(T.vb_basis, (size_t)T.vb_aantal * T.vb_stap, 'r')) ok = 0;
    if (!ok) { fprintf(stderr, "rg_irtrack: FOUT: een adres uit %s ligt niet in deze game; niets vervangen\n", pad); return; }
    for (int h = 0; h < H_N && ok; h++) if (T.ftdi[h]) { char naam[40]; snprintf(naam, sizeof naam, "ftdi_%s", H_NAAM[h]); ok = vervang(T.ftdi[h], H_FUNC[h], naam); }
    if (ok && T.richt) ok = vervang(T.richt, (void *)irt_richt, "richtpunt");
    if (ok && T.leds) ok = vervang(T.leds, (void *)irt_leds, "leds");
    if (ok && T.kast) ok = vervang(T.kast, (void *)irt_kast, "kasttype");
    logf_("tabel %s: %d geweren, %d spelers, richtpunt %s, kasttype %d", pad, T.ngeweer, T.nspeler, T.richt ? "ja" : "nee", T.kast ? T.kast_waarde : 0);
}
#endif /* RG_IRTRACK_TEST */

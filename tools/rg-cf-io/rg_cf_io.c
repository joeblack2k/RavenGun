/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-cf-io -- de kast van Crossfire: Maximum Paintball voor de native route (LD_PRELOAD, i386, eigen code, geen TeknoParrot).
 * Header-vrij (zoals tools/rg-oss-stub): alleen <stdarg.h> van de compiler, de rest zelf gedeclareerd, syscalls via syscall().
 * Bouwen met kaal `gcc -m32 -shared -fPIC` (op SteamOS ontbreken de 32-bits glibc-headers).
 *
 * WAT DE GAME VAN ZIJN KAST VRAAGT (alles statisch gemeten aan med_pball en mjg.o in de spelmap, docs/research/budgie/README.md,
 * en in deze ronde verder uitgelezen; adressen in med_pball, ET_EXEC):
 *   /dev/mjg   de kernelmodule mjg.o van het MegaJamma-bord. read() geeft een record van 0xb8 bytes of 0 (niets nieuws). Indeling
 *              (stuff_curr_mjd in mjg.o, de verwerking 0x804a862 / 0x804a63b in de game):
 *                +0x00 misc-guns (16 bit, bord-register 0x5e): bits 0-2 aantal geweer-samples van geweer 1 (0..4), bit 12 = schot van
 *                      geweer 1; bits 4-6 aantal samples van geweer 2, bit 13 = schot van geweer 2 (tabellen 0x806e38c/0x806e394/0x806e39c)
 *                +0x04 schakelaars (32 bit, bord-register 0x00); de game leest ze als niveau (positieve logica: een drukknop = bit gezet)
 *                +0x0c irq-teller: MOET per record strikt oplopen, anders "irq's out of order" en exit (0x804aa40)
 *                +0x10 + geweer*16 + (i&3)*4: vier samples per geweer; een sample = x | y<<12, twaalf bits elk. De game middelt ze
 *                      (met weglating van min en max bij meer dan twee) en rekent om: scherm_x = raw_x * 0.6946 - 170, scherm_y = raw_y - 25
 *                      (0x804a63b); zonder samples (0) is het richtpunt -1,-1 = "buiten beeld"
 *                +0x30/+0x34 muntentellers (stijgende flanken van bit 0x200000 en 0x100000 in +0x04; mogen alleen oplopen, anders
 *                      "invalid delta" en exit), +0x38 = 0x7d, +0x3c volgnummer
 *              ioctl's (0x40044d02..0x40044d09: registers schrijven, lampen) slagen en doen niets.
 *   /dev/hda   HDIO_GET_IDENTITY (0x30d): het serienummer (offset 0x14, 20 bytes) moet gelijk zijn aan de eerste 0x14 tekens van
 *              adjust.dat uit de eigen spelmap (een '-' in adjust.dat telt niet mee; 0x8056ec7). Dit antwoord komt uit dat eigen bestand.
 *   /dev/p37c  het registerbeeld van hetzelfde bord, byte voor byte (de game doet dat zelf als hij geen root is; als root doet hij iopl
 *              en in/out). Schrijven: 0x2n/0x3n = adres (laag/hoog), 0x0n/0x1n = data (laag, dan hoog: voert de schrijfactie uit);
 *              lezen: het register op het huidige adres. De game gebruikt het alleen voor de iButton (zie hieronder).
 *   iButton    een Dallas DS1991 (multi-key, 1-Wire) op registers 0x10 (commando), 0x80 (data), 0x81 (status: laag nibble 0 = klaar,
 *              bit 0x10 = aanwezig). Controle 0x805e7ca: reset; 0x33 (ROM lezen: familie 0x02, 6 serienummer-bytes, crc); 0x66
 *              (subsleutel lezen) + controlebyte ((n&3)<<6|0x10) + zijn complement; acht bytes lezen (ID); acht wachtwoordbytes
 *              schrijven; achtenveertig bytes lezen (data). Subsleutel 0 en 1 worden vergeleken met constanten die IN de game staan
 *              (0x806fbc8/0x806fbe0 en 0x806fc18/0x806fc20); de waarden zijn zichtbaar patroondata (aa55 + oplopende bytes). Zonder
 *              iButton ("no ibutton detected") komt de game in de opstart niet verder (0x8050c1b). Deze hulp antwoordt met die eigen
 *              constanten uit het geheugen van het draaiende spel (niets staat in deze repo) en toetst geen wachtwoord of serienummer.
 *   /proc/version moet met "Linux version 2.4.22 (root@midas) ..." beginnen: dat doet een mount in native.json (files), niet deze hulp.
 *
 * INVOER (bordbeeld van de daemon, /dev/shm/ravengun_JvsState, indeling RawThrillsGUN: bits op offset 8, assen op 12/16/20/24):
 *   schakelaars: standaardtabel hieronder (munt 1/2 = 0x200000/0x100000, START 1/2 = 0x80000/0x40000: aangenomen uit de volgorde van de
 *   speler-index in de game, NIET gemeten) en te overschrijven met RG_CF_MAP="shmbit:masker,...". Geweer: de as bytes 2..253 =
 *   het scherm, buiten (<=1 of >=254) = buiten beeld; de trekker is een schot-bit in precies een record (herladen = schot buiten beeld).
 *
 * Omgeving: RG_CF_LOG=1; RG_CF_SHM=pad; RG_CF_W/RG_CF_H = de afmetingen van het spelbeeld waarin het richtpunt valt (640x480);
 *   RG_CF_AIM_X0/X1/Y0/Y1 = strook van het scherm (0..1) waar het spelbeeld staat; RG_CF_INVX/INVY=1; RG_CF_HDSERIAL=<tekst>
 *   (anders adjust.dat in de werkmap); RG_CF_SWITCHES=<hex> (vaste schakelaars erbij, voor onderzoek); RG_CF_MAP=...
 */
#include <stdarg.h>

extern long syscall(long, ...);
extern char *getenv(const char *);
extern int strcmp(const char *, const char *);

#define NR_read 3
#define NR_write 4
#define NR_open 5
#define NR_close 6
#define NR_lseek 19
#define NR_ioctl 54
#define NR_openat 295
#define NR_clock_gettime 265
#define MAXFD 1024

enum { K_NONE = 0, K_P37C = 1, K_MJG = 2, K_HDA = 3 };
static unsigned char g_kind[MAXFD];

static int g_log, g_klaar, g_gemeld[16];
static unsigned int g_cyc[16], g_ncyc, g_cyc_i, g_cyc_n, g_cyc_was;
static unsigned long g_mem;   /* 0 voor de game; de toets legt er een eigen geheugen onder (RG_CF_TEST_MEM) */

/* ----- logboek -------------------------------------------------------------------------------------------------------- */
static void melding(const char *wat, int soort)
{
    if (!g_log || soort < 0 || soort >= 16 || g_gemeld[soort]++ >= (soort == 15 ? 60 : soort >= 12 && soort <= 13 ? 60 : 3)) return;
    char b[120];
    unsigned int p = 0;
    const char *v = "rg-cf-io: ";
    while (*v) b[p++] = *v++;
    while (*wat && p < 110) b[p++] = *wat++;
    b[p++] = '\n';
    syscall(NR_write, 2, b, (unsigned long)p);
}
static void melding_hex(const char *wat, unsigned int v, int soort)
{
    char b[96];
    unsigned int p = 0;
    while (*wat && p < 70) b[p++] = *wat++;
    static const char H[] = "0123456789abcdef";
    b[p++] = '0'; b[p++] = 'x';
    int started = 0;
    for (int s = 28; s >= 0; s -= 4) { unsigned int d = (v >> s) & 15; if (d || started || s == 0) { b[p++] = H[d]; started = 1; } }
    b[p] = 0;
    melding(b, soort);
}

static unsigned long long nu_ms(void)
{
    struct { long s; long ns; } t;
    syscall(NR_clock_gettime, 1 /* CLOCK_MONOTONIC */, &t);
    return (unsigned long long)t.s * 1000ull + (unsigned long long)t.ns / 1000000ull;
}

static unsigned int hexwaarde(const char *s)
{
    unsigned int v = 0;
    for (; *s; ++s) {
        unsigned int d = (*s >= '0' && *s <= '9') ? (unsigned)(*s - '0') : (*s >= 'a' && *s <= 'f') ? (unsigned)(*s - 'a' + 10) : (*s >= 'A' && *s <= 'F') ? (unsigned)(*s - 'A' + 10) : 16u;
        if (d > 15) break;
        v = v * 16 + d;
    }
    return v;
}
static double env_d(const char *k, double d)
{
    const char *s = getenv(k);
    if (!s || !*s) return d;
    double v = 0, f = 0.1;
    int punt = 0;
    for (; *s; ++s) {
        if (*s == '.') { punt = 1; continue; }
        if (*s < '0' || *s > '9') break;
        if (!punt) v = v * 10 + (*s - '0'); else { v += (*s - '0') * f; f /= 10; }
    }
    return v;
}

/* ----- de iButton (DS1991) ---------------------------------------------------------------------------------------------- */
#define IB_SIG_ADDR 0x805e7cau
static const unsigned char IB_SIG[13] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x78, 0xc7, 0x45, 0x90, 0x00, 0x00, 0x00, 0x00};
static const unsigned int IB_ID[2] = {0x806fbc8u, 0x806fc18u};     /* 8 bytes: wat de game als ID van subsleutel 0/1 verwacht */
static const unsigned int IB_DATA[2] = {0x806fbe0u, 0x806fc20u};   /* 48 bytes: wat de game als data van subsleutel 0/1 verwacht */
static const unsigned char IB_SERIE[6] = {0x52, 0x47, 0x43, 0x46, 0x00, 0x01};   /* eigen serienummer ("RGCF"), niets toetst het */

static int g_ib_ok;                       /* de game is de verwachte build: de constanten zijn te lezen */
enum { OW_ROM, OW_ROMKLAAR, OW_C1, OW_C2, OW_PW, OW_DATA };
static int g_ow, g_pw_n, g_subkey, g_presence;
static unsigned char g_q[64];
static unsigned int g_qn, g_qi;
static unsigned char g_hi;
static unsigned char g_reg[256];
static unsigned int g_addr;
static unsigned char g_ow_byte = 0xff;

static unsigned char crc8_dallas(const unsigned char *p, int n)
{
    unsigned char crc = 0;
    for (int i = 0; i < n; i++) {
        unsigned char b = p[i];
        for (int k = 0; k < 8; k++) {
            unsigned char mix = (crc ^ b) & 1;
            crc >>= 1;
            if (mix) crc ^= 0x8c;
            b >>= 1;
        }
    }
    return crc;
}

static void q_vul(const unsigned char *p, unsigned int n)
{
    g_qn = n > sizeof g_q ? (unsigned int)sizeof g_q : n;
    for (unsigned int i = 0; i < g_qn; i++) g_q[i] = p[i];
    g_qi = 0;
}

static void ow_schrijf(unsigned char b)
{
    switch (g_ow) {
    case OW_ROM:
        if (b == 0x33) {
            unsigned char rom[8];
            rom[0] = 0x02;
            for (int i = 0; i < 6; i++) rom[1 + i] = IB_SERIE[i];
            rom[7] = crc8_dallas(rom, 7);
            q_vul(rom, 8);
            g_ow = OW_ROMKLAAR;
            melding("iButton: ROM gelezen (0x33)", 8);
        } else if (b == 0xcc) g_ow = OW_ROMKLAAR;
        break;
    case OW_ROMKLAAR:
        if (b == 0x66) g_ow = OW_C1;
        break;
    case OW_C1: g_subkey = (b >> 6) & 3; g_ow = OW_C2; break;
    case OW_C2: {
        unsigned char id[8];
        for (int i = 0; i < 8; i++) id[i] = (g_ib_ok && g_subkey < 2) ? *(const unsigned char *)(g_mem + IB_ID[g_subkey] + (unsigned)i) : 0;
        q_vul(id, 8);
        g_pw_n = 0;
        g_ow = OW_PW;
        melding(g_subkey == 0 ? "iButton: subsleutel 0 gevraagd" : "iButton: subsleutel 1 gevraagd", g_subkey == 0 ? 9 : 10);
        break;
    }
    case OW_PW:
        if (++g_pw_n >= 8) {
            unsigned char d[48];
            for (int i = 0; i < 48; i++) d[i] = (g_ib_ok && g_subkey < 2) ? *(const unsigned char *)(g_mem + IB_DATA[g_subkey] + (unsigned)i) : 0;
            q_vul(d, 48);
            g_ow = OW_DATA;
        }
        break;
    default: break;
    }
}

static void ow_cmd(unsigned char v)
{
    if (v == 0x00) { g_ow = OW_ROM; g_presence = 1; g_qn = g_qi = 0; return; }   /* reset */
    g_presence = 0;
    if (v == 0x10) { g_ow_byte = g_qi < g_qn ? g_q[g_qi++] : 0xff; return; }      /* byte lezen */
    if ((v >> 4) == 2) { g_hi = (unsigned char)(v & 15); return; }                 /* byte schrijven: hoge nibble */
    if ((v >> 4) == 3) { ow_schrijf((unsigned char)((g_hi << 4) | (v & 15))); return; }   /* ... lage nibble: uitvoeren */
}

static unsigned char reg_lees(unsigned int a)
{
    a &= 0xff;
    if (a == 0x81) return (unsigned char)(g_presence ? 0x10 : 0x00);   /* laag nibble 0 = klaar */
    if (a == 0x80) return g_ow_byte;
    return g_reg[a];
}

static unsigned char g_dlo;
static void poort_uit(unsigned char b)
{
    switch (b >> 4) {
    case 2: g_addr = (g_addr & 0xf0) | (b & 0x0f); break;
    case 3: g_addr = (g_addr & 0x0f) | ((unsigned)(b & 0x0f) << 4); break;
    case 0: g_dlo = (unsigned char)(b & 15); break;
    case 1: {
        unsigned char v = (unsigned char)((((b & 15) << 4)) | g_dlo);
        if ((g_addr & 0xff) == 0x10) ow_cmd(v); else g_reg[g_addr & 0xff] = v;
        break;
    }
    default: break;
    }
}

/* ----- invoer: het bordbeeld van de daemon ------------------------------------------------------------------------------ */
#define MAXMAP 16
static struct { int bit; unsigned int mask; } g_map[MAXMAP];
static int g_nmap;
static int g_shm = -1;
static unsigned int g_vast;
static double g_w = 640, g_h = 480, g_x0, g_x1 = 1, g_y0, g_y1 = 1;
static int g_invx, g_invy;
static unsigned char g_serial[21];

static void map_standaard(void)
{
    static const struct { int bit; unsigned int mask; } S[] = {
        {2, 0x200000u},   /* munt 1 */
        {3, 0x100000u},   /* munt 2 */
        {4, 0xc0000u},    /* START 1: beide bits (bench 46: 0x80000 alleen deed niets; de game vraagt per speler-index een eigen bit, 0x80000 of 0x40000) */
        {8, 0xc0000u},    /* START 2 */
    };
    for (unsigned int i = 0; i < sizeof S / sizeof S[0]; i++) { g_map[g_nmap].bit = S[i].bit; g_map[g_nmap].mask = S[i].mask; g_nmap++; }
}

static void map_van_env(const char *s)
{
    g_nmap = 0;
    while (*s && g_nmap < MAXMAP) {
        int bit = 0;
        while (*s >= '0' && *s <= '9') bit = bit * 10 + (*s++ - '0');
        if (*s != ':') break;
        ++s;
        unsigned int m = 0;
        while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F')) {
            unsigned int d = (*s >= '0' && *s <= '9') ? (unsigned)(*s - '0') : (*s >= 'a' && *s <= 'f') ? (unsigned)(*s - 'a' + 10) : (unsigned)(*s - 'A' + 10);
            m = m * 16 + d;
            ++s;
        }
        g_map[g_nmap].bit = bit; g_map[g_nmap].mask = m; g_nmap++;
        if (*s == ',') ++s;
    }
}


/* Ligt [a, a+len) in een leesbaar stuk van het proces? (/proc/self/maps; zonder libc-headers) */
static int leesbaar(unsigned long a, unsigned long len)
{
    long fd = syscall(NR_open, "/proc/self/maps", 0, 0);
    if (fd < 0) return 0;
    static char buf[16384];
    long r = syscall(NR_read, fd, buf, sizeof buf - 1);
    syscall(NR_close, fd);
    if (r <= 0) return 0;
    buf[r] = 0;
    for (char *q = buf; *q;) {
        unsigned long lo = 0, hi = 0;
        while (*q && *q != '-') { char c = *q++; lo = lo * 16 + (unsigned long)((c >= 'a') ? c - 'a' + 10 : c - '0'); }
        if (*q == '-') ++q;
        while (*q && *q != ' ') { char c = *q++; hi = hi * 16 + (unsigned long)((c >= 'a') ? c - 'a' + 10 : c - '0'); }
        if (*q == ' ') ++q;
        int lees = *q == 'r';
        if (lees && a >= lo && a + len <= hi) return 1;
        while (*q && *q != '\n') ++q;
        if (*q) ++q;
    }
    return 0;
}

static void init(void)
{
    if (g_klaar) return;
    g_klaar = 1;
    g_log = getenv("RG_CF_LOG") != 0;
#ifdef RG_CF_TEST
    { const char *m = getenv("RG_CF_TEST_MEM"); if (m) g_mem = hexwaarde(m); }
#endif
    const char *sw = getenv("RG_CF_SWITCHES");
    g_vast = sw ? hexwaarde(sw) : 0;
    const char *mp = getenv("RG_CF_MAP");
    if (mp && *mp) map_van_env(mp); else map_standaard();
    { const char *cy = getenv("RG_CF_CYCLE"); while (cy && *cy && g_ncyc < 16) { g_cyc[g_ncyc++] = hexwaarde(cy); while (*cy && *cy != ',') ++cy; if (*cy == ',') ++cy; } }
    g_w = env_d("RG_CF_W", 640); g_h = env_d("RG_CF_H", 480);
    g_x0 = env_d("RG_CF_AIM_X0", 0); g_x1 = env_d("RG_CF_AIM_X1", 1); g_y0 = env_d("RG_CF_AIM_Y0", 0); g_y1 = env_d("RG_CF_AIM_Y1", 1);
    if (g_x1 <= g_x0) { g_x0 = 0; g_x1 = 1; }
    if (g_y1 <= g_y0) { g_y0 = 0; g_y1 = 1; }
    g_invx = getenv("RG_CF_INVX") != 0; g_invy = getenv("RG_CF_INVY") != 0;
    /* de iButton-constanten zijn alleen te lezen als dit de verwachte build van de game is: de bytes van de controlefunctie kloppen */
    g_ib_ok = 1;
    if (!g_mem) {
        g_ib_ok = leesbaar(IB_SIG_ADDR, sizeof IB_SIG) && leesbaar(IB_DATA[1], 48) && leesbaar(IB_ID[0], 8);
        if (g_ib_ok) { const unsigned char *p = (const unsigned char *)(unsigned long)IB_SIG_ADDR; for (unsigned int i = 0; i < sizeof IB_SIG; i++) if (p[i] != IB_SIG[i]) g_ib_ok = 0; }
    }
    melding(g_ib_ok ? "iButton: game herkend (controlefunctie op 0x805e7ca klopt)" : "iButton: game NIET herkend, geen antwoorden", 11);
    /* serienummer van de schijf: uit het eigen bestand adjust.dat (of RG_CF_HDSERIAL) */
    const char *hs = getenv("RG_CF_HDSERIAL");
    unsigned int n = 0;
    if (hs) { while (hs[n] && n < 20) { g_serial[n] = (unsigned char)hs[n]; ++n; } }
    else {
        long fd = syscall(NR_open, "adjust.dat", 0, 0);
        if (fd >= 0) { long r = syscall(NR_read, fd, g_serial, 20); n = r > 0 ? (unsigned)r : 0; syscall(NR_close, fd); }
    }
    for (; n < 20; ++n) g_serial[n] = 0;
    g_serial[20] = 0;
    const char *sp = getenv("RG_CF_SHM");
    g_shm = (int)syscall(NR_open, sp && *sp ? sp : "/dev/shm/ravengun_JvsState", 0, 0);
}

/* ----- /dev/mjg: een record per 10 ms, uit het bordbeeld --------------------------------------------------------------- */
static unsigned long long g_laatste_ms;
static unsigned int g_irq = 100, g_volg;
static unsigned int g_coin[2], g_prev_sw;
static unsigned int g_prev_bits;
static int g_fire[2];

static unsigned int px_naar_raw_x(double x) { double r = (x + 170.0) / 0.6946107745170593; return r < 0 ? 0u : r > 4095 ? 4095u : (unsigned int)(r + 0.5); }
static unsigned int px_naar_raw_y(double y) { double r = y + 25.0; return r < 0 ? 0u : r > 4095 ? 4095u : (unsigned int)(r + 0.5); }

/* een asbyte (2..253 = in beeld) -> pixel; buiten beeld of buiten de strook = -1 */
static double as_px(unsigned char b, double max, int inv, double lo, double hi)
{
    if (b <= 1 || b >= 254) return -1;
    double f = ((double)b - 2.0) / 251.0;
    if (inv) f = 1.0 - f;
    f = (f - lo) / (hi - lo);
    if (f < 0.0 || f > 1.0) return -1;
    return f * max;
}

static void vul_record(unsigned char *rec)
{
    for (unsigned int i = 0; i < 0xb8; ++i) rec[i] = 0;
    unsigned char beeld[64];
    int ok = 0;
    if (g_shm >= 0) {
        syscall(NR_lseek, g_shm, 0, 0);
        ok = syscall(NR_read, g_shm, beeld, 64) == 64;
    }
    unsigned int bits = 0, sw = g_vast;
    if (ok) {
        bits = (unsigned int)beeld[8] | ((unsigned int)beeld[9] << 8) | ((unsigned int)beeld[10] << 16) | ((unsigned int)beeld[11] << 24);
        for (int i = 0; i < g_nmap; i++) if ((bits >> g_map[i].bit) & 1u) sw |= g_map[i].mask;
    }
    /* Onderzoek (RG_CF_CYCLE=hex,hex,...): elke stijgende flank van START 1 (bord-bit 4) zet de volgende masker uit de lijst i.p.v. de
       tabel; een regel per druk. Zo is in een run te zien welke bit de game als START leest. */
    if (g_ncyc) {
        sw = 0;   /* de tabel blijft voor munten; START komt uit de lijst */
        for (int i = 0; i < g_nmap; i++) if (g_map[i].bit != 4 && g_map[i].bit != 8 && ((bits >> g_map[i].bit) & 1u)) sw |= g_map[i].mask;
        sw |= g_vast;
        unsigned int nu = (bits >> 4) & 1u;
        if (nu && !g_cyc_was) { g_cyc_i = (g_cyc_n++) % g_ncyc; melding_hex("CYCLE: START-druk gebruikt masker ", g_cyc[g_cyc_i], 15 + (g_cyc_n % 1)); }
        g_cyc_was = nu;
        if (nu) sw |= g_cyc[g_cyc_i];
    }
    /* de muntentellers: stijgende flanken, zoals mjg.o ze telt (0x200000 -> teller 0, 0x100000 -> teller 1) */
    if ((sw & ~g_prev_sw) & 0x200000u) g_coin[0]++;
    if ((sw & ~g_prev_sw) & 0x100000u) g_coin[1]++;
    g_prev_sw = sw;
    /* trekker: een schot-bit in precies een record, op de stijgende flank (bit 5 = trekker 1, bit 9 = trekker 2) */
    int trig[2] = {(int)((bits >> 5) & 1u), (int)((bits >> 9) & 1u)};
    for (int g = 0; g < 2; g++) if (trig[g] && !((g_prev_bits >> (g ? 9 : 5)) & 1u)) g_fire[g] = 1;
    g_prev_bits = bits;
    unsigned int misc = 0;
    for (int g = 0; g < 2; g++) {
        double x = -1, y = -1;
        if (ok) { x = as_px(beeld[12 + 8 * g], g_w, g_invx, g_x0, g_x1); y = as_px(beeld[16 + 8 * g], g_h, g_invy, g_y0, g_y1); }
        int inbeeld = x >= 0 && y >= 0;
        if (inbeeld) {
            unsigned int s = px_naar_raw_x(x) | (px_naar_raw_y(y) << 12);
            *(unsigned int *)(rec + 0x10 + 16 * g) = s;
            misc |= g ? (1u << 4) : 1u;   /* een sample */
        }
        if (g_fire[g]) {
            misc |= g ? 0x2000u : 0x1000u; g_fire[g] = 0;
            melding(g ? "schot geweer 2" : "schot geweer 1", 12 + g);
            if (inbeeld) { melding_hex("  schot op spelbeeld-x ", (unsigned int)x, 12 + g); melding_hex("  schot op spelbeeld-y ", (unsigned int)y, 12 + g); }
            else melding("  schot buiten beeld (herladen)", 12 + g);
        }
    }
    *(unsigned int *)(rec + 0x00) = misc;
    *(unsigned int *)(rec + 0x04) = sw;
    *(unsigned int *)(rec + 0x08) = g_irq;
    *(unsigned int *)(rec + 0x0c) = g_irq;     /* strikt oplopend */
    g_irq++;
    *(unsigned int *)(rec + 0x30) = g_coin[0];
    *(unsigned int *)(rec + 0x34) = g_coin[1];
    *(unsigned int *)(rec + 0x38) = 0x7d;
    *(unsigned int *)(rec + 0x3c) = g_volg++;
}

/* ----- de onderschepte aanroepen -------------------------------------------------------------------------------------- */
static int soort_van(const char *pad)
{
    if (!pad) return K_NONE;
    if (!strcmp(pad, "/dev/p37c")) return K_P37C;
    if (!strcmp(pad, "/dev/mjg")) return K_MJG;
    if (!strcmp(pad, "/dev/hda")) return K_HDA;
    return K_NONE;
}

static long nep_open(const char *pad)
{
    int k = soort_van(pad);
    if (!k) return -2;
    init();
    long fd = syscall(NR_open, "/dev/null", 2 /* O_RDWR */, 0);
    if (fd >= 0 && fd < MAXFD) g_kind[fd] = (unsigned char)k;
    melding(k == K_P37C ? "open /dev/p37c" : k == K_MJG ? "open /dev/mjg" : "open /dev/hda", k);
    return fd;
}

int open(const char *pad, int vlaggen, ...)
{
    long fd = nep_open(pad);
    if (fd != -2) return (int)fd;
    unsigned int mode = 0;
    if (vlaggen & 0100) { va_list ap; va_start(ap, vlaggen); mode = va_arg(ap, unsigned int); va_end(ap); }
    return (int)syscall(NR_open, pad, vlaggen, mode);
}
int open64(const char *pad, int vlaggen, ...)
{
    long fd = nep_open(pad);
    if (fd != -2) return (int)fd;
    unsigned int mode = 0;
    if (vlaggen & 0100) { va_list ap; va_start(ap, vlaggen); mode = va_arg(ap, unsigned int); va_end(ap); }
    return (int)syscall(NR_open, pad, vlaggen, mode);
}
int openat(int dirfd, const char *pad, int vlaggen, ...)
{
    long fd = nep_open(pad);
    if (fd != -2) return (int)fd;
    unsigned int mode = 0;
    if (vlaggen & 0100) { va_list ap; va_start(ap, vlaggen); mode = va_arg(ap, unsigned int); va_end(ap); }
    return (int)syscall(NR_openat, dirfd, pad, vlaggen, mode);
}
int openat64(int dirfd, const char *pad, int vlaggen, ...)
{
    long fd = nep_open(pad);
    if (fd != -2) return (int)fd;
    unsigned int mode = 0;
    if (vlaggen & 0100) { va_list ap; va_start(ap, vlaggen); mode = va_arg(ap, unsigned int); va_end(ap); }
    return (int)syscall(NR_openat, dirfd, pad, vlaggen, mode);
}

int iopl(int niveau) { (void)niveau; return 0; }

int close(int fd)
{
    if (fd >= 0 && fd < MAXFD) g_kind[fd] = K_NONE;
    return (int)syscall(NR_close, fd);
}

long read(int fd, void *buf, unsigned long n)
{
    if (fd < 0 || fd >= MAXFD || !g_kind[fd]) return syscall(NR_read, fd, buf, n);
    unsigned char *p = (unsigned char *)buf;
    if (g_kind[fd] == K_P37C) {
        if (n < 1) return 0;
        p[0] = reg_lees(g_addr);
        return 1;
    }
    if (g_kind[fd] == K_MJG) {
        if (n < 0xb8) return 0;
        unsigned long long t = nu_ms();
        if (t < g_laatste_ms + 10) return 0;   /* niets nieuws */
        g_laatste_ms = t;
        vul_record(p);
        melding("read /dev/mjg (record)", 5);
        return 0xb8;
    }
    return 0;
}

long write(int fd, const void *buf, unsigned long n)
{
    if (fd < 0 || fd >= MAXFD || !g_kind[fd]) return syscall(NR_write, fd, buf, n);
    if (g_kind[fd] == K_P37C && n >= 1) poort_uit(*(const unsigned char *)buf);
    return (long)n;
}

int ioctl(int fd, unsigned long verzoek, ...)
{
    va_list ap;
    va_start(ap, verzoek);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (fd < 0 || fd >= MAXFD || !g_kind[fd]) return (int)syscall(NR_ioctl, fd, verzoek, arg);
    if (g_kind[fd] == K_HDA && verzoek == 0x30d /* HDIO_GET_IDENTITY */) {
        unsigned char *p = (unsigned char *)arg;
        for (unsigned int i = 0; i < 512; ++i) p[i] = 0;
        for (unsigned int i = 0; i < 20; ++i) p[0x14 + i] = g_serial[i];
        melding("ioctl HDIO_GET_IDENTITY", 6);
        return 0;
    }
    if (g_kind[fd] == K_MJG) { melding_hex("ioctl /dev/mjg ", (unsigned int)verzoek, 7); return 0; }
    return 0;
}

/* glutInit: de game geeft GLUT hard "-display :0.0" mee (0x806b9d8/0x806ba18) en kan dan niet verbinden met het scherm van gamescope
   ("Authorization required ... failed to open display ':0.0'", bench 2026-10-18). Met RG_GLUT_DISPLAY_FROM_ENV=1 wordt de waarde achter
   "-display" $DISPLAY. */
extern void *dlsym(void *, const char *);
extern void *dlopen(const char *, int);
void glutInit(int *argcp, char **argv)
{
    static void (*echt)(int *, char **);
    if (!echt) echt = (void (*)(int *, char **))dlsym((void *)-1l /* RTLD_NEXT */, "glutInit");
    if (!echt) { void *h = dlopen("libglut.so.3", 0x102 /* RTLD_NOW|RTLD_GLOBAL */); if (h) echt = (void (*)(int *, char **))dlsym(h, "glutInit"); }
    init();
    const char *d = getenv("DISPLAY");
    if (argcp && argv && getenv("RG_GLUT_DISPLAY_FROM_ENV") && d && *d)
        for (int i = 0; i + 1 < *argcp; i++)
            if (argv[i] && !strcmp(argv[i], "-display")) argv[i + 1] = (char *)d;
    melding(echt ? "glutInit aangeroepen (echte functie gevonden)" : "glutInit aangeroepen: echte functie NIET gevonden", 14);
    if (echt) echt(argcp, argv);
}

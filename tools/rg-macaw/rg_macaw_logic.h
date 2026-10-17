/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_macaw_logic.h - de zuivere logica van de eigen Konami-lader (rg-macaw.exe) en van onze libacio.dll.
 *
 * Geen Windows-aanroepen, geen C-runtime buiten <string.h>/<stdint.h>: dit bestand wordt ook native (gcc) getest door
 * tests/silentscope_macaw_test.sh. De Windows-kant staat in rg_macaw.c (lader) en rg_libacio.c (de DLL).
 *
 * Alles hier is gemeten aan Silent Scope: Bone Eater (docs/research/losse/SilentScopeBoneEater.md):
 *   - libavs-win64.dll is AVS 2.16.3 (FileVersion "2.16.3,r7106"); de exportnamen zijn "XCnbrep7" + index, de namen van
 *     de functies staan in de tabel hieronder (bevestigd: libacio.dll importeert op ordinaal 298/299 = boot/shutdown).
 *   - de ea3-instellingen staan in prop/ea3-config.xml (model NDD, dest A, spec A, rev A, ext 2016122700).
 *   - het statusblok van het bmpu-apparaat zoals arkndd.dll het leest (arkndd.dll 0x18000b6e0 en 0x18000ad3c..0x18000b028).
 */
#ifndef RG_MACAW_LOGIC_H
#define RG_MACAW_LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- paden ------------------------------------------------------------------------------------------------------- */

static inline int rgm_is_sep(char c) { return c == '\\' || c == '/'; }

/* "Z:\\...\\views\\SilentScopeBoneEater\\modules\\arkndd.dll" -> "Z:\\...\\views\\SilentScopeBoneEater" (de map boven modules,
 * het werkmapje dat de kast zelf heeft). Staat de dll niet in een map "modules", dan wordt het de map van de dll zelf.
 * Geeft 0 bij een te klein bufferje of een leeg pad, anders de lengte. */
static inline size_t rgm_module_root(const char *module_path, char *root, size_t cap)
{
    size_t n = module_path ? strlen(module_path) : 0, i, end;
    if (!n || cap < 2) return 0;
    for (end = n; end > 0 && !rgm_is_sep(module_path[end - 1]); end--) {}      /* weg met de bestandsnaam */
    if (!end) return 0;
    /* end wijst net achter de laatste scheiding; de map is [0, end-1) */
    i = end - 1;
    {   /* is de laatste map "modules" (hoofdletters maken niet uit)? */
        size_t s = i;
        while (s > 0 && !rgm_is_sep(module_path[s - 1])) s--;
        if (i - s == 7) {
            static const char want[] = "modules";
            size_t k;
            int same = 1;
            for (k = 0; k < 7; k++) {
                char c = module_path[s + k];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (c != want[k]) same = 0;
            }
            if (same && s > 0) i = s - 1;                                         /* de scheiding voor "modules" weg */
        }
    }
    if (i + 1 > cap) return 0;
    memcpy(root, module_path, i);
    root[i] = 0;
    return i;
}

/* ---- kleine XML-helpers (de configuratiebestanden zijn plat genoeg) -------------------------------------------------- */

/* De tekst tussen <tag ...> en </tag>, eerste voorkomen. Geeft de lengte, of -1 als de tag er niet staat / het niet past. */
static inline int rgm_xml_value(const char *xml, const char *tag, char *out, size_t cap)
{
    char open[64], close[64];
    const char *p, *q, *gt;
    size_t tl = strlen(tag), len;
    if (!xml || tl + 4 > sizeof open) return -1;
    open[0] = '<'; memcpy(open + 1, tag, tl); open[tl + 1] = 0;
    close[0] = '<'; close[1] = '/'; memcpy(close + 2, tag, tl); close[tl + 2] = '>'; close[tl + 3] = 0;
    for (p = xml; (p = strstr(p, open)) != NULL; p++) {
        char after = p[tl + 1];
        if (after != '>' && after != ' ' && after != '/' && after != '\t' && after != '\r' && after != '\n') continue;  /* <model2> is een ander */
        gt = strchr(p, '>');
        if (!gt) return -1;
        if (gt[-1] == '/') return -1;
        q = strstr(gt + 1, close);
        if (!q) return -1;
        len = (size_t)(q - (gt + 1));
        if (len + 1 > cap) return -1;
        memcpy(out, gt + 1, len);
        out[len] = 0;
        return (int)len;
    }
    return -1;
}

/* De "softwarecode" zoals ea3 hem samenstelt: model:dest:spec:rev:ext, bv. "NDD:A:A:A:2016122700". Dat is wat
 * dll_entry_init als eerste argument krijgt. Een ontbrekend veld geeft -1. */
static inline int rgm_soft_code(const char *ea3_xml, char *out, size_t cap)
{
    static const char *const tags[5] = {"model", "dest", "spec", "rev", "ext"};
    char part[5][32];
    size_t i, n = 0;
    for (i = 0; i < 5; i++)
        if (rgm_xml_value(ea3_xml, tags[i], part[i], sizeof part[i]) < 0) return -1;
    for (i = 0; i < 5; i++) {
        size_t l = strlen(part[i]);
        if (n + l + 2 > cap) return -1;
        memcpy(out + n, part[i], l);
        n += l;
        if (i < 4) out[n++] = ':';
    }
    out[n] = 0;
    return (int)n;
}

/* Vervangt het eerste voorkomen van from door to, op zijn plek in buf (len bytes in gebruik, cap groot). Geeft de nieuwe
 * lengte, of (size_t)-1 als from er niet staat of het niet past. De lader gebruikt dit om een tijdelijke kopie van een
 * configuratiebestand aan te passen (de bestanden op de NAS blijven onaangeroerd). */
static inline size_t rgm_text_replace(char *buf, size_t len, size_t cap, const char *from, const char *to)
{
    size_t fl = strlen(from), tl = strlen(to), off;
    char *p = NULL;
    size_t i;
    if (!fl || len < fl) return (size_t)-1;
    for (i = 0; i + fl <= len; i++)
        if (memcmp(buf + i, from, fl) == 0) { p = buf + i; break; }
    if (!p) return (size_t)-1;
    off = (size_t)(p - buf);
    if (len - fl + tl + 1 > cap) return (size_t)-1;
    memmove(buf + off + tl, buf + off + fl, len - off - fl);
    memcpy(buf + off, to, tl);
    buf[len - fl + tl] = 0;
    return len - fl + tl;
}

/* Voegt ins in vlak voor het eerste voorkomen van marker. Zelfde teruggave als rgm_text_replace. */
static inline size_t rgm_text_insert_before(char *buf, size_t len, size_t cap, const char *marker, const char *ins)
{
    size_t ml = strlen(marker), il = strlen(ins), i, off;
    int gevonden = 0;
    for (i = 0; i + ml <= len; i++)
        if (memcmp(buf + i, marker, ml) == 0) { gevonden = 1; break; }
    if (!gevonden || len + il + 1 > cap) return (size_t)-1;
    off = i;
    memmove(buf + off + il, buf + off, len - off);
    memcpy(buf + off, ins, il);
    buf[len + il] = 0;
    return len + il;
}

/* ---- de namen in libavs-win64.dll / libavs-win64-ea3.dll -------------------------------------------------------------- */

struct rgm_avs_syms {
    const char *boot, *shutdown;
    const char *property_search, *property_create, *property_destroy, *property_read_query_memsize, *property_insert_read;
    const char *fs_open, *fs_read, *fs_close;
    const char *ea3_boot, *ea3_shutdown;
};

/* AVS 2.16.3 t/m 2.16.8 (r7106 is 2.16.3): namen uit de gepubliceerde tabel van de spice2x-bron (avs/core.cpp en avs/ea3.cpp)
 * en bevestigd tegen onze dll: het ordinaal van de boot (0x129 + 1 = 298) is wat libacio.dll importeert. 2.16.1 heeft
 * andere indices (boot 0x11a) en wordt niet ondersteund. */
static const struct rgm_avs_syms rgm_avs_216 = {
    "XCnbrep7000129", "XCnbrep700012a",
    "XCnbrep70000a1", "XCnbrep7000090", "XCnbrep7000091", "XCnbrep70000b0", "XCnbrep7000094",
    "XCnbrep700004e", "XCnbrep7000051", "XCnbrep7000055",
    "XEyy2igh000007", "XEyy2igh000008",
};

/* file_version: de FileVersion uit de versiebron van libavs-win64.dll, bv. "2.16.3,r7106". NULL = niet ondersteund. */
static inline const struct rgm_avs_syms *rgm_avs_syms_for(const char *file_version)
{
    unsigned maj = 0, min = 0, pat = 0, k;
    const char *p = file_version;
    if (!p) return NULL;
    for (k = 0; *p >= '0' && *p <= '9'; p++) maj = maj * 10 + (unsigned)(*p - '0');
    if (*p++ != '.') return NULL;
    for (; *p >= '0' && *p <= '9'; p++) min = min * 10 + (unsigned)(*p - '0');
    if (*p++ != '.') return NULL;
    for (; *p >= '0' && *p <= '9'; p++) pat = pat * 10 + (unsigned)(*p - '0');
    (void)k;
    if (maj == 2 && min == 16 && pat >= 3 && pat <= 8) return &rgm_avs_216;
    return NULL;
}

/* ---- het statusblok van het bmpu-apparaat (wat arkndd.dll uit ac_io_bmpu_get_control_status_buffer leest) --------------- */

/* tp-shm, indeling KonamiAcio (packaging/tp-shm-profiles.json): bit 0 test, 1 service, 2 munt, 3 start, 4 menu omhoog,
 * 5 menu omlaag, 6/7 vrij, 8 zijknop links (aux1), 9 zijknop rechts, 10 trekker (p1.trigger), 11 scope (p1.grenade). */
#define RGM_C_TEST     0x001u
#define RGM_C_SERVICE  0x002u
#define RGM_C_COIN     0x004u
#define RGM_C_START    0x008u
#define RGM_C_SIDE_L   0x100u
#define RGM_C_SIDE_R   0x200u
#define RGM_C_TRIGGER  0x400u
#define RGM_C_SCOPE    0x800u

#define RGM_BMPU_SIZE  0x38   /* het blok van het bmpu-apparaat; het blok van nddb begint in de lokale struct van het spel op +0x38 */

/* Het spel leest (arkndd.dll, 0x18000af73..0x18000b028; L = begin van het blok):
 *   L[4] bit7 -> knopmasker bit 8 (scope links), bit6 -> bit 9 (scope rechts), bit5 -> bit 10 (trekker)
 *   L[5] bit0..4 -> bit 3..7 (start, menu omhoog/omlaag, twee vrije)
 *   L[7] bit4 -> bit 0 (test), bit1 -> bit 1 (service), bit0 -> bit 2 (munt)
 *   X = (L[8] << 4) | (L[9] >> 4), Y = (L[10] << 4) | (L[11] >> 4)    (12 bit, links/boven = 0)
 * ctl is het 32-bit besturingswoord op offset 8 van tp-shm, x16/y16 de 16-bit assen op 16 en 20. */
static inline void rgm_acio_pack_bmpu(uint32_t ctl, uint16_t x16, uint16_t y16, uint8_t out[RGM_BMPU_SIZE])
{
    uint16_t x12 = (uint16_t)(x16 >> 4), y12 = (uint16_t)(y16 >> 4);
    uint8_t l4 = 0, l5, l7 = 0;
    memset(out, 0, RGM_BMPU_SIZE);
    /* De scope-knop van onze indeling (granaat) zet beide scopeknoppen van het geweer: het echte geweer heeft er twee en het
     * spel zoomt bij elk van de twee. De zijknop (aux1) is de linker. */
    if ((ctl & RGM_C_SIDE_L) || (ctl & RGM_C_SCOPE)) l4 |= 0x80;
    if ((ctl & RGM_C_SIDE_R) || (ctl & RGM_C_SCOPE)) l4 |= 0x40;
    if (ctl & RGM_C_TRIGGER) l4 |= 0x20;
    l5 = (uint8_t)((ctl >> 3) & 0x1f);
    if (ctl & RGM_C_TEST) l7 |= 0x10;
    if (ctl & RGM_C_SERVICE) l7 |= 0x02;
    if (ctl & RGM_C_COIN) l7 |= 0x01;
    out[4] = l4;
    out[5] = l5;
    out[7] = l7;
    out[8] = (uint8_t)(x12 >> 4);
    out[9] = (uint8_t)((x12 & 0xf) << 4);
    out[10] = (uint8_t)(y12 >> 4);
    out[11] = (uint8_t)((y12 & 0xf) << 4);
}

/* Het muntentotaal dat ac_io_bmpu_current_coinstock levert: het spel neemt het verschil met de vorige stand (0x18000af12..).
 * Een muntdruk (stijgende flank van RGM_C_COIN) telt er een bij; prev_ctl is het woord van de vorige aanroep. */
static inline uint32_t rgm_acio_coin_total(uint32_t total, uint32_t prev_ctl, uint32_t ctl)
{
    if ((ctl & RGM_C_COIN) && !(prev_ctl & RGM_C_COIN)) total++;
    return total;
}

#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Unit-test van tools/rg-macaw/rg_macaw_logic.h (de zuivere logica van de eigen Konami-lader en van onze libacio.dll).
 * Bouwen en draaien: tests/silentscope_macaw_test.sh. Argumenten: <exportlijst> <rg_libacio.c>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../tools/rg-macaw/rg_macaw_logic.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char *EA3 =
    "<?xml version=\"1.0\" encoding=\"Shift_JIS\"?>\n<ea3>\n  <id>\n    <pcbid __type=\"str\">00010203040506070809</pcbid>\n"
    "    <hardid __type=\"str\">00010203040506070809</hardid>\n  </id>\n  <soft>\n    <model __type=\"str\">NDD</model>\n"
    "    <dest __type=\"str\">A</dest>\n    <spec __type=\"str\">A</spec>\n    <rev __type=\"str\">A</rev>\n"
    "    <ext __type=\"str\">2016122700</ext>\n  </soft>\n</ea3>\n";

static void t_root(void)
{
    char r[260];
    CHECK(rgm_module_root("Z:\\home\\deck\\views\\SilentScopeBoneEater\\modules\\arkndd.dll", r, sizeof r) > 0);
    CHECK(strcmp(r, "Z:\\home\\deck\\views\\SilentScopeBoneEater") == 0);
    CHECK(rgm_module_root("/home/x/Game/MODULES/arkndd.dll", r, sizeof r) > 0 && strcmp(r, "/home/x/Game") == 0);
    CHECK(rgm_module_root("C:\\game\\bin\\arkndd.dll", r, sizeof r) > 0 && strcmp(r, "C:\\game\\bin") == 0);   /* geen modules: de map zelf */
    CHECK(rgm_module_root("arkndd.dll", r, sizeof r) == 0);                                                  /* geen map */
    CHECK(rgm_module_root("", r, sizeof r) == 0);
    CHECK(rgm_module_root("C:\\a\\modules\\x.dll", r, 4) == 0);                                              /* te klein */
}

static void t_xml(void)
{
    char v[64], sid[64];
    CHECK(rgm_xml_value(EA3, "model", v, sizeof v) == 3 && strcmp(v, "NDD") == 0);
    CHECK(rgm_xml_value(EA3, "ext", v, sizeof v) == 10 && strcmp(v, "2016122700") == 0);
    CHECK(rgm_xml_value(EA3, "nietaanwezig", v, sizeof v) == -1);
    CHECK(rgm_xml_value(EA3, "model", v, 3) == -1);                       /* past niet */
    CHECK(rgm_xml_value("<model2>X</model2>", "model", v, sizeof v) == -1);   /* <model2> is een andere tag */
    CHECK(rgm_soft_code(EA3, sid, sizeof sid) == 20 && strcmp(sid, "NDD:A:A:A:2016122700") == 0);
    CHECK(rgm_soft_code("<ea3><soft><model>X</model></soft></ea3>", sid, sizeof sid) == -1);
    CHECK(rgm_soft_code(EA3, sid, 10) == -1);
}

static void t_text(void)
{
    char b[256];
    size_t n;
    strcpy(b, "<a><ea_on __type=\"bool\">1</ea_on></a>");
    n = rgm_text_replace(b, strlen(b), sizeof b, "<ea_on __type=\"bool\">1</ea_on>", "<ea_on __type=\"bool\">0</ea_on>");
    CHECK(n == strlen(b) && strcmp(b, "<a><ea_on __type=\"bool\">0</ea_on></a>") == 0);
    strcpy(b, "abc");
    CHECK(rgm_text_replace(b, 3, sizeof b, "xyz", "q") == (size_t)-1 && strcmp(b, "abc") == 0);
    strcpy(b, "abcabc");
    n = rgm_text_replace(b, 6, sizeof b, "bc", "WXYZ");                  /* alleen het eerste voorkomen, andere lengte */
    CHECK(n == 8 && strcmp(b, "aWXYZabc") == 0);
    strcpy(b, "abcdef");
    CHECK(rgm_text_replace(b, 6, 8, "abc", "0123456") == (size_t)-1);    /* past niet in cap */
    strcpy(b, "<id>x</id>");
    n = rgm_text_insert_before(b, strlen(b), sizeof b, "</id>", "<s>1</s>");
    CHECK(n == strlen(b) && strcmp(b, "<id>x<s>1</s></id>") == 0);
    CHECK(rgm_text_insert_before(b, strlen(b), sizeof b, "</nope>", "x") == (size_t)-1);
}

static void t_avs(void)
{
    const struct rgm_avs_syms *s = rgm_avs_syms_for("2.16.3,r7106");
    CHECK(s && strcmp(s->boot, "XCnbrep7000129") == 0 && strcmp(s->shutdown, "XCnbrep700012a") == 0);
    CHECK(s && strcmp(s->ea3_boot, "XEyy2igh000007") == 0);
    CHECK(rgm_avs_syms_for("2.16.8.1") == s);
    CHECK(rgm_avs_syms_for("2.16.1,r6000") == NULL);       /* andere indices */
    CHECK(rgm_avs_syms_for("2.17.0") == NULL);
    CHECK(rgm_avs_syms_for("2.15.8") == NULL);
    CHECK(rgm_avs_syms_for("") == NULL && rgm_avs_syms_for(NULL) == NULL && rgm_avs_syms_for("abc") == NULL);
    /* het ordinaal van de boot in onze dll: index 0x129 + 1 = 298, wat libacio.dll importeert */
    CHECK(strtoul(s->boot + strlen("XCnbrep7"), NULL, 16) + 1 == 298);
}

static void t_pack(void)
{
    uint8_t b[RGM_BMPU_SIZE];
    rgm_acio_pack_bmpu(0, 0, 0, b);
    { int i, z = 1; for (i = 0; i < RGM_BMPU_SIZE; i++) if (b[i]) z = 0; CHECK(z); }
    rgm_acio_pack_bmpu(RGM_C_TRIGGER, 0, 0, b);
    CHECK(b[4] == 0x20 && b[5] == 0 && b[7] == 0);
    rgm_acio_pack_bmpu(RGM_C_SIDE_L, 0, 0, b);
    CHECK(b[4] == 0x80);
    rgm_acio_pack_bmpu(RGM_C_SIDE_R, 0, 0, b);
    CHECK(b[4] == 0x40);
    rgm_acio_pack_bmpu(RGM_C_SCOPE, 0, 0, b);
    CHECK(b[4] == 0xC0);                                   /* scope zet beide scopeknoppen */
    rgm_acio_pack_bmpu(RGM_C_START, 0, 0, b);
    CHECK(b[5] == 0x01);
    rgm_acio_pack_bmpu(0x10 | 0x20, 0, 0, b);              /* menu omhoog en omlaag: bit 1 en 2 van L[5] */
    CHECK(b[5] == 0x06);
    rgm_acio_pack_bmpu(RGM_C_TEST | RGM_C_SERVICE | RGM_C_COIN, 0, 0, b);
    CHECK(b[7] == 0x13);
    /* masker-bits zoals het spel ze terugrekent (arkndd.dll 0x18000af73..0x18000b00b): hoogste bits van ctl (12+) doen niets */
    rgm_acio_pack_bmpu(0xfffff000u, 0, 0, b);
    CHECK(b[4] == 0 && b[5] == 0 && b[7] == 0);
    /* positie: 16 bit -> 12 bit -> (L8<<4)|(L9>>4) */
    rgm_acio_pack_bmpu(0, 0xABCD, 0x1234, b);
    CHECK((((unsigned)b[8] << 4) | (b[9] >> 4)) == 0xABC);
    CHECK((((unsigned)b[10] << 4) | (b[11] >> 4)) == 0x123);
    CHECK((b[9] & 0x0f) == 0 && (b[11] & 0x0f) == 0);
    rgm_acio_pack_bmpu(0, 0xFFFF, 0, b);
    CHECK((((unsigned)b[8] << 4) | (b[9] >> 4)) == 0xFFF);
    /* de rest van het blok (voorbij byte 11) blijft nul */
    rgm_acio_pack_bmpu(0xffffffffu, 0xffff, 0xffff, b);
    { int i, z = 1; for (i = 12; i < RGM_BMPU_SIZE; i++) if (b[i]) z = 0; CHECK(z); }
}

static void t_coin(void)
{
    uint32_t t = 0, prev = 0;
    t = rgm_acio_coin_total(t, prev, RGM_C_COIN); prev = RGM_C_COIN;
    CHECK(t == 1);
    t = rgm_acio_coin_total(t, prev, RGM_C_COIN);                    /* vastgehouden: geen tweede munt */
    CHECK(t == 1);
    prev = 0;
    t = rgm_acio_coin_total(t, prev, 0);
    CHECK(t == 1);
    t = rgm_acio_coin_total(t, prev, RGM_C_COIN | RGM_C_TRIGGER);    /* losgelaten en weer ingedrukt */
    CHECK(t == 2);
}

/* elke naam die arkndd.dll importeert staat als export in rg_libacio.c, en niets extra's */
static void t_exports(const char *list, const char *src)
{
    FILE *f = fopen(list, "r"), *g = fopen(src, "rb");
    char line[256];
    static char code[200000];
    size_t n, cnt = 0;
    if (!f || !g) { printf("FAIL: kan %s of %s niet openen\n", list, src); fails++; return; }
    n = fread(code, 1, sizeof code - 1, g);
    code[n] = 0;
    while (fgets(line, sizeof line, f)) {
        char key[270];
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        cnt++;
        snprintf(key, sizeof key, "API int %s(", line);
        if (!strstr(code, key)) { printf("FAIL: export %s ontbreekt in rg_libacio.c\n", line); fails++; }
    }
    CHECK(cnt == 35);
    {   /* telling van API-definities in de bron */
        size_t defs = 0;
        const char *p = code;
        while ((p = strstr(p, "API int ")) != NULL) { defs++; p += 8; }
        CHECK(defs == cnt);
    }
    fclose(f); fclose(g);
}

int main(int argc, char **argv)
{
    t_root(); t_xml(); t_text(); t_avs(); t_pack(); t_coin();
    if (argc > 2) t_exports(argv[1], argv[2]);
    if (fails) { printf("%d mislukt\n", fails); return 1; }
    printf("silentscope_macaw: alles geslaagd\n");
    return 0;
}

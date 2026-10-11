/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * selftest.c — controleert de dongle-laag tegen de echte dongle in halo-fireteam/.
 *
 * Bouwen en draaien:
 *   cc -std=c99 -Wall -Wextra -o selftest selftest.c dongle.c
 *   ./selftest ../halo-fireteam
 *
 * Geeft 0 als alles klopt. scripts/build-hasp.sh draait hem na het bouwen.
 */
#include "dongle.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "ok " : "FOUT", what);
    if (!cond) failures++;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "../halo-fireteam";
    printf("dongle-zelftest tegen: %s\n\n", dir);

    /* ---- 1. de naamregel van het formaat ---- */
    printf("1. de binaire fileid-naam\n");
    char name[33];
    dongle_fileid_name(65524, name);
    printf("     fileid 65524 -> %s\n", name);
    check(strcmp(name, "00000000000000001111111111110100") == 0,
          "fileid 65524 geeft de verwachte 32-bits naam");
    dongle_fileid_name(65525, name);
    printf("     fileid 65525 -> %s\n", name);
    check(strcmp(name, "00000000000000001111111111110101") == 0,
          "fileid 65525 geeft de verwachte 32-bits naam");
    dongle_fileid_name(0, name);
    check(strcmp(name, "00000000000000000000000000000000") == 0, "fileid 0 is alle nullen");
    dongle_fileid_name(0xFFFFFFFFu, name);
    check(strcmp(name, "11111111111111111111111111111111") == 0, "fileid 0xFFFFFFFF is alle enen");

    /* ---- 2. de beschrijving inlezen ---- */
    printf("\n2. de beschrijving inlezen\n");
    dongle_t d;
    if (dongle_load(dir, &d) != 0) {
        printf("  [FOUT] dongle.json niet gevonden in %s\n", dir);
        return 1;
    }
    printf("     spel: %s\n", d.game);
    check(d.type == DONGLE_HASP_HL, "type is hasp-hl");
    check(d.haspid == 748675309u, "haspid is 748675309");
    check(d.nfeatures == 2, "twee kenmerken");
    check(d.nmemory == 2, "twee geheugenbestanden");
    check(d.nkeycaps == 5, "vijf capabilities");
    check(d.port_address == 196608u, "poortadres is 0x30000");
    check(strcmp(d.port_type, "USB") == 0, "poorttype is USB");
    for (int i = 0; i < d.nmemory; i++)
        printf("     memory: %-12s fileid=%-6u size=%u\n",
               d.memory[i].name, d.memory[i].fileid, d.memory[i].size);
    check(dongle_memory_size(&d, 65524) == 112, "fileid 65524 heeft grootte 112");
    check(dongle_memory_size(&d, 999) == 0, "een onbekende fileid geeft grootte 0");

    /* ---- 3. de XML ---- */
    printf("\n3. de XML die hasp_get_info teruggeeft\n");
    char xml[8192];
    int n = dongle_get_info_xml(&d, 0, xml, sizeof(xml));
    check(n > 0, "de korte XML wordt opgebouwd");
    printf("%s", xml);
    check(strstr(xml, "id=\"748675309\"") != NULL, "de korte XML bevat het haspid");
    check(strstr(xml, "type=\"HASP-HL\"") != NULL, "de korte XML noemt het type");
    check(strstr(xml, "feature id=\"0\"") != NULL, "kenmerk 0 staat erin");
    check(strstr(xml, "feature id=\"60\"") != NULL, "kenmerk 60 staat erin");

    n = dongle_get_info_xml(&d, 1, xml, sizeof(xml));
    check(n > 0, "de uitgebreide XML wordt opgebouwd");
    check(strstr(xml, "<aes />") != NULL, "de uitgebreide XML noemt de aes-capability");
    check(strstr(xml, "<fileid>65524</fileid>") != NULL, "de uitgebreide XML noemt fileid 65524");
    check(strstr(xml, "<address>196608</address>") != NULL, "de uitgebreide XML noemt het poortadres");

    /* ---- 4. het geheugen lezen ---- */
    printf("\n4. het geheugen lezen\n");
    unsigned char buf[256];
    memset(buf, 0, sizeof(buf));
    int rd = dongle_read(&d, 65524, buf, 112);
    printf("     %d bytes gelezen\n", rd);
    check(rd == 112, "112 bytes gelezen voor fileid 65524");
    if (rd > 0) {
        printf("     eerste 8 bytes:");
        for (int i = 0; i < 8 && i < rd; i++) printf(" %02x", buf[i]);
        printf("\n");
    }
    /* de 112-byte image begint in beide bekende gevallen met 2c 03 ab 63 */
    check(rd >= 4 && buf[0] == 0x2c && buf[1] == 0x03 && buf[2] == 0xab && buf[3] == 0x63,
          "de image begint met 2c 03 ab 63 (zoals de echte dongle)");

    /* met een offset (de ABI van hasp_read heeft er een) */
    unsigned char twee[8];
    memset(twee, 0, sizeof(twee));
    check(dongle_read_at(&d, 65524, 4, twee, 4) == 4, "lezen met een offset geeft 4 bytes");
    check(memcmp(twee, buf + 4, 4) == 0, "de bytes met offset 4 zijn dezelfde als de eerste lezing");

    /* ---- 5. de HASP-API ---- */
    printf("\n5. de HASP-API\n");
    check(dongle_install(&d) == 0, "de dongle wordt actief");
    check(dongle_active() == &d || dongle_active() != NULL, "dongle_active geeft de actieve dongle");
    void *h = NULL;
    check(hasp_login(0, NULL, &h) == HASP_STATUS_OK, "hasp_login geeft succes");
    check(h != NULL && (unsigned int)(size_t)h == DONGLE_HANDLE_VALUE, "de greep is 0xE6F");
    unsigned int sz = 999;
    check(hasp_get_size(h, 65524, &sz) == HASP_STATUS_OK && sz == 112,
          "hasp_get_size geeft de beschreven grootte (112)");
    check(hasp_get_size(h, 999, &sz) == HASP_STATUS_OK && sz == 0,
          "hasp_get_size van een onbekende fileid geeft 0");
    char *info = NULL;
    check(hasp_get_info(h, NULL, NULL, &info) == HASP_STATUS_OK && info != NULL, "hasp_get_info geeft tekst");
    if (info) { check(strstr(info, "hasp_info") != NULL, "de tekst is de XML"); hasp_free(info); }
    char *sess = NULL;
    check(hasp_get_sessioninfo(h, NULL, &sess) == HASP_STATUS_OK && sess != NULL,
          "hasp_get_sessioninfo geeft de uitgebreide XML");
    if (sess) { check(strstr(sess, "<keyspec>") != NULL, "de sessietekst is de uitgebreide vorm"); hasp_free(sess); }
    memset(buf, 0, sizeof(buf));
    check(hasp_read(h, 65524, 0, 112, buf) == HASP_STATUS_OK, "hasp_read op fileid 65524");
    check(buf[0] == 0x2c && buf[3] == 0x63, "hasp_read gaf de echte image terug");
    check(hasp_read(NULL, 65524, 0, 112, buf) == HASP_STATUS_INVALID_PARAMETER,
          "hasp_read zonder greep geeft 501");
    check(hasp_read(h, 65524, 0, 112, NULL) == HASP_STATUS_INVALID_PARAMETER,
          "hasp_read zonder buffer geeft 501");
    check(hasp_read(h, 12345, 0, 16, buf) == HASP_STATUS_NOT_FOUND,
          "hasp_read op een onbekende fileid geeft een fout");
    check(hasp_encrypt(h, buf, 112) == HASP_STATUS_OK, "hasp_encrypt meldt succes");
    check(hasp_decrypt(h, buf, 112) == HASP_STATUS_OK, "hasp_decrypt meldt succes");
    check(hasp_logout(h) == HASP_STATUS_OK, "hasp_logout meldt succes");
    dongle_uninstall();
    check(dongle_active() == NULL, "na dongle_uninstall is er geen actieve dongle");
    check(hasp_login(0, NULL, &h) == HASP_STATUS_NOT_FOUND,
          "zonder dongle geeft hasp_login een fout");

    printf("\n%s — %d fout(en)\n", failures ? "MISLUKT" : "GESLAAGD", failures);
    return failures ? 1 : 0;
}

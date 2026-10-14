/* Eenheidstest van de RIO-brug (tools/rio-bridge/rg_rio_bridge.c), gestart door tests/rio_bridge_test.sh met de brug in
 * LD_PRELOAD. Laadt een nep-librio.so (de goede build) en een andere build met dlopen, schrijft het bordbeeld zelf en kijkt wat
 * de functies van de nep-librio teruggeven. Geen spel nodig.
 * Argumenten: <goede librio.so> <andere build librio.so> <bordbeeld> <probe-bestand>
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fouten;
#define CHECK(c, ...) do { if (!(c)) { fouten++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int fd; static unsigned char beeld[64];
static void schrijf(void) { pwrite(fd, beeld, sizeof beeld, 0); usleep(30000); }
static void bit(int b, int aan) { unsigned v; memcpy(&v, beeld + 8, 4); v = aan ? v | 1u << b : v & ~(1u << b); memcpy(beeld + 8, &v, 4); schrijf(); }

int main(int argc, char **argv) {
    if (argc != 5) { printf("gebruik: %s goed fout beeld probe\n", argv[0]); return 2; }
    fd = open(argv[3], O_RDWR); if (fd < 0) { printf("FAIL: bordbeeld\n"); return 1; }
    memset(beeld, 0, sizeof beeld); schrijf();

    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { printf("FAIL: dlopen %s: %s\n", argv[1], dlerror()); return 1; }
    int (*verb)(void) = (int (*)(void))dlsym(h, "RIO_Connected");
    unsigned (*sw)(int) = (unsigned (*)(int))dlsym(h, "RIO_SW_Count");
    int (*adc)(int) = (int (*)(int))dlsym(h, "RIO_ADC_Val");
    int (*adcx)(int, int *) = (int (*)(int, int *))dlsym(h, "RIO_ADC_Val_Ex");
    if (!verb || !sw || !adc || !adcx) { printf("FAIL: symbolen\n"); return 1; }

    /* Aangepast? Een niet-aangepaste functie van de goede build is int3: dan stopt het hier met SIGTRAP. */
    CHECK(verb() == 0, "RIO_Connected geeft %d, verwacht 0 (verbonden)", verb());

    /* Schakelaars: een teller van overgangen, oneven = ingedrukt. Tabel: bit 7 -> id 15, bit 6 -> id 7. */
    CHECK(sw(15) == 0, "id 15 in rust: %u, verwacht 0", sw(15));
    bit(7, 1); CHECK(sw(15) == 1, "id 15 na indrukken: %u, verwacht 1", sw(15));
    bit(7, 0); CHECK(sw(15) == 2, "id 15 na loslaten: %u, verwacht 2", sw(15));
    bit(6, 1); CHECK(sw(7) == 1 && sw(15) == 2, "bit 6 -> id 7: %u (id 15 blijft 2: %u)", sw(7), sw(15));
    bit(6, 0); CHECK(sw(7) == 2, "id 7 na loslaten: %u, verwacht 2", sw(7));
    CHECK(sw(9) == 0, "id zonder schakelaar geeft %u, verwacht 0", sw(9));
    /* min=300: een korte druk (30 ms) blijft voor het spel minstens 300 ms ingedrukt, daarna los. */
    bit(9, 1); bit(9, 0);
    CHECK(sw(11) == 1, "id 11 (min=300) kort na een korte druk: %u, verwacht 1 (nog ingedrukt)", sw(11));
    usleep(350000);
    CHECK(sw(11) == 2, "id 11 na 350 ms: %u, verwacht 2 (los)", sw(11));

    /* Kanalen: bron 12 -> ch 19 (0..4095), bron 13 -> ch 20 geinverteerd. round(min + f*(max-min)), dan inverteren. */
    beeld[12] = 0; beeld[13] = 0; schrijf();
    CHECK(adc(19) == 0, "ch 19 bij byte 0: %d, verwacht 0", adc(19));
    CHECK(adc(20) == 4095, "ch 20 (geinverteerd) bij byte 0: %d, verwacht 4095", adc(20));
    beeld[12] = 255; beeld[13] = 255; schrijf();
    CHECK(adc(19) == 4095 && adc(20) == 0, "bij byte 255: ch 19 %d (4095), ch 20 %d (0)", adc(19), adc(20));
    beeld[12] = 128; schrijf();
    int verwacht = (int)floor(128 / 255.0 * 4095 + 0.5);
    CHECK(adc(19) == verwacht, "ch 19 bij byte 128: %d, verwacht %d", adc(19), verwacht);
    int d = -1; int v = adcx(19, &d);
    CHECK(v == verwacht && d == verwacht - 4095, "RIO_ADC_Val_Ex: %d delta %d, verwacht %d delta %d", v, d, verwacht, verwacht - 4095);
    CHECK(adc(30) == 0, "kanaal zonder bron geeft %d, verwacht 0", adc(30));

    /* Een andere build: de brug laat hem met rust (de functies geven hun eigen waarde). */
    void *h2 = dlopen(argv[2], RTLD_NOW);
    if (!h2) { printf("FAIL: dlopen %s: %s\n", argv[2], dlerror()); return 1; }
    int (*verb2)(void) = (int (*)(void))dlsym(h2, "RIO_Connected");
    unsigned (*sw2)(int) = (unsigned (*)(int))dlsym(h2, "RIO_SW_Count");
    CHECK(verb2 && verb2() == 0x3333 && sw2 && sw2(15) == 0x4444, "een andere build is toch aangepast");

    /* Probe: "bit id" live erbij; weg is weer los (even). */
    FILE *p = fopen(argv[4], "w"); fprintf(p, "5 9\n"); fclose(p); usleep(400000);
    bit(5, 1); CHECK(sw(9) == 1, "probe bit 5 -> id 9 ingedrukt: %u, verwacht 1", sw(9));
    p = fopen(argv[4], "w"); fclose(p); usleep(400000);
    CHECK((sw(9) & 1) == 0, "probe weg terwijl bit 5 nog staat: id 9 = %u, verwacht even (los)", sw(9));
    bit(5, 0);

    if (fouten) { printf("%d fouten\n", fouten); return 1; }
    printf("OK: rio-brug (aanpassen, schakelaars, kanalen, andere build, probe)\n");
    return 0;
}

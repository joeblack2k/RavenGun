/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * matrix.c - onze eigen Matrix/SBK-donglelaag: een drop-in vervanger voor matrix32.dll/matrix64.dll.
 *
 * Waarom deze laag bestaat, en wanneer je hem gebruikt in plaats van een patch in de game:
 * zie docs/matrix-layer.md. Kort: de Harmony-patch in plugins/unity/ is de goedkoopste route voor
 * een Unity-game, deze DLL is de versie-onafhankelijke route (en de enige route voor een native
 * game, waar "patchen" een bytepatch zou zijn).
 *
 * Wat de game echt verbruikt (gemeten met dnlib.py op Assembly-CSharp.dll van Rabbids Hollywood):
 *   SBK.Matrix.MatrixManager.Awake roept KDLGNPHHMKI aan, en die doet:
 *       Init_MatrixAPI()                                  -> moet >= 0 zijn
 *       Dongle_Count(85)                                  -> moet > 0 zijn
 *       Dongle_ReadSerNr(dongle, aantal, 85)              -> moet > 0 zijn
 *   Zonder dongle geeft de originele DLL op de laatste twee 0, geeft de controle code 3 terug en
 *   volgt "**** DONGLE NOT FOUND" plus Application.Quit (na 30 seconden wachten). De game leest
 *   geen dongle-geheugen: Dongle_ReadData/Dongle_WriteData komen in het spelpad niet voor.
 *
 * Daarom geven wij precies die drie antwoorden, en is de rest van de 113 namen een stub die 0
 * teruggeeft. De gegevensbestanden blijven leeg voor deze game (dongle.json zonder memory-lijst);
 * leest een game ooit wel geheugen, dan laadt deze laag een image uit het bestand dat
 * RAVENGUN_MATRIX_DATA noemt (zie docs/matrix-layer.md).
 *
 * Referenties: findings/MATRIX-dongle-API.md (de 27 functies van de wrapper) en docs/matrix-layer.md
 * (de volledige exportlijst, de argumentgroottes en wat TeknoParrot's eigen emulator doet).
 */
#include "matrix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

/* --------------------------------------------------------------------------- hulp: logboek en image
 *
 * Deze laag leest zijn eigen gegevens; de launcher weet er niets van (docs/geen-dongle-in-de-loader.md).
 * De gegevens staan naast de bibliotheek in de drop-in-map van de game, bijvoorbeeld
 *
 *   patches/<Game>/dropin/matrix/matrix.ini     de instellingen (poort, aantal, geheugenbestand)
 *   patches/<Game>/dropin/matrix/matrix.bin     het dongle-geheugen, als een game dat leest
 *
 * en de drop-in geeft de weg mee: Env = RAVENGUN_MATRIX_DIR=matrix. Staat er niets, dan gelden de
 * waarden die in matrix.h staan (poort 85, aantal 2) en is er geen geheugen -- precies wat Rabbids
 * Hollywood nodig heeft.
 *
 * Er wordt nooit naar de NAS geschreven: het logpad komt uit de omgeving en de launcher kiest daar
 * een lokaal pad.
 */

static int mx_log_geopend;
static FILE *mx_log;

static void mx_log_regel(const char *wat, long waarde)
{
    const char *pad;
    if (!mx_log_geopend) {
        mx_log_geopend = 1;
        pad = getenv("RAVENGUN_MATRIX_LOG");
        if (pad && *pad) {
            mx_log = fopen(pad, "a");
        }
    }
    if (!mx_log) {
        return;
    }
    fprintf(mx_log, "[matrix] %s = %ld\n", wat, waarde);
    fflush(mx_log);
}

/* De map waarin deze DLL staat. Op Windows via GetModuleHandleExA op een adres in deze DLL (met NULL
 * geeft GetModuleFileNameA de exe, niet de DLL), native via /proc/self/exe -- zodat de zelftest
 * hetzelfde pad bepaalt als de DLL in de game. */
static void mx_module_map(char *uit, size_t n)
{
    uit[0] = '\0';
#if defined(_WIN32)
    {
        HMODULE zelf = NULL;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)(const void *)&mx_module_map, &zelf)) {
            return;
        }
        if (GetModuleFileNameA(zelf, uit, (DWORD)n) == 0) {
            uit[0] = '\0';
            return;
        }
    }
#else
    {
        ssize_t k = readlink("/proc/self/exe", uit, n - 1);
        if (k <= 0) {
            uit[0] = '\0';
            return;
        }
        uit[k] = '\0';
    }
#endif
    {
        char *laatste = strrchr(uit, '/');
#if defined(_WIN32)
        char *laatste_win = strrchr(uit, '\\');
        if (laatste_win && (!laatste || laatste_win > laatste)) {
            laatste = laatste_win;
        }
#endif
        if (laatste) {
            *laatste = '\0';
        }
    }
}

static int mx_bestaat(const char *pad)
{
    FILE *f = fopen(pad, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

static char mx_dir[1024];
static int mx_gegevens_gelezen;
static int mx_poort = MX_RABBIDS_POORT;
static int mx_aantal = MX_DONGLE_AANTAL;
static char mx_memory_naam[256];

/* Het geheugen. Leeg tenzij matrix.ini een memory-bestand noemt of RAVENGUN_MATRIX_DATA er een aanwijst. */
static int mx_image_geprobeerd;
static unsigned char *mx_image;
static long mx_image_lengte;

static void mx_trim(char *s)
{
    size_t n;
    while (*s == ' ' || *s == '\t') {
        memmove(s, s + 1, strlen(s));
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}

/* Leest <mx_dir>/matrix.ini: regels "sleutel = waarde", # en ; beginnen een opmerking. */
static void mx_ini_lezen(void)
{
    char pad[1400];
    char regel[512];
    FILE *f;

    if (!mx_dir[0]) {
        return;
    }
    snprintf(pad, sizeof(pad), "%s/matrix.ini", mx_dir);
    f = fopen(pad, "r");
    if (!f) {
        return;
    }
    while (fgets(regel, sizeof(regel), f)) {
        char *is = strchr(regel, '=');
        char *sleutel = regel;
        char *waarde;
        if (regel[0] == '#' || regel[0] == ';' || regel[0] == '\n') {
            continue;
        }
        if (!is) {
            continue;
        }
        *is = '\0';
        waarde = is + 1;
        mx_trim(sleutel);
        mx_trim(waarde);
        if (strcmp(sleutel, "poort") == 0) {
            mx_poort = atoi(waarde);
        } else if (strcmp(sleutel, "aantal") == 0) {
            mx_aantal = atoi(waarde);
        } else if (strcmp(sleutel, "memory") == 0) {
            snprintf(mx_memory_naam, sizeof(mx_memory_naam), "%s", waarde);
        }
    }
    fclose(f);
    mx_log_regel("matrix.ini gelezen, poort", mx_poort);
    mx_log_regel("matrix.ini gelezen, aantal", mx_aantal);
}

/* Zoekt de gegevensmap: RAVENGUN_MATRIX_DIR zoals hij is (absoluut of ten opzichte van de werkmap),
 * en anders naast deze DLL. Herkend aan een matrix.ini erin. */
static void mx_gegevens_vinden(void)
{
    const char *gevraagd = getenv("RAVENGUN_MATRIX_DIR");
    char eigen[1024];
    char poging[1400];
    static const char *standaard = "matrix";

    if (mx_gegevens_gelezen) {
        return;
    }
    mx_gegevens_gelezen = 1;

    mx_module_map(eigen, sizeof(eigen));
    if (!gevraagd || !*gevraagd) {
        gevraagd = standaard;
    }

    /* 1. zoals gevraagd (een absoluut pad, of relatief ten opzichte van de werkmap) */
    snprintf(poging, sizeof(poging), "%s/matrix.ini", gevraagd);
    if (mx_bestaat(poging)) {
        snprintf(mx_dir, sizeof(mx_dir), "%s", gevraagd);
    } else if (eigen[0]) {
        /* 2. naast deze DLL: de gegevens horen naast de bibliotheek te staan */
        snprintf(poging, sizeof(poging), "%s/%s/matrix.ini", eigen, gevraagd);
        if (mx_bestaat(poging)) {
            snprintf(mx_dir, sizeof(mx_dir), "%s/%s", eigen, gevraagd);
        }
    }

    if (mx_dir[0]) {
        mx_log_regel("gegevensmap gevonden", 0);
        mx_ini_lezen();
    } else {
        mx_log_regel("geen gegevensmap gevonden, standaardwaarden", 0);
    }
}

static void mx_image_laden(void)
{
    const char *pad;
    char volledig[1400];
    FILE *f;
    long n;

    if (mx_image_geprobeerd) {
        return;
    }
    mx_image_geprobeerd = 1;
    mx_gegevens_vinden();

    pad = getenv("RAVENGUN_MATRIX_DATA");
    if (!pad || !*pad) {
        if (!mx_memory_naam[0]) {
            return;   /* deze game leest geen dongle-geheugen */
        }
        snprintf(volledig, sizeof(volledig), "%s/%s", mx_dir, mx_memory_naam);
        pad = volledig;
    }
    f = fopen(pad, "rb");
    if (!f) {
        mx_log_regel("geheugenbestand niet te openen", 0);
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    n = ftell(f);
    if (n <= 0 || n > (16L * 1024 * 1024)) {   /* een dongle is klein; weiger onzin */
        fclose(f);
        return;
    }
    rewind(f);
    mx_image = (unsigned char *)malloc((size_t)n);
    if (!mx_image) {
        fclose(f);
        return;
    }
    if (fread(mx_image, 1, (size_t)n, f) != (size_t)n) {
        free(mx_image);
        mx_image = NULL;
        fclose(f);
        return;
    }
    fclose(f);
    mx_image_lengte = n;
    mx_log_regel("geheugen geladen, lengte", n);
}

/* ------------------------------------------------------------------------------------- de 27 functies */

short MX_CALL Init_MatrixAPI(void)
{
    /* 0 = in orde. Een negatieve waarde betekent voor de game "MatrixAPI niet geinitialiseerd" en
     * dan gebruikt hij de rest van de API niet eens. Meteen ook de eigen gegevens lezen. */
    mx_gegevens_vinden();
    mx_log_regel("Init_MatrixAPI -> 0", 0);
    return 0;
}

short MX_CALL Release_MatrixAPI(void)
{
    if (mx_image) {
        free(mx_image);
        mx_image = NULL;
        mx_image_lengte = 0;
        mx_image_geprobeerd = 0;
    }
    return 0;
}

int32_t MX_CALL GetVersionAPI(void)
{
    /* Zelfgekozen versie van onze laag (1.0). Geen enkele game in dit project vergelijkt hem. */
    return 0x00000100;
}

int32_t MX_CALL GetVersionDRV(void)
{
    /* Er is geen echte stuurprogramma's; 0 = geen. */
    return 0;
}

int32_t MX_CALL GetVersionDRV_USB(void)
{
    return 0;
}

void MX_CALL SetW95Access(short toegang)
{
    (void)toegang;
}

short MX_CALL GetPortAdr(short poort)
{
    (void)poort;
    /* Het adres van de parallelle poort. Wij hebben geen echte poort; de game gebruikt dit niet. */
    return 0;
}

short MX_CALL PausePrinterActivity(void)
{
    return 0;
}

short MX_CALL ResumePrinterActivity(void)
{
    return 0;
}

short MX_CALL Dongle_Find(void)
{
    return 1;
}

int32_t MX_CALL Dongle_Version(short poort, short index)
{
    (void)poort;
    (void)index;
    /* Zelfgekozen: versie 1.0 van de nagebootste dongle. */
    return 0x00000100;
}

int32_t MX_CALL Dongle_Model(short poort, short index)
{
    (void)poort;
    (void)index;
    /* Zelfgekozen modelnummer; de game gebruikt het niet. */
    return 1;
}

short MX_CALL Dongle_MemSize(short poort, short index)
{
    (void)poort;
    (void)index;
    mx_image_laden();
    /* Zonder image is de grootte 0; dat is eerlijk: wij hebben geen dongle-geheugen. */
    return (short)(mx_image ? mx_image_lengte : 0);
}

short MX_CALL Dongle_Count(short poort)
{
    mx_gegevens_vinden();
    mx_log_regel("Dongle_Count -> aantal, poort", poort);
    return (short)mx_aantal;
}

short MX_CALL Dongle_ReadData(int32_t dongle, int32_t *buf, short poort, short adres, short lengte)
{
    long start = (long)adres;
    long n = (long)lengte;

    (void)dongle;
    (void)poort;

    mx_image_laden();
    if (!buf || n <= 0) {
        return 1;
    }
    if (!mx_image || start < 0 || start >= mx_image_lengte) {
        /* Geen gegevens: 1 = fout, net als een echte dongle zonder inhoud op dat adres. */
        return 1;
    }
    if (start + n > mx_image_lengte) {
        n = mx_image_lengte - start;
    }
    memcpy(buf, mx_image + start, (size_t)n);
    mx_log_regel("Dongle_ReadData, bytes", n);
    return 0;
}

short MX_CALL Dongle_ReadDataEx(int32_t dongle, int32_t *buf, short poort, short adres, short lengte,
                                short extra)
{
    (void)extra;
    return Dongle_ReadData(dongle, buf, poort, adres, lengte);
}

int32_t MX_CALL Dongle_ReadSerNr(int32_t dongle, short aantal, short poort)
{
    (void)dongle;
    (void)aantal;
    mx_gegevens_vinden();
    /* Vaste, zelfgekozen waarde; nooit een echt serienummer, en niet instelbaar via matrix.ini -- dat
     * zou een echt serienummer in een bestand zetten. Zie matrix.h.
     * De tweede parameter van de aanroep is het aantal dongles, de derde de poort (bij Rabbids
     * Hollywood: aantal 2, poort 85). */
    mx_log_regel("Dongle_ReadSerNr -> 69420, poort", poort);
    return MX_SERIENUMMER;
}

short MX_CALL Dongle_WriteData(int32_t dongle, int32_t *buf, short poort, short adres, short lengte)
{
    long start = (long)adres;
    long n = (long)lengte;

    (void)dongle;
    (void)poort;

    mx_image_laden();
    if (!buf || n <= 0) {
        return 1;
    }
    if (!mx_image || start < 0 || start >= mx_image_lengte) {
        return 1;
    }
    if (start + n > mx_image_lengte) {
        n = mx_image_lengte - start;
    }
    memcpy(mx_image + start, buf, (size_t)n);
    return 0;
}

short MX_CALL Dongle_WriteDataEx(int32_t dongle, int32_t *buf, short poort, short adres, short lengte,
                                 short extra)
{
    (void)extra;
    return Dongle_WriteData(dongle, buf, poort, adres, lengte);
}

short MX_CALL Dongle_WriteKey(int32_t dongle, int32_t *sleutel, short poort, short index)
{
    (void)dongle;
    (void)sleutel;
    (void)poort;
    (void)index;
    return 0;
}

short MX_CALL Dongle_GetKeyFlag(int32_t dongle, short poort, short index)
{
    (void)dongle;
    (void)poort;
    (void)index;
    return 0;
}

short MX_CALL Dongle_Exit(void)
{
    return Release_MatrixAPI();
}

int32_t MX_CALL GetConfig_MatrixNet(short poort)
{
    (void)poort;
    return 0;
}

short MX_CALL LogIn_MatrixNet(int32_t dongle, short poort, short index)
{
    (void)dongle;
    (void)poort;
    (void)index;
    return 0;
}

short MX_CALL LogOut_MatrixNet(int32_t dongle, short poort, short index)
{
    (void)dongle;
    (void)poort;
    (void)index;
    return 0;
}

short MX_CALL Dongle_EncryptData(int32_t dongle, int32_t *buf, short poort, short lengte)
{
    /* Ongewijzigd laten en 0 teruggeven, net als bij de HASP-laag: de game versleutelt daarmee zijn
     * eigen opslag, en zolang wij beide richtingen hetzelfde behandelen blijft dat in zichzelf
     * consistent. Zie docs/matrix-layer.md. */
    (void)dongle;
    (void)buf;
    (void)poort;
    (void)lengte;
    return 0;
}

short MX_CALL Dongle_DecryptData(int32_t dongle, int32_t *buf, short poort, short lengte)
{
    (void)dongle;
    (void)buf;
    (void)poort;
    (void)lengte;
    return 0;
}

/* ------------------------------------------------------------- de rest van de 113 namen: stubs */

short MX_CALL Dongle_FindEx(short poort)
{
    (void)poort;
    return 1;
}

short MX_CALL Dongle_GetDriverFlag(int32_t dongle, short poort, short index)
{
    (void)dongle;
    (void)poort;
    (void)index;
    return 0;
}

short MX_CALL Dongle_SetDriverFlag(int32_t dongle, int32_t *buf, short poort, short index)
{
    (void)dongle;
    (void)buf;
    (void)poort;
    (void)index;
    return 0;
}

short MX_CALL Dongle_SetLedFlag(int32_t dongle, int32_t *buf, short poort, short index)
{
    (void)dongle;
    (void)buf;
    (void)poort;
    (void)index;
    return 0;
}

short MX_CALL Dongle_GetLoginFlag(int32_t dongle, short poort, short index)
{
    (void)dongle;
    (void)poort;
    (void)index;
    return 0;
}

int32_t MX_CALL GetSystemVersion(void)
{
    return 0;
}

short MX_CALL MasterKey_Find(int32_t dongle, short index)
{
    (void)dongle;
    (void)index;
    return 0;
}

int32_t MX_CALL MasterKey_Find_USB2(short index)
{
    (void)index;
    return 0;
}

short MX_CALL SetConfig_MatrixNet(int32_t waarde, short poort)
{
    (void)waarde;
    (void)poort;
    return 0;
}

short MX_CALL OpenIWPort(short poort)
{
    (void)poort;
    return 0;
}

short MX_CALL CloseIWPort(short poort)
{
    (void)poort;
    return 0;
}

int32_t MX_CALL ReadPort(short poort, short adres)
{
    (void)poort;
    (void)adres;
    return 0;
}

int32_t MX_CALL WritePort(short poort, short adres, int32_t waarde)
{
    (void)poort;
    (void)adres;
    (void)waarde;
    return 0;
}

int32_t MX_CALL HiResWait(int32_t microseconden)
{
    (void)microseconden;
    return 0;
}

short MX_CALL HiResCntPresent(void)
{
    return 0;
}

int32_t MX_CALL get_user_name(int32_t *buf, int32_t lengte)
{
    (void)buf;
    (void)lengte;
    return 0;
}

int32_t MX_CALL DllRegisterServer(void)
{
    return 0;
}

int32_t MX_CALL DllUnregisterServer(void)
{
    return 0;
}

/* De Java/JNI-wrappers: geen van de games in dit project loopt via de JVM, dus ze doen niets. Ze
 * bestaan alleen zodat onze DLL dezelfde 113 namen exporteert als het origineel. */
#define MX_JNI_2(naam) int32_t MX_CALL naam(void *a, void *b) \
    { (void)a; (void)b; return 0; }
#define MX_JNI_3(naam) int32_t MX_CALL naam(void *a, void *b, void *c) \
    { (void)a; (void)b; (void)c; return 0; }
#define MX_JNI_4(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d) \
    { (void)a; (void)b; (void)c; (void)d; return 0; }
#define MX_JNI_5(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d, void *e) \
    { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
#define MX_JNI_6(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d, void *e, void *f) \
    { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
#define MX_JNI_7(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d, void *e, void *f, void *g) \
    { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return 0; }
#define MX_JNI_8(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h) \
    { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; return 0; }

MX_JNI_3(Java_Matrix_Dongle_1Count)
MX_JNI_6(Java_Matrix_Dongle_1DecryptData)
MX_JNI_6(Java_Matrix_Dongle_1EncryptData)
MX_JNI_2(Java_Matrix_Dongle_1Exit)
MX_JNI_2(Java_Matrix_Dongle_1Find)
MX_JNI_3(Java_Matrix_Dongle_1FindEx)
MX_JNI_5(Java_Matrix_Dongle_1GetDriverFlag)
MX_JNI_5(Java_Matrix_Dongle_1GetKeyFlag)
MX_JNI_3(Java_Matrix_Dongle_1GetLoginFlag)
MX_JNI_4(Java_Matrix_Dongle_1MemSize)
MX_JNI_4(Java_Matrix_Dongle_1Model)
MX_JNI_7(Java_Matrix_Dongle_1ReadData)
MX_JNI_8(Java_Matrix_Dongle_1ReadDataEx)
MX_JNI_5(Java_Matrix_Dongle_1ReadSerNr)
MX_JNI_6(Java_Matrix_Dongle_1SetDriverFlag)
MX_JNI_6(Java_Matrix_Dongle_1SetLedFlag)
MX_JNI_4(Java_Matrix_Dongle_1Version)
MX_JNI_7(Java_Matrix_Dongle_1WriteData)
MX_JNI_8(Java_Matrix_Dongle_1WriteDataEx)
MX_JNI_6(Java_Matrix_Dongle_1WriteKey)
MX_JNI_3(Java_Matrix_GetConfig_1MatrixNet)
MX_JNI_3(Java_Matrix_GetPortAdr)
MX_JNI_2(Java_Matrix_GetVersionAPI)
MX_JNI_2(Java_Matrix_GetVersionDRV)
MX_JNI_2(Java_Matrix_GetVersionDRV_1USB)
MX_JNI_2(Java_Matrix_Init_1MatrixAPI)
MX_JNI_5(Java_Matrix_LogIn_1MatrixNet)
MX_JNI_5(Java_Matrix_LogOut_1MatrixNet)
MX_JNI_4(Java_Matrix_MasterKey_1Find)
MX_JNI_2(Java_Matrix_PausePrinterActivity)
MX_JNI_2(Java_Matrix_Release_1MatrixAPI)
MX_JNI_2(Java_Matrix_ResumePrinterActivity)
MX_JNI_4(Java_Matrix_SetConfig_1MatrixNet)
MX_JNI_3(Java_Matrix_SetW95Access)

MX_JNI_3(Java_de_matrixlock_Matrix_Dongle_1Count)
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1DecryptData)
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1EncryptData)
MX_JNI_2(Java_de_matrixlock_Matrix_Dongle_1Exit)
MX_JNI_2(Java_de_matrixlock_Matrix_Dongle_1Find)
MX_JNI_3(Java_de_matrixlock_Matrix_Dongle_1FindEx)
MX_JNI_5(Java_de_matrixlock_Matrix_Dongle_1GetDriverFlag)
MX_JNI_5(Java_de_matrixlock_Matrix_Dongle_1GetKeyFlag)
MX_JNI_3(Java_de_matrixlock_Matrix_Dongle_1GetLoginFlag)
MX_JNI_4(Java_de_matrixlock_Matrix_Dongle_1MemSize)
MX_JNI_4(Java_de_matrixlock_Matrix_Dongle_1Model)
MX_JNI_7(Java_de_matrixlock_Matrix_Dongle_1ReadData)
MX_JNI_8(Java_de_matrixlock_Matrix_Dongle_1ReadDataEx)
MX_JNI_5(Java_de_matrixlock_Matrix_Dongle_1ReadSerNr)
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1SetDriverFlag)
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1SetLedFlag)
MX_JNI_4(Java_de_matrixlock_Matrix_Dongle_1Version)
MX_JNI_7(Java_de_matrixlock_Matrix_Dongle_1WriteData)
MX_JNI_8(Java_de_matrixlock_Matrix_Dongle_1WriteDataEx)
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1WriteKey)
MX_JNI_3(Java_de_matrixlock_Matrix_GetConfig_1MatrixNet)
MX_JNI_3(Java_de_matrixlock_Matrix_GetPortAdr)
MX_JNI_2(Java_de_matrixlock_Matrix_GetVersionAPI)
MX_JNI_2(Java_de_matrixlock_Matrix_GetVersionDRV)
MX_JNI_2(Java_de_matrixlock_Matrix_GetVersionDRV_1USB)
MX_JNI_2(Java_de_matrixlock_Matrix_Init_1MatrixAPI)
MX_JNI_5(Java_de_matrixlock_Matrix_LogIn_1MatrixNet)
MX_JNI_5(Java_de_matrixlock_Matrix_LogOut_1MatrixNet)
MX_JNI_4(Java_de_matrixlock_Matrix_MasterKey_1Find)
MX_JNI_2(Java_de_matrixlock_Matrix_PausePrinterActivity)
MX_JNI_2(Java_de_matrixlock_Matrix_Release_1MatrixAPI)
MX_JNI_2(Java_de_matrixlock_Matrix_ResumePrinterActivity)
MX_JNI_4(Java_de_matrixlock_Matrix_SetConfig_1MatrixNet)
MX_JNI_3(Java_de_matrixlock_Matrix_SetW95Access)

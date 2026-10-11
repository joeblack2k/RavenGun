/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * matrix.h - de Matrix/SBK-dongle-API zoals de originele matrix32.dll/matrix64.dll hem exporteert.
 *
 * De API is nagelopen op de echte bestanden (zie docs/matrix-layer.md):
 *   - de originele 32-bits dongle-DLL (114688 bytes, 2009) exporteert 113 namen;
 *   - de 64-bits versie (93184 bytes) exporteert exact dezelfde 113 namen, in dezelfde volgorde,
 *     met ordinal base 1.
 * De C#-wrapper van de games (MatrixDLLCSharp.MatrixWrapper in Assembly-CSharp.dll) gebruikt
 * daarvan 27; de rest zijn Java/JNI-wrappers, poort-I/O en hulpfuncties.
 *
 * De argumentgroottes hieronder zijn niet geraden: elke stdcall-functie in de originele 32-bits DLL
 * eindigt op `ret N`, en N is het aantal argumentbytes. Die N staat per functie in de tabel in
 * docs/matrix-layer.md. scripts/build-matrix.sh controleert na het bouwen dat onze DLL dezelfde
 * `ret N` heeft, zodat de stapel in balans blijft als een game ons bestand in de plaats van het
 * originele laadt.
 */
#ifndef RAVENGUN_MATRIX_H
#define RAVENGUN_MATRIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* De oorspronkelijke DLL gebruikt de stdcall-conventie (Windows). Op i686 is die verplicht om
 * dezelfde stapelopruiming (`ret N`) te krijgen als het origineel; op x86-64 bestaat stdcall niet
 * meer en is de conventie vast. Zo compileert deze bron ook native (voor de zelftest). */
#if defined(__i386__)
#define MX_CALL __attribute__((stdcall))
#else
#define MX_CALL
#endif

/* ------------------------------------------------------------------ de 27 functies van de wrapper */

short MX_CALL Init_MatrixAPI(void);
short MX_CALL Release_MatrixAPI(void);
int32_t MX_CALL GetVersionAPI(void);
int32_t MX_CALL GetVersionDRV(void);
int32_t MX_CALL GetVersionDRV_USB(void);
void MX_CALL SetW95Access(short toegang);
short MX_CALL GetPortAdr(short poort);
short MX_CALL PausePrinterActivity(void);
short MX_CALL ResumePrinterActivity(void);
short MX_CALL Dongle_Find(void);
int32_t MX_CALL Dongle_Version(short poort, short index);
int32_t MX_CALL Dongle_Model(short poort, short index);
short MX_CALL Dongle_MemSize(short poort, short index);
short MX_CALL Dongle_Count(short poort);
short MX_CALL Dongle_ReadData(int32_t dongle, int32_t *buf, short poort, short adres, short lengte);
short MX_CALL Dongle_ReadDataEx(int32_t dongle, int32_t *buf, short poort, short adres, short lengte,
                                short extra);
int32_t MX_CALL Dongle_ReadSerNr(int32_t dongle, short aantal, short poort);
short MX_CALL Dongle_WriteData(int32_t dongle, int32_t *buf, short poort, short adres, short lengte);
short MX_CALL Dongle_WriteDataEx(int32_t dongle, int32_t *buf, short poort, short adres, short lengte,
                                 short extra);
short MX_CALL Dongle_WriteKey(int32_t dongle, int32_t *sleutel, short poort, short index);
short MX_CALL Dongle_GetKeyFlag(int32_t dongle, short poort, short index);
short MX_CALL Dongle_Exit(void);
int32_t MX_CALL GetConfig_MatrixNet(short poort);
short MX_CALL LogIn_MatrixNet(int32_t dongle, short poort, short index);
short MX_CALL LogOut_MatrixNet(int32_t dongle, short poort, short index);
short MX_CALL Dongle_EncryptData(int32_t dongle, int32_t *buf, short poort, short lengte);
short MX_CALL Dongle_DecryptData(int32_t dongle, int32_t *buf, short poort, short lengte);

/* --------------------------------------------------- de rest van de 113 namen (geen spel gebruikt ze) */

short MX_CALL Dongle_FindEx(short poort);
short MX_CALL Dongle_GetDriverFlag(int32_t dongle, short poort, short index);
short MX_CALL Dongle_SetDriverFlag(int32_t dongle, int32_t *buf, short poort, short index);
short MX_CALL Dongle_SetLedFlag(int32_t dongle, int32_t *buf, short poort, short index);
short MX_CALL Dongle_GetLoginFlag(int32_t dongle, short poort, short index);
int32_t MX_CALL GetSystemVersion(void);
short MX_CALL MasterKey_Find(int32_t dongle, short index);
int32_t MX_CALL MasterKey_Find_USB2(short index);
short MX_CALL SetConfig_MatrixNet(int32_t waarde, short poort);
short MX_CALL OpenIWPort(short poort);
short MX_CALL CloseIWPort(short poort);
int32_t MX_CALL ReadPort(short poort, short adres);
int32_t MX_CALL WritePort(short poort, short adres, int32_t waarde);
int32_t MX_CALL HiResWait(int32_t microseconden);
short MX_CALL HiResCntPresent(void);
int32_t MX_CALL get_user_name(int32_t *buf, int32_t lengte);
int32_t MX_CALL DllRegisterServer(void);
int32_t MX_CALL DllUnregisterServer(void);

/* De Java/JNI-wrappers. Hun argumentaantal komt uit de `ret N` van het origineel; ze doen niets,
 * want geen enkele game in dit project gaat via de JVM. */
#define MX_JNI_2(naam) int32_t MX_CALL naam(void *a, void *b)
#define MX_JNI_3(naam) int32_t MX_CALL naam(void *a, void *b, void *c)
#define MX_JNI_4(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d)
#define MX_JNI_5(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d, void *e)
#define MX_JNI_6(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d, void *e, void *f)
#define MX_JNI_7(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d, void *e, void *f, void *g)
#define MX_JNI_8(naam) int32_t MX_CALL naam(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h)

MX_JNI_3(Java_Matrix_Dongle_1Count);
MX_JNI_6(Java_Matrix_Dongle_1DecryptData);
MX_JNI_6(Java_Matrix_Dongle_1EncryptData);
MX_JNI_2(Java_Matrix_Dongle_1Exit);
MX_JNI_2(Java_Matrix_Dongle_1Find);
MX_JNI_3(Java_Matrix_Dongle_1FindEx);
MX_JNI_5(Java_Matrix_Dongle_1GetDriverFlag);
MX_JNI_5(Java_Matrix_Dongle_1GetKeyFlag);
MX_JNI_3(Java_Matrix_Dongle_1GetLoginFlag);
MX_JNI_4(Java_Matrix_Dongle_1MemSize);
MX_JNI_4(Java_Matrix_Dongle_1Model);
MX_JNI_7(Java_Matrix_Dongle_1ReadData);
MX_JNI_8(Java_Matrix_Dongle_1ReadDataEx);
MX_JNI_5(Java_Matrix_Dongle_1ReadSerNr);
MX_JNI_6(Java_Matrix_Dongle_1SetDriverFlag);
MX_JNI_6(Java_Matrix_Dongle_1SetLedFlag);
MX_JNI_4(Java_Matrix_Dongle_1Version);
MX_JNI_7(Java_Matrix_Dongle_1WriteData);
MX_JNI_8(Java_Matrix_Dongle_1WriteDataEx);
MX_JNI_6(Java_Matrix_Dongle_1WriteKey);
MX_JNI_3(Java_Matrix_GetConfig_1MatrixNet);
MX_JNI_3(Java_Matrix_GetPortAdr);
MX_JNI_2(Java_Matrix_GetVersionAPI);
MX_JNI_2(Java_Matrix_GetVersionDRV);
MX_JNI_2(Java_Matrix_GetVersionDRV_1USB);
MX_JNI_2(Java_Matrix_Init_1MatrixAPI);
MX_JNI_5(Java_Matrix_LogIn_1MatrixNet);
MX_JNI_5(Java_Matrix_LogOut_1MatrixNet);
MX_JNI_4(Java_Matrix_MasterKey_1Find);
MX_JNI_2(Java_Matrix_PausePrinterActivity);
MX_JNI_2(Java_Matrix_Release_1MatrixAPI);
MX_JNI_2(Java_Matrix_ResumePrinterActivity);
MX_JNI_4(Java_Matrix_SetConfig_1MatrixNet);
MX_JNI_3(Java_Matrix_SetW95Access);

/* De de_matrixlock-varianten zijn in het origineel byte-identiek aan de Java_Matrix-varianten
 * (dezelfde RVA's), dus dezelfde argumentaantallen. */
MX_JNI_3(Java_de_matrixlock_Matrix_Dongle_1Count);
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1DecryptData);
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1EncryptData);
MX_JNI_2(Java_de_matrixlock_Matrix_Dongle_1Exit);
MX_JNI_2(Java_de_matrixlock_Matrix_Dongle_1Find);
MX_JNI_3(Java_de_matrixlock_Matrix_Dongle_1FindEx);
MX_JNI_5(Java_de_matrixlock_Matrix_Dongle_1GetDriverFlag);
MX_JNI_5(Java_de_matrixlock_Matrix_Dongle_1GetKeyFlag);
MX_JNI_3(Java_de_matrixlock_Matrix_Dongle_1GetLoginFlag);
MX_JNI_4(Java_de_matrixlock_Matrix_Dongle_1MemSize);
MX_JNI_4(Java_de_matrixlock_Matrix_Dongle_1Model);
MX_JNI_7(Java_de_matrixlock_Matrix_Dongle_1ReadData);
MX_JNI_8(Java_de_matrixlock_Matrix_Dongle_1ReadDataEx);
MX_JNI_5(Java_de_matrixlock_Matrix_Dongle_1ReadSerNr);
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1SetDriverFlag);
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1SetLedFlag);
MX_JNI_4(Java_de_matrixlock_Matrix_Dongle_1Version);
MX_JNI_7(Java_de_matrixlock_Matrix_Dongle_1WriteData);
MX_JNI_8(Java_de_matrixlock_Matrix_Dongle_1WriteDataEx);
MX_JNI_6(Java_de_matrixlock_Matrix_Dongle_1WriteKey);
MX_JNI_3(Java_de_matrixlock_Matrix_GetConfig_1MatrixNet);
MX_JNI_3(Java_de_matrixlock_Matrix_GetPortAdr);
MX_JNI_2(Java_de_matrixlock_Matrix_GetVersionAPI);
MX_JNI_2(Java_de_matrixlock_Matrix_GetVersionDRV);
MX_JNI_2(Java_de_matrixlock_Matrix_GetVersionDRV_1USB);
MX_JNI_2(Java_de_matrixlock_Matrix_Init_1MatrixAPI);
MX_JNI_5(Java_de_matrixlock_Matrix_LogIn_1MatrixNet);
MX_JNI_5(Java_de_matrixlock_Matrix_LogOut_1MatrixNet);
MX_JNI_4(Java_de_matrixlock_Matrix_MasterKey_1Find);
MX_JNI_2(Java_de_matrixlock_Matrix_PausePrinterActivity);
MX_JNI_2(Java_de_matrixlock_Matrix_Release_1MatrixAPI);
MX_JNI_2(Java_de_matrixlock_Matrix_ResumePrinterActivity);
MX_JNI_4(Java_de_matrixlock_Matrix_SetConfig_1MatrixNet);
MX_JNI_3(Java_de_matrixlock_Matrix_SetW95Access);

#undef MX_JNI_2
#undef MX_JNI_3
#undef MX_JNI_4
#undef MX_JNI_5
#undef MX_JNI_6
#undef MX_JNI_7
#undef MX_JNI_8

/* ---------------------------------------------------------------- de waarden die wij teruggeven
 *
 * Dit zijn de standaardwaarden. Een game mag ze overschrijven met zijn eigen gegevensbestand naast de
 * bibliotheek: <RAVENGUN_MATRIX_DIR>/matrix.ini met de sleutels poort, aantal en memory (de naam van
 * het dongle-geheugen binnen die map). De drop-in geeft de weg mee; de launcher weet nergens van
 * (docs/geen-dongle-in-de-loader.md). Zonder dat bestand gelden deze waarden.
 */

/* De poort waarop Rabbids Hollywood controleert. SBK.Matrix.MatrixManager.Awake roept
 * KDLGNPHHMKI aan, en die gebruikt Dongle_Count(85) en Dongle_ReadSerNr(..., 85). */
#define MX_RABBIDS_POORT 85

/* Aantal dongles. De controle eist > 0; er is nog een ongebruikte tweede controlevariant in de
 * assembly die > 1 eist. TeknoParrot's eigen matrix32.dll geeft hier 2 terug; wij doen hetzelfde,
 * zodat beide varianten slagen. */
#define MX_DONGLE_AANTAL 2

/* Het "serienummer". De controle eist > 0. Dit is een vaste, zelfgekozen waarde: er wordt geen
 * echt serienummer van een dongle gelezen of gelogd. 0x10F2C is dezelfde waarde die TeknoParrot's
 * eigen matrix32.dll teruggeeft (bewezen met objdump: `mov eax,0x10f2c; ret`). */
#define MX_SERIENUMMER 0x10F2C

#ifdef __cplusplus
}
#endif

#endif /* RAVENGUN_MATRIX_H */

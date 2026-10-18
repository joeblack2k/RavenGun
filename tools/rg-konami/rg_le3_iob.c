/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-konami: RavenGuns eigen iob.dll voor Lethal Enforcers 3 (Konami, 2005). Op de kast praat Konami's iob.dll over een seriele poort
 * met het I/O-bord (knoppen, munten, lampen, schudmotoren); hier beantwoordt deze DLL de game uit het bordbeeld van RavenGun, zonder
 * bord en zonder de kern van TeknoParrot. Dezelfde 44 exports (namen en ordinalen) als het origineel: le3_iob.def.
 *
 * CLEAN-ROOM: geschreven uit de aanroepen in game.exe (de opstartmachine op 0x51ea80, de toegangsfuncties 0x51d6e0..0x51d8d0) en uit
 * wat het origineel in IOB_DIGITAL_IN_W schrijft (iob.dll 0x10006400). Geen code van het origineel. Zie konami_logic.h en
 * docs/research/konami/README.md.
 *
 * De opstartmachine van de game (0x51eb1f, toestand e430) wil achtereenvolgens: iob_boot_wait() == 1, iob_check_need_firm_update() == 0,
 * iob_check_initial_security_checked() == 0 en iob_check_error() == 0; daarna start hij het geweerbord (gunio.dll).
 */
#define KB_NAAM "iob.dll(LE3)"
#include "kb_win.h"

static le3_iob_staat staat;
static int muntteller_aan = 1;

/* Een handvol regels per soort invoer, als bewijs in het logboek dat de knoppen aankomen. */
static void meld_flanken(const kb_beeld *nu, const kb_beeld *vorig)
{
    static const struct { int bit; const char *naam; } namen[] = {
        {KB_TEST, "TEST"}, {KB_SERVICE, "SERVICE"}, {KB_MUNT1, "MUNT 1"}, {KB_MUNT2, "MUNT 2"},
        {KB_START(0), "START P1"}, {KB_TREKKER(0), "TREKKER P1"}, {KB_HERLADEN(0), "OPTION P1"}, {KB_GRANAAT(0), "RELOAD P1"},
        {KB_START(1), "START P2"}, {KB_TREKKER(1), "TREKKER P2"}, {KB_HERLADEN(1), "OPTION P2"}, {KB_GRANAAT(1), "RELOAD P2"},
    };
    for (size_t i = 0; i < sizeof namen / sizeof namen[0]; i++)
        if (kb_flank(nu, vorig, namen[i].bit) && kb_meldingen < 200) {
            kb_meldingen++;
            kb_log("invoer: %s ingedrukt", namen[i].naam);
        }
}

int le3_iob_get_digital_in(uint8_t *uit)
{
    kb_hoofdlus_gezien = 1;   /* de hoofdlus van game.exe (0x51ea80) roept dit elk beeld aan: de waakhond kan stoppen */
    kb_meld_geladen("iob_get_digital_in");
    kb_beeld b;
    kb_beeld_nu(&b);
    EnterCriticalSection(&kb_slot);
    const kb_beeld vorig = staat.vorig;
    uint8_t buf[LE3_DIN_GROOTTE];
    le3_digital_in(&staat, &b, buf);
    LeaveCriticalSection(&kb_slot);
    if (staat.begonnen) meld_flanken(&b, &vorig);
    if (uit) memcpy(uit, buf, LE3_DIN_GROOTTE);
    return 0;
}

int le3_iob_init(const void *model, int a, int b)
{
    kb_meld_geladen("iob_init");
    kb_log("iob_init(model=%08lx, %d, %d): bord van RavenGun", (unsigned long)(uintptr_t)model, a, b);
    return 0;
}

int le3_iob_start(void) { kb_log("iob_start"); return 0; }
int le3_iob_end(void) { kb_log("iob_end"); return 0; }

int le3_iob_boot_wait(void)
{
    static int gemeld;
    if (!gemeld) { gemeld = 1; kb_log("iob_boot_wait: 1 (bord klaar)"); }
    return 1;
}

unsigned le3_iob_check_error(void) { return 0; }
unsigned le3_iob_get_boot_error(void) { return 0; }
int le3_iob_check_initial_security_checked(void) { return 0; }
int le3_iob_check_need_firm_update(void) { return 0; }
void le3_iob_firm_update_start(void) {}
int le3_iob_firm_update_wait(void) { return 1; }

void le3_iob_loop_keeper_init(int ms) { (void)ms; }
void le3_iob_loop_keeper_main(void) {}

const char *le3_iob_get_id(int i) { (void)i; return "RAVENGUN"; }
char *le3_iob_get_version(void) { static char v[] = "RavenGun rg-konami"; return v; }

int le3_iob_get_firmware_version(uint8_t *a, uint8_t *b, uint8_t *c)
{
    if (a) *a = 1;
    if (b) *b = 0;
    if (c) *c = 0;
    return 0;
}

/* Niet door game.exe gebruikt; de grootte van de array is niet bekend, dus niets schrijven. */
int le3_iob_get_model_code(uint8_t *code) { (void)code; return 0; }

void le3_iob_set_coinctr_enable(void) { muntteller_aan = 1; }
void le3_iob_set_coinctr_disable(void) { muntteller_aan = 0; }
int le3_iob_check_coinctr_enable(void) { return muntteller_aan; }

/* Lampen, schudmotoren, geweerterugslag en voetverlichting: geen hardware, niets te doen. */
void le3_iob_auto_ex_in_out_check_off(void) {}
void le3_iob_auto_ex_in_out_check_on(void) {}
void le3_iob_auto_gun_reaction_off(int p) { (void)p; }
void le3_iob_auto_gun_reaction_on(int p) { (void)p; }
int le3_iob_modify_gun_reaction(void *param) { (void)param; return 0; }
void le3_iob_modify_gun_reaction_init(void) {}
void le3_iob_put_ckl(uint8_t v) { (void)v; }
void le3_iob_put_ex_led(uint8_t v) { (void)v; }
void le3_iob_put_start_lamp(uint8_t v) { (void)v; }
void le3_iob_reset_now_gaming(void) {}
void le3_iob_set_now_gaming(void) {}
void le3_iob_set_game_continue_start(void) {}
int le3_iob_set_1p_bass_shaker(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { (void)a; (void)b; (void)c; (void)d; return 0; }
void le3_iob_set_1p_bass_shaker_init(void) {}
int le3_iob_set_2p_bass_shaker(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { (void)a; (void)b; (void)c; (void)d; return 0; }
void le3_iob_set_2p_bass_shaker_init(void) {}
int le3_iob_set_footled(uint8_t a, uint8_t b) { (void)a; (void)b; return 0; }
void le3_iob_set_footled_init(void) {}
int le3_iob_set_frled(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { (void)a; (void)b; (void)c; (void)d; return 0; }
void le3_iob_set_frled_init(void) {}
int le3_iob_set_gun_reaction(int v) { (void)v; return 0; }
void le3_iob_set_gun_reaction_init(int v) { (void)v; }
int le3_iob_set_light(uint8_t a, uint8_t b) { (void)a; (void)b; return 0; }
void le3_iob_set_light_init(void) {}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reden, LPVOID r)
{
    if (reden == DLL_PROCESS_ATTACH) { kb_dllmain_attach(h); kb_waakhond_start(); }
    else if (reden == DLL_PROCESS_DETACH) kb_dllmain_detach(r);
    return TRUE;
}

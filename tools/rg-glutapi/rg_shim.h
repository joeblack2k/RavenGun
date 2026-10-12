/* rg_shim.h -- afspraken tussen de gegenereerde delen en de handgeschreven delen
 * van librg-glutapi. Alles hierin is verborgen (hidden) of puur declaratief; er
 * komt geen enkel extra symbool in de exportlijst van de bibliotheek. Zie README.md.
 */
#ifndef RG_SHIM_H
#define RG_SHIM_H

/* Eén doorverwijzing: de naam zoals BudgieLoader hem exporteert en de wijzer die
 * bij het laden naar de echte libglut wordt gezet (rg_gen.c vult de tabel). */
typedef struct {
    const char *naam;
    void **slot;
} rg_slot_desc;

extern rg_slot_desc rg_slots[] __attribute__((visibility("hidden")));
extern const unsigned rg_nslots __attribute__((visibility("hidden")));

/* Wordt door de 122 noodstubs in rg_stubs.S aangeroepen als een aanroep op een
 * niet-opgeloste naam stuit. Probeert het één keer opnieuw (voor als de echte
 * libglut pas later in het proces kwam) en meldt het op stderr. */
void rg_herprobeer(unsigned idx) __attribute__((visibility("hidden")));

/* De drie callback-setters die wij zelf leveren en waar de bijbehorende
 * *Ucall-wikkel naar doorverwijst. */
void rg_set_appstatus(void (*cb)(int)) __attribute__((visibility("hidden")));
void rg_set_initcontext(void (*cb)(void)) __attribute__((visibility("hidden")));
void rg_set_position(void (*cb)(int, int)) __attribute__((visibility("hidden")));

#endif /* RG_SHIM_H */

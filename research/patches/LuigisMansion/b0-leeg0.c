/* B0a (bisectie Luigi's Mansion): de allerkleinste DLL -- geen entry point, geen CRT.
 *
 * Wordt alleen in het proces gemapt; er draait geen enkele instructie van ons.
 * Dat is de scherpste vraag: verdraagt de pakker het dat er een vreemde module in de
 * modulelijst van het proces staat? Zo nee, dan is elke in-process aanhechting te veel,
 * en is B0 (met DllMain) niet eens meer nodig.
 */
void rg_b0_niets(void) { }

/* Dwingt een .reloc af, zodat de DLL verplaatsbaar is en niet op zijn ImageBase hoeft te
 * passen. Zonder .reloc kan LoadLibraryW mislukken en dan meet je de lader, niet de game. */
void *const rg_b0_ankers0[2] = { (void *)&rg_b0_niets, (void *)&rg_b0_ankers0 };


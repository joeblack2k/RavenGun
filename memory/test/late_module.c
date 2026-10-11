/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * late_module.c — de bibliotheek die de proef PAS LATER laadt (Linux).
 *
 * Dit is het geval uit docs/community-model.md, paragraaf 3: een patch in een DLL die pas later
 * geladen wordt. Die bytes staan niet in de exe, dus een bestandspatch kan er niets mee. Alleen de
 * geheugen-motor kan hem doen.
 *
 * Dezelfde twee doelen als in probe_linux.c, zodat de proef dezelfde soort patch kan gebruiken.
 */
unsigned char ravengun_laat_doel[16] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a
};

__attribute__((noinline)) int ravengun_laat_functie(void)
{
    return 0;
}

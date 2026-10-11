/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * packed_module.c — een nagebootste GEPAKTE module, voor de proef.
 *
 * Dit is het derde geval uit docs/community-model.md paragraaf 3: een GEPAKTE exe (Pigs Might Fly,
 * Luigi's Mansion). Daar staat in het bestand niet de code die draait; de pakker zet de echte code
 * pas in het geheugen.
 *
 * Een echte pakker kunnen we hier niet gebruiken (die zit in de game), maar het wezenlijke verschil
 * is klein en wel na te bootsen: de bytes in het BESTAND zijn versleuteld, en een constructor zet ze
 * bij het laden om naar de echte bytes in het GEHEUGEN.
 *
 *   bestand:  f0 e1 96 87 b4 a5 5b 58 59 5e 5f 5c 5d 52 53 50   (versleuteld, XOR 0x5a)
 *   geheugen: aa bb cc dd ee ff 01 02 03 04 05 06 07 08 09 0a   (na het uitpakken)
 *
 * Een proef kan dan twee dingen laten zien:
 *   - een patch met `verwacht` = de GEHEUGEN-bytes lukt;
 *   - een patch met `verwacht` = de BESTANDS-bytes wordt geweigerd.
 * Dat is precies waarom de bestandspatch-motor hier niet kan en deze motor wel.
 */
#define XOR_SLEUTEL 0x5a

unsigned char ravengun_gepakt_doel[16] = {
    0xf0, 0xe1, 0x96, 0x87, 0xb4, 0xa5, 0x5b, 0x58,
    0x59, 0x5e, 0x5f, 0x5c, 0x5d, 0x52, 0x53, 0x50
};

__attribute__((noinline)) int ravengun_gepakt_functie(void)
{
    return 0;
}

/* De "pakker": bij het laden wordt het bestand in het geheugen uitgepakt. */
__attribute__((constructor))
static void ravengun_uitpakken(void)
{
    unsigned int i;
    for (i = 0; i < sizeof ravengun_gepakt_doel; i++) {
        ravengun_gepakt_doel[i] ^= XOR_SLEUTEL;
    }
}

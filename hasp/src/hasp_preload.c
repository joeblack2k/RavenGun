/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * De vooraf te laden bibliotheek van RavenGun (Linux): installeert de HASP-dongle op het
 * moment dat de bibliotheek in het spel geladen wordt, nog voordat het spel begint.
 *
 * Dit is het stuk dat de eigen emulator aan een spel koppelt. De emulator zelf staat in
 * dongle.c (de hasp_-functies); dit bestand doet alleen het laden en het installeren.
 *
 * Omgevingsvariabelen: zie dongle_install_from_env() in dongle.c.
 *
 * Bouwen (beide breedtes, want de games zijn 32-bits en 64-bits): scripts/build-hasp.sh
 *   gcc -shared -fPIC -O2 -o ravengun-hasp-x64.so hasp_preload.c dongle.c
 *   gcc -m32 -shared -fPIC -O2 -o ravengun-hasp.so     hasp_preload.c dongle.c
 */
#include "dongle.h"

__attribute__((constructor))
static void ravengun_hasp_init(void)
{
    (void)dongle_install_from_env();
}

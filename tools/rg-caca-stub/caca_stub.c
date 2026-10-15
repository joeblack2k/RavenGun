/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * caca_stub.c -- een eigen libcaca.so.0 (32-bits) voor de native route van The Walking Dead (Raw Thrills).
 *
 * De game linkt libcaca (ASCII-kunst in een terminal; op de kast hooguit een diagnosescherm) en importeert negen functies plus
 * twee gegevensobjecten via R_386_COPY (gemeten: patches/WalkingDead/EERSTE-PLT-SLOTS.md). Een echte i386-libcaca van Ubuntu
 * 14.04 sleept libslang2, libncursesw5 en libtinfo5 mee, die SteamOS niet heeft. Deze stub geeft "niet beschikbaar" terug:
 * __caca0_init() = -1, de rest 0 of NULL. Geen code van libcaca.
 */
void *__caca0_cv = 0;   /* caca_canvas_t * (compatibiliteitslaag 0.9) */
void *__caca0_dp = 0;   /* caca_display_t * */

int __caca0_init(void) { return -1; }
void __caca0_end(void) {}
void *__caca0_create_bitmap(void) { return 0; }
void __caca0_free_bitmap(void *b) { (void)b; }
unsigned int __caca0_get_event(void) { return 0; }
int caca_dither_bitmap(void) { return 0; }
int caca_refresh_display(void) { return 0; }
int caca_get_canvas_width(void) { return 0; }
int caca_get_canvas_height(void) { return 0; }

// SPDX-License-Identifier: GPL-3.0-or-later
// libsndio.so.7 als stub voor de Lindbergh-laag (RavenGun, eigen code).
//
// Waarom: libopenal.so.0 uit libs/ van lindbergh-loader is gelinkt tegen libsndio.so.7 (NEEDED; gemeten met objdump -p), en
// SteamOS heeft geen i386-sndio. Zonder dit bestand start Too Spicy (apacheM.elf) niet: "error while loading shared libraries:
// libsndio.so.7" (bench 2026-10-15). OpenAL gebruikt sndio alleen als het sio_open lukt; deze stub laat dat altijd mislukken,
// dus kiest OpenAL een volgende uitvoer (PulseAudio/ALSA). Alleen de acht functies die libopenal.so.0 importeert.
#include <stddef.h>

struct sio_hdl;
struct sio_par;

struct sio_hdl *sio_open(const char *name, unsigned int mode, int nbio) { (void)name; (void)mode; (void)nbio; return NULL; }
void sio_close(struct sio_hdl *h) { (void)h; }
void sio_initpar(struct sio_par *p) { (void)p; }
int sio_setpar(struct sio_hdl *h, struct sio_par *p) { (void)h; (void)p; return 0; }
int sio_getpar(struct sio_hdl *h, struct sio_par *p) { (void)h; (void)p; return 0; }
int sio_start(struct sio_hdl *h) { (void)h; return 0; }
int sio_stop(struct sio_hdl *h) { (void)h; return 0; }
size_t sio_write(struct sio_hdl *h, const void *b, size_t n) { (void)h; (void)b; (void)n; return 0; }

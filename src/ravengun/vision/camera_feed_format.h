/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Het camerabestand voor een spel (de "virtuele camera"): de daemon (ravengund, de enige eigenaar van de camera) schrijft het beeld
 * van zijn camera in een bestand in /dev/shm, en een bibliotheek in het spelproces leest het (onder Wine: tools/rg-mfcam, de
 * Media Foundation-camera voor Mario Kart Arcade GP DX). Dit kopbestand is de afspraak tussen die twee; het is gewone C, zodat de
 * Windows-DLL (MinGW) en de Linux-kant (C++) hetzelfde lezen.
 *
 * Indeling (little-endian, vaste plekken):
 *   0   magic    8 bytes "RGCAMv1\0"
 *   8   seq      u32: oneven terwijl de schrijver een beeld schrijft, even als het klaar is; +2 per beeld (een seqlock)
 *   12  width    u32 (pixels)
 *   16  height   u32
 *   20  stride   u32 (bytes per regel)
 *   24  fourcc   u32 ('YUY2' = 0x32595559; dezelfde bytes als V4L2 YUYV)
 *   28  fps_num  u32 (beelden per seconde = fps_num / fps_den; 0 = onbekend)
 *   32  fps_den  u32
 *   36  frames   u32: aantal geschreven beelden (0 = er is nog geen beeld)
 *   40  t_ms     u32: tijd van het laatste beeld (CLOCK_MONOTONIC in ms, afgekapt; alleen om "staat het beeld stil" te zien)
 *   44  reserved tot 64
 *   64  het beeld: height regels van stride bytes
 * Lezen: seq lezen (oneven = opnieuw), kop en beeld kopieren, seq opnieuw lezen; anders dan de eerste keer = opnieuw. */
#ifndef RAVENGUN_CAMERA_FEED_FORMAT_H
#define RAVENGUN_CAMERA_FEED_FORMAT_H

#include <stdint.h>

#define RG_CAMFEED_MAGIC "RGCAMv1"          /* met de afsluitende nul: 8 bytes */
#define RG_CAMFEED_HEADER 64u
#define RG_CAMFEED_FOURCC_YUY2 0x32595559u  /* 'Y' 'U' 'Y' '2' */
#define RG_CAMFEED_MAX_W 4096u
#define RG_CAMFEED_MAX_H 4096u
#define RG_CAMFEED_PATH_LINUX "/dev/shm/ravengun_Camera"
#define RG_CAMFEED_PATH_WINE L"Z:\\dev\\shm\\ravengun_Camera"

struct rg_camfeed_header {
    char magic[8];
    uint32_t seq;
    uint32_t width, height, stride, fourcc, fps_num, fps_den, frames, t_ms;
    uint8_t reserved[RG_CAMFEED_HEADER - 44];
};

/* Klopt de kop (magic, afmetingen binnen de grenzen, stride past bij YUY2)? 1 = ja. Zuiver. */
static inline int rg_camfeed_header_ok(const struct rg_camfeed_header* h) {
    const char* m = RG_CAMFEED_MAGIC;
    for (int i = 0; i < 8; ++i)
        if (h->magic[i] != m[i]) return 0;
    if (h->fourcc != RG_CAMFEED_FOURCC_YUY2) return 0;
    if (h->width == 0 || h->height == 0 || h->width > RG_CAMFEED_MAX_W || h->height > RG_CAMFEED_MAX_H) return 0;
    if ((h->width & 1u) != 0 || h->stride < h->width * 2u || h->stride > RG_CAMFEED_MAX_W * 4u) return 0;
    return 1;
}

/* Grootte van het beeld in bytes (stride x height); 0 bij een kop die niet klopt. */
static inline uint32_t rg_camfeed_frame_bytes(const struct rg_camfeed_header* h) {
    return rg_camfeed_header_ok(h) ? h->stride * h->height : 0u;
}

#endif

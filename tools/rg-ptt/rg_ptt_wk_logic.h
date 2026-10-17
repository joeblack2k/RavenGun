/* SPDX-License-Identifier: GPL-3.0-or-later */
/* De logica van onze WkWin32.dll voor Pull The Trigger, zonder Windows-aanroepen, zodat een test hem native kan draaien.
 *
 * De WibuKey-stub in PTW.exe versleutelt alleen de importtabel van het spel (280 bytes). Hij vraagt de doos die te
 * ontsleutelen met WkbCrypt2(hnd, 0x102, buffer, parameters, grootte, 0). Wij hebben geen doos, maar wel de platte tekst
 * (ptt_imports.h, afgeleid uit het spelbestand zelf door rg_ptt_imports.py).
 */
#ifndef RG_PTT_WK_LOGIC_H
#define RG_PTT_WK_LOGIC_H

#include "ptt_imports.h"

/* Uitkomsten van ptt_crypt(). */
enum {
    PTT_CRYPT_PLAIN = 0,   /* buffer was de bekende versleutelde importtabel; vervangen door de platte tekst */
    PTT_CRYPT_OTHER = 1,   /* andere grootte of andere inhoud (de 8-bytes controlevraag van de stub): ongemoeid, doos "antwoordt" */
    PTT_CRYPT_SAME = 2     /* buffer is al de platte tekst (tweede aanroep): ongemoeid */
};

static int ptt_equal(const unsigned char *a, const unsigned char *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int ptt_crypt(unsigned char *buf, unsigned size)
{
    unsigned i;
    if (!buf || size != PTT_BLOCK_SIZE) return PTT_CRYPT_OTHER;
    if (ptt_equal(buf, ptt_plain, size)) return PTT_CRYPT_SAME;
    if (!ptt_equal(buf, ptt_cipher, size)) return PTT_CRYPT_OTHER;
    for (i = 0; i < size; i++) buf[i] = ptt_plain[i];
    return PTT_CRYPT_PLAIN;
}

#endif

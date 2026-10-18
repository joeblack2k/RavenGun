/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * oss_probe -- 32-bit proefprogramma voor librg-oss-stub.so in afspeelmodus (RG_OSS_PLAY=1).
 * Doet wat Target: Terror Gold doet (jpsdrvr_init/jpsdrvr_proc): open /dev/dsp, RESET, GETFMTS, SETFRAGMENT 0x3000c,
 * SETFMT S16LE, CHANNELS 1, SPEED 22050, daarna per ronde GETOSPACE en bij een vrij fragment een fragment sinus
 * schrijven, anders even slapen. Meldt na afloop de geschreven bytes per seconde (moet ~ rate*2 zijn: de pacing
 * loopt dan op het afspeeltempo) en de GETOSPACE-antwoorden.
 * Header-vrij, zoals de shim (geen 32-bits glibc-headers nodig):
 *   gcc -m32 -O2 -o oss_probe oss_probe.c -lm
 * Gebruik: LD_PRELOAD=./librg-oss-stub.so RG_OSS_PLAY=1 ./oss_probe [seconden] [amplitude 0..32767] [toonHz]
 */
extern int open(const char *, int, ...);
extern int ioctl(int, unsigned long, ...);
extern long write(int, const void *, unsigned long);
extern int usleep(unsigned int);
extern int atoi(const char *);
extern int snprintf(char *, unsigned long, const char *, ...);
extern double sin(double);
extern long syscall(long, ...);

static unsigned long long nu(void)
{
    struct { long s, n; } t;
    syscall(265, 1, &t);
    return (unsigned long long)t.s * 1000000000ull + (unsigned long long)t.n;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 3, amp = argc > 2 ? atoi(argv[2]) : 0, hz = argc > 3 ? atoi(argv[3]) : 440;
    int fd = open("/dev/dsp", 1 /* O_WRONLY */), v, ospace[4], rondes = 0, vol = 0;
    unsigned long long t0, totbytes = 0;
    static short buf[4096 / 2];
    unsigned long ph = 0;
    char m[200];
    int n;
    if (fd < 0) { write(2, "open /dev/dsp mislukt\n", 22); return 1; }
    ioctl(fd, 0x5000, 0);
    ioctl(fd, 0x8004500b, &v);
    v = 0x3000c; ioctl(fd, 0xc004500a, &v);
    v = 0x10;    ioctl(fd, 0xc0045005, &v);
    v = 1;       ioctl(fd, 0xc0045006, &v);
    v = 22050;   ioctl(fd, 0xc0045002, &v);
    v = 0x6464;  ioctl(fd, 0xc0044d00, &v); /* mixer-ioctl op de dsp-fd: mag niets veranderen */
    n = snprintf(m, sizeof m, "ingesteld: fmt/ch/speed ok; snelheid=%d\n", v);
    (void)vol;
    write(1, m, (unsigned long)n);
    ioctl(fd, 0x8010500c, ospace);
    n = snprintf(m, sizeof m, "start GETOSPACE: frags=%d total=%d size=%d bytes=%d\n", ospace[0], ospace[1], ospace[2], ospace[3]);
    write(1, m, (unsigned long)n);
    t0 = nu();
    while (nu() - t0 < (unsigned long long)secs * 1000000000ull) {
        ioctl(fd, 0x8010500c, ospace);
        rondes++;
        if (ospace[0] > 0) {
            int i, fs = ospace[2] / 2;
            for (i = 0; i < fs; i++) {
                buf[i] = (short)(amp * sin(6.2831853 * hz * (double)(ph++) / 22050.0));
            }
            write(fd, buf, (unsigned long)ospace[2]);
            totbytes += (unsigned long long)ospace[2];
        } else {
            usleep(1000);
        }
    }
    {
        unsigned long long ms = (nu() - t0) / 1000000ull;
        n = snprintf(m, sizeof m, "geschreven: %llu bytes in %llu ms = %llu B/s (verwacht ~44100); rondes %d\n", totbytes, ms,
                     totbytes * 1000ull / ms, rondes);
        write(1, m, (unsigned long)n);
    }
    return 0;
}

#ifdef BREAK_GTOD
/* Zoals de exe van Target: Terror Gold: hij exporteert een eigen gettimeofday (via ftime) die de tv-velden vult maar geen 0
 * teruggeeft. Door symbool-interpositie ziet libpulse (pa_gettimeofday: assert(gettimeofday() == 0)) die in plaats van die van libc.
 * Bouwen met -DBREAK_GTOD -rdynamic. */
int gettimeofday(void *tv, void *tz)
{
    (void)tz;
    ((long *)tv)[0] = 1;
    ((long *)tv)[1] = 2000;
    return 12345;
}
#endif

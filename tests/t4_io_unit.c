/* Proefproces voor tests/t4_io_test.sh: legt een eigen geheugen onder de game-adressen van rg-t4-io (RG_T4_TEST_MEM), laadt de
 * bibliotheek, wacht argv[2] seconden en drukt de twee richtpunt-paren af. Geen spel. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    unsigned char *buf = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) return 3;
    float *f = (float *)buf;
    for (int i = 0; i < 16; i++) f[i] = 7.0f;   /* "niet door de hulp aangeraakt" */
    char b[32];
    snprintf(b, sizeof b, "%lx", (unsigned long)((uintptr_t)buf - 0x8b0da00u));
    setenv("RG_T4_TEST_MEM", b, 1);
    if (!dlopen(argv[1], RTLD_NOW)) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 4; }
    sleep((unsigned)atoi(argv[2]));
    /* geweer 0 op +0x0/+0x4, geweer 1 op +0x30/+0x34 */
    printf("AIM %.1f %.1f %.1f %.1f\n", (double)f[0], (double)f[1], (double)f[12], (double)f[13]);
    fflush(stdout);
    return 0;
}

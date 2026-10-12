/* proef_preload.c -- bewijs dat librg-glutapi.so via LD_PRELOAD in een echt
 * 32-bits proces terechtkomt: hij zoekt zichzelf op in zijn eigen /proc/self/maps. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

int main(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char regel[1024];
    int gevonden = 0;

    if (!f) {
        printf("FAIL: /proc/self/maps niet leesbaar\n");
        return 1;
    }
    while (fgets(regel, sizeof regel, f)) {
        if (strstr(regel, "librg-glutapi.so")) {
            printf("maps: %s", regel);
            gevonden++;
        }
    }
    fclose(f);
    printf("%s\n", gevonden
           ? "PASS: librg-glutapi.so zit in het adresbeeld van dit 32-bits proces"
           : "FAIL: librg-glutapi.so niet in /proc/self/maps");
    return gevonden ? 0 : 1;
}

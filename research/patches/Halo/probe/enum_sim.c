/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * enum_sim.c — doet precies wat libusb doet bij het opsommen van de apparaatlijst: de
 * sysfs-attribuutbestanden openen en lezen (busnum, devnum, idVendor, idProduct, ...) en van elk
 * apparaat het `descriptors`-bestand lezen. Geen libusb, geen bord: alleen het leespatroon.
 *
 * Gebruik: LD_PRELOAD=<tap.so> RG_USB_LOG=<log> RG_USB_SYSFS=<boom> ./enum_sim <boom>
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void lees(const char *pad, int met_bytes)
{
    char buf[256];
    int fd = open(pad, O_RDONLY);
    ssize_t n;
    if (fd < 0) {
        printf("  (kon %s niet openen)\n", pad);
        return;
    }
    n = read(fd, buf, sizeof buf - 1);
    if (n < 0)
        n = 0;
    buf[n] = 0;
    if (met_bytes)
        printf("  gelezen %s: %zd bytes\n", pad, n);
    else
        printf("  gelezen %s: %s", pad, buf);
    close(fd);
}

int main(int argc, char **argv)
{
    const char *attr[] = { "busnum", "devnum", "idVendor", "idProduct", "bDeviceClass", "speed" };
    const char *boom = argc > 1 ? argv[1] : "/sys/bus/usb/devices";
    DIR *d = opendir(boom);
    struct dirent *e;
    char pad[1024];
    unsigned i;

    if (!d) {
        perror("opendir");
        return 1;
    }
    printf("opsomming van %s (zoals libusb het doet)\n", boom);
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        printf("apparaat %s:\n", e->d_name);
        for (i = 0; i < sizeof attr / sizeof attr[0]; i++) {
            snprintf(pad, sizeof pad, "%s/%s/%s", boom, e->d_name, attr[i]);
            lees(pad, 0);
        }
        snprintf(pad, sizeof pad, "%s/%s/descriptors", boom, e->d_name);
        lees(pad, 1);
    }
    closedir(d);
    printf("klaar\n");
    return 0;
}

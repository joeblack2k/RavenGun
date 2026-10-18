// SPDX-License-Identifier: GPL-3.0-or-later
// rg-fake-pad: een virtuele gamepad via uinput, voor hoofdloze benchen.
//
// De daemon wacht bij lightgun-spellen op invoer: de Move van speler 1, een muis of een gamepad.
// Zonder die hardware blijft een automatische run staan op "Zet je PS Move aan (druk op de
// PS-knop)" en komt er dus nooit een spel in beeld. Dit hulpmiddel zet een pad neer dat de
// daemon als gamepad herkent (BTN_SOUTH/BTN_GAMEPAD, twee sticks en een kruis), houdt hem in
// leven tot SIGINT/SIGTERM en haalt hem dan weer weg. Alleen voor ontwikkelaars: het product
// gebruikt de echte Move.
//
// Bouwen: gcc -O2 -Wall -o /tmp/rg-fake-pad tools/dev/rg-fake-pad.c
// Gebruik: /tmp/rg-fake-pad &   (stoppen met kill)
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop = 0;

static void on_signal(int sig) {
    (void)sig;
    stop = 1;
}

static int zet(int fd, unsigned long welke, int bit) {
    if (ioctl(fd, welke, bit) < 0) {
        fprintf(stderr, "rg-fake-pad: ioctl 0x%lx %d: %s\n", welke, bit, strerror(errno));
        return -1;
    }
    return 0;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "rg-fake-pad: /dev/uinput: %s\n", strerror(errno));
        return 1;
    }
    // Knoppen die een gamepad voor de daemon herkenbaar maken.
    if (zet(fd, UI_SET_EVBIT, EV_KEY) < 0) return 1;
    const int knoppen[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
                           BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    for (size_t i = 0; i < sizeof knoppen / sizeof knoppen[0]; ++i) {
        if (zet(fd, UI_SET_KEYBIT, knoppen[i]) < 0) return 1;
    }
    if (zet(fd, UI_SET_EVBIT, EV_ABS) < 0) return 1;
    const int assen[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_HAT0X, ABS_HAT0Y};
    for (size_t i = 0; i < sizeof assen / sizeof assen[0]; ++i) {
        if (zet(fd, UI_SET_ABSBIT, assen[i]) < 0) return 1;
    }
    struct uinput_abs_setup as;
    const int stick[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY};
    for (size_t i = 0; i < sizeof stick / sizeof stick[0]; ++i) {
        memset(&as, 0, sizeof as);
        as.code = (unsigned short)stick[i];
        as.absinfo.minimum = -32768;
        as.absinfo.maximum = 32767;
        if (ioctl(fd, UI_ABS_SETUP, &as) < 0) {
            fprintf(stderr, "rg-fake-pad: UI_ABS_SETUP %d: %s\n", stick[i], strerror(errno));
            return 1;
        }
    }
    const int kruis[] = {ABS_HAT0X, ABS_HAT0Y};
    for (size_t i = 0; i < sizeof kruis / sizeof kruis[0]; ++i) {
        memset(&as, 0, sizeof as);
        as.code = (unsigned short)kruis[i];
        as.absinfo.minimum = -1;
        as.absinfo.maximum = 1;
        if (ioctl(fd, UI_ABS_SETUP, &as) < 0) return 1;
    }
    struct uinput_setup us;
    memset(&us, 0, sizeof us);
    snprintf(us.name, sizeof us.name, "RavenGun fake pad");
    us.id.bustype = BUS_USB;
    us.id.vendor = 0x045e;
    us.id.product = 0x028e;
    us.id.version = 1;
    if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "rg-fake-pad: aanmaken: %s\n", strerror(errno));
        return 1;
    }
    printf("rg-fake-pad: virtuele gamepad staat aan (stop met SIGTERM)\n");
    fflush(stdout);
    while (!stop) pause();
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    printf("rg-fake-pad: weg\n");
    return 0;
}

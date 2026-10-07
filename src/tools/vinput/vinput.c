/* vinput - virtueel evdev-invoerapparaat (uinput) voor diagnose van TeknoParrot.
 * Gebruik: vinput kbd|abs [--hold SEC]   (commando's via stdin, een per regel)
 * Commando's: key NAAM [down|up|tap], abs X Y, btn left|right|middle down|up|click,
 *             sleep MS, quit.  Zonder commando's op stdin blijft het apparaat bestaan
 *             tot EOF (gebruik --hold SEC om na EOF nog SEC seconden te wachten).
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define VID 0x1209  /* pid.codes testbereik */
#define PID_KBD 0x7201
#define PID_ABS 0x7202

static int fd;

static void emit(int type, int code, int val) {
  struct input_event e;
  memset(&e, 0, sizeof e);
  e.type = type; e.code = code; e.value = val;
  if (write(fd, &e, sizeof e) != sizeof e) perror("write");
}
static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); }

static const struct { const char *n; int c; } keys[] = {
  {"esc",KEY_ESC},{"enter",KEY_ENTER},{"space",KEY_SPACE},{"tab",KEY_TAB},{"backspace",KEY_BACKSPACE},
  {"up",KEY_UP},{"down",KEY_DOWN},{"left",KEY_LEFT},{"right",KEY_RIGHT},
  {"lshift",KEY_LEFTSHIFT},{"lctrl",KEY_LEFTCTRL},{"lalt",KEY_LEFTALT},
  {"f1",KEY_F1},{"f2",KEY_F2},{"f3",KEY_F3},{"f4",KEY_F4},{"f5",KEY_F5},{"f6",KEY_F6},
  {"f7",KEY_F7},{"f8",KEY_F8},{"f9",KEY_F9},{"f10",KEY_F10},{"f11",KEY_F11},{"f12",KEY_F12},
  {"0",KEY_0},{"1",KEY_1},{"2",KEY_2},{"3",KEY_3},{"4",KEY_4},{"5",KEY_5},{"6",KEY_6},
  {"7",KEY_7},{"8",KEY_8},{"9",KEY_9},
  {"a",KEY_A},{"b",KEY_B},{"c",KEY_C},{"d",KEY_D},{"e",KEY_E},{"f",KEY_F},{"g",KEY_G},
  {"h",KEY_H},{"i",KEY_I},{"j",KEY_J},{"k",KEY_K},{"l",KEY_L},{"m",KEY_M},{"n",KEY_N},
  {"o",KEY_O},{"p",KEY_P},{"q",KEY_Q},{"r",KEY_R},{"s",KEY_S},{"t",KEY_T},{"u",KEY_U},
  {"v",KEY_V},{"w",KEY_W},{"x",KEY_X},{"y",KEY_Y},{"z",KEY_Z},
};
#define NKEYS (int)(sizeof keys / sizeof keys[0])

static int keycode(const char *s) {
  char b[32]; size_t i;
  for (i = 0; s[i] && i < sizeof b - 1; i++) b[i] = tolower((unsigned char)s[i]);
  b[i] = 0;
  const char *p = strncmp(b, "key_", 4) ? b : b + 4;
  for (int k = 0; k < NKEYS; k++) if (!strcmp(p, keys[k].n)) return keys[k].c;
  return -1;
}

static int setup(int abs_mode) {
  fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0) { perror("open /dev/uinput"); return -1; }
  struct uinput_setup us;
  memset(&us, 0, sizeof us);
  us.id.bustype = BUS_USB; us.id.vendor = VID; us.id.version = 1;
  if (!abs_mode) {
    us.id.product = PID_KBD;
    strcpy(us.name, "RavenGun Virtual Keyboard");
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int k = 0; k < NKEYS; k++) ioctl(fd, UI_SET_KEYBIT, keys[k].c);
  } else {
    us.id.product = PID_ABS;
    strcpy(us.name, "RavenGun Virtual Lightgun");
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    /* Geen INPUT_PROP_DIRECT: dat markeert een touchscreen en laat libinput/kwin het als
     * touch behandelen. Zonder property + BTN_LEFT geldt het als absolute muis/tablet. */
    for (int a = ABS_X; a <= ABS_Y; a++) {
      struct uinput_abs_setup as;
      memset(&as, 0, sizeof as);
      as.code = a; as.absinfo.minimum = 0; as.absinfo.maximum = 65535;
      ioctl(fd, UI_ABS_SETUP, &as);
    }
  }
  if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) { perror("uinput create"); return -1; }
  usleep(500000); /* even wachten tot consumenten het apparaat zien */
  return 0;
}

static int btncode(const char *s) {
  if (!strcmp(s, "left")) return BTN_LEFT;
  if (!strcmp(s, "right")) return BTN_RIGHT;
  if (!strcmp(s, "middle")) return BTN_MIDDLE;
  return -1;
}

static void handle(char *line, int abs_mode) {
  char c1[64] = "", c2[64] = "", c3[64] = "";
  int n = sscanf(line, "%63s %63s %63s", c1, c2, c3);
  if (n < 1 || c1[0] == '#') return;
  if (!strcmp(c1, "key") && !abs_mode && n >= 2) {
    int k = keycode(c2);
    if (k < 0) { fprintf(stderr, "onbekende toets: %s\n", c2); return; }
    if (n == 2 || !strcmp(c3, "tap")) {
      emit(EV_KEY, k, 1); syn(); usleep(80000); emit(EV_KEY, k, 0); syn();
    } else { emit(EV_KEY, k, !strcmp(c3, "down")); syn(); }
  } else if (!strcmp(c1, "abs") && abs_mode) {
    int x, y;
    if (sscanf(line, "%*s %d %d", &x, &y) != 2) { fprintf(stderr, "abs X Y\n"); return; }
    emit(EV_ABS, ABS_X, x); emit(EV_ABS, ABS_Y, y); syn();
  } else if (!strcmp(c1, "btn") && abs_mode && n >= 2) {
    int b = btncode(c2);
    if (b < 0) { fprintf(stderr, "onbekende knop: %s\n", c2); return; }
    if (n == 2 || !strcmp(c3, "click")) {
      emit(EV_KEY, b, 1); syn(); usleep(80000); emit(EV_KEY, b, 0); syn();
    } else { emit(EV_KEY, b, !strcmp(c3, "down")); syn(); }
  } else if (!strcmp(c1, "sleep") && n >= 2) {
    usleep(atoi(c2) * 1000);
  } else if (!strcmp(c1, "quit")) {
    exit(0);
  } else fprintf(stderr, "onbekend commando in deze modus: %s", line);
  fflush(stderr);
}

int main(int argc, char **argv) {
  if (argc < 2 || (strcmp(argv[1], "kbd") && strcmp(argv[1], "abs"))) {
    fprintf(stderr, "gebruik: %s kbd|abs [--hold SEC] < commando's\n", argv[0]);
    return 2;
  }
  int abs_mode = !strcmp(argv[1], "abs");
  int hold = (argc >= 4 && !strcmp(argv[2], "--hold")) ? atoi(argv[3]) : 0;
  if (setup(abs_mode)) return 1;
  fprintf(stderr, "apparaat aangemaakt (%s)\n", abs_mode ? "lightgun" : "keyboard");
  char line[256];
  while (fgets(line, sizeof line, stdin)) handle(line, abs_mode);
  if (hold) sleep(hold);
  ioctl(fd, UI_DEV_DESTROY);
  close(fd);
  return 0;
}

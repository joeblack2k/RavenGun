/* Eenheidstest voor rg-ptylink (tools/rg-ptylink/rg_ptylink.c): draait met de bibliotheek in LD_PRELOAD en
 * RG_PTYLINK="<a>=<b>;<c>=<d>" (rg-ptylink_test.sh). Controleert: A is een tty, B ook te bevragen (tcgetattr), bytes gaan rauw
 * van A naar B en terug (ook \r, \n, 0x00 en 0xff, zonder echo), een tweede open van A is dezelfde verbinding, openat
 * komt ook aan, O_NONBLOCK geldt, het tweede koppel staat los van het eerste, en een gewoon pad gaat naar libc. Eigen code. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int fouten;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FOUT: " __VA_ARGS__); fputc('\n', stderr); fouten++; } } while (0)

static int lees_precies(int fd, unsigned char *b, int n) {
    int got = 0;
    while (got < n) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 1000) <= 0) break;
        ssize_t r = read(fd, b + got, (size_t)(n - got));
        if (r <= 0) break;
        got += (int)r;
    }
    return got;
}

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "gebruik: %s A B C D gewoon-bestand\n", argv[0]); return 2; }
    int a = open(argv[1], O_RDWR | O_NOCTTY);
    int b = open(argv[2], O_RDWR | O_NOCTTY);
    CHECK(a >= 0 && b >= 0, "open van het koppel mislukte (a=%d b=%d, errno %d)", a, b, errno);
    if (a < 0 || b < 0) return 1;
    CHECK(isatty(a), "kant A is geen tty");
    struct termios t;
    CHECK(tcgetattr(a, &t) == 0, "tcgetattr op A");
    CHECK(tcgetattr(b, &t) == 0, "tcgetattr op B");
    CHECK(cfsetispeed(&t, B115200) == 0 && tcsetattr(a, TCSANOW, &t) == 0, "baudrate zetten op A");
    const unsigned char heen[] = {'a', '\r', '\n', 0x00, 0xff, 0x7f, 0x03, 'z'};
    CHECK(write(a, heen, sizeof heen) == (ssize_t)sizeof heen, "schrijven op A");
    unsigned char in[32] = {0};
    CHECK(lees_precies(b, in, sizeof heen) == (int)sizeof heen && !memcmp(in, heen, sizeof heen), "A -> B niet rauw aangekomen");
    const unsigned char terug[] = {0x55, '\n', 0x00, '\r', 0x11, 0x13};
    CHECK(write(b, terug, sizeof terug) == (ssize_t)sizeof terug, "schrijven op B");
    memset(in, 0, sizeof in);
    CHECK(lees_precies(a, in, sizeof terug) == (int)sizeof terug && !memcmp(in, terug, sizeof terug), "B -> A niet rauw aangekomen");
    struct pollfd p = {b, POLLIN, 0};
    CHECK(poll(&p, 1, 100) == 0, "echo: B kreeg zijn eigen bytes terug");
    /* een tweede open van A is dezelfde verbinding */
    int a2 = openat(AT_FDCWD, argv[1], O_RDWR | O_NONBLOCK);
    CHECK(a2 >= 0 && a2 != a, "openat van A");
    CHECK((fcntl(a2, F_GETFL) & O_NONBLOCK) != 0, "O_NONBLOCK niet gezet");
    CHECK(write(b, "Q", 1) == 1 && lees_precies(a2, in, 1) == 1 && in[0] == 'Q', "tweede open van A hoort bij hetzelfde koppel");
    /* het tweede koppel staat los */
    int c = open(argv[3], O_RDWR), d = open(argv[4], O_RDWR);
    CHECK(c >= 0 && d >= 0, "tweede koppel");
    CHECK(write(c, "K", 1) == 1 && lees_precies(d, in, 1) == 1 && in[0] == 'K', "tweede koppel C -> D");
    p.fd = b;
    CHECK(poll(&p, 1, 100) == 0, "het tweede koppel lekt naar het eerste");
    /* een gewoon pad gaat naar libc */
    int g = open(argv[5], O_RDONLY);
    CHECK(g >= 0 && !isatty(g), "gewoon bestand");
    CHECK(open("/bestaat/niet/echt", O_RDONLY) < 0 && errno == ENOENT, "een onbekend pad geeft geen ENOENT");
    if (fouten) return 1;
    printf("ptylink: tty, rauw heen en terug, geen echo, tweede open, openat, O_NONBLOCK, twee koppels, gewone paden in orde\n");
    return 0;
}

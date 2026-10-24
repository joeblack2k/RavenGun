/* glut-log.c - 32-bits LD_PRELOAD die de GLUT-aanroepen van de gast vastlegt.
 *
 * DOEL: antwoord geven op EEN vraag -- maakt de gast een venster, waarmee, en wat gebeurt er?
 *   * NIET aangeroepen -> de gast neemt een tak waarin hij geen venster maakt (zelfde vorm als het kind
 *                         dat onvoorwaardelijk exit(0) doet); dan is er geen displayprobleem.
 *   * WEL aangeroepen  -> de RETOURWAARDE en errno op dat moment zijn het antwoord.
 *
 * HET LAADBEWIJS: deze wikkel schrijft bij het LADEN een regel via een constructor. Zonder dat betekent
 * "geen logbestand" twee dingen tegelijk: niet geladen, of nooit aangeroepen. Het instrument moet eerst
 * bewijzen dat het leeft voordat ik zijn stilte geloof.
 *
 * DE GAME-MODE-API: de gast importeert `glutEnterGameMode` en `glutGameModeString`, de VOLLEDIG-SCHERM-
 * route van freeglut. Die loopt NIET via glutCreateWindow, dus zonder deze wikkels zou een correct
 * werkende proef niets vastleggen terwijl de gast juist een venster maakt.
 *
 * ROOKPROEF: `glutSwapBuffers` -- wordt die aangeroepen, dan presenteert de gast frames.
 *
 * Logboek: $RAVENGUN_GLUT_LOG, anders /tmp/ravengun-glut.log
 * Bouwen: gcc -m32 -shared -fPIC -O1 -o libravengun-glut-log.so glut-log.c -ldl
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/syscall.h>

static double t0;
static int t0_gezet;

static double nu(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* rauwe syscalls, zodat deze logger niet door de bestandslogger gezien wordt */
static void meld(const char *fmt, ...)
{
    char buf[1024], kern[768];
    va_list ap;
    int fd, pre, n;
    const char *pad = getenv("RAVENGUN_GLUT_LOG");
    if (!pad || !*pad) pad = "/tmp/ravengun-glut.log";
    if (!t0_gezet) { t0 = nu(); t0_gezet = 1; }
    va_start(ap, fmt);
    vsnprintf(kern, sizeof kern, fmt, ap);
    va_end(ap);
    pre = snprintf(buf, sizeof buf, "+%9.6f pid=%-6d ", nu() - t0, (int)getpid());
    n = pre + snprintf(buf + pre, sizeof buf - pre, "%s\n", kern);
    if (n <= 0 || n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    fd = (int)syscall(SYS_open, pad, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        ssize_t r = syscall(SYS_write, fd, buf, (size_t)n);
        (void)r;
        syscall(SYS_close, fd);
    }
}

/* ============================ HET LAADBEWIJS ============================ */
__attribute__((constructor))
static void wikkel_geladen(void)
{
    meld("WIKKEL GELADEN (libravengun-glut-log actief in pid %d)", (int)getpid());
}

/* ============================ de klassieke aanroepen ============================ */

void glutInit(int *argc, char **argv)
{
    static void (*echt)(int *, char **);
    if (!echt) echt = (void (*)(int *, char **))dlsym(RTLD_NEXT, "glutInit");
    meld("glutInit argc=%d", argc ? *argc : -1);
    if (echt) echt(argc, argv);
    else meld("glutInit ECHTE FUNCTIE NIET GEVONDEN");
}

void glutInitDisplayMode(unsigned int mode)
{
    static void (*echt)(unsigned int);
    if (!echt) echt = (void (*)(unsigned int))dlsym(RTLD_NEXT, "glutInitDisplayMode");
    meld("glutInitDisplayMode mode=0x%x%s%s%s%s", mode,
         (mode & 0x1) ? " RGB" : "", (mode & 0x2) ? " DOUBLE" : "",
         (mode & 0x10) ? " DEPTH" : "", (mode & 0x8) ? " ALPHA" : "");
    if (echt) echt(mode);
}

int glutCreateWindow(const char *titel)
{
    static int (*echt)(const char *);
    int r = -1;
    if (!echt) echt = (int (*)(const char *))dlsym(RTLD_NEXT, "glutCreateWindow");
    if (!echt) { meld("glutCreateWindow ECHTE FUNCTIE NIET GEVONDEN"); return -1; }
    errno = 0;
    r = echt(titel);
    meld("glutCreateWindow titel=\"%s\" -> %d (0x%x) errno=%d (%s)",
         titel ? titel : "(null)", r, r, errno, errno ? strerror(errno) : "geen");
    return r;
}

int glutCreateSubWindow(int win, int x, int y, int b, int h)
{
    static int (*echt)(int, int, int, int, int);
    int r;
    if (!echt) echt = (int (*)(int, int, int, int, int))dlsym(RTLD_NEXT, "glutCreateSubWindow");
    if (!echt) return -1;
    errno = 0; r = echt(win, x, y, b, h);
    meld("glutCreateSubWindow ouder=%d %dx%d+%d+%d -> %d errno=%d", win, b, h, x, y, r, errno);
    return r;
}

void glutMainLoop(void)
{
    static void (*echt)(void);
    if (!echt) echt = (void (*)(void))dlsym(RTLD_NEXT, "glutMainLoop");
    meld("glutMainLoop BINNENGEKOMEN (vanaf hier rendert de GLUT-lus)");
    if (echt) { echt(); meld("glutMainLoop TERUG (zou niet moeten gebeuren)"); }
}

void glutSwapBuffers(void)
{
    static void (*echt)(void);
    static long n;
    if (!echt) echt = (void (*)(void))dlsym(RTLD_NEXT, "glutSwapBuffers");
    n++;
    if (n <= 5 || (n % 100) == 0) meld("glutSwapBuffers nummer %ld", n);
    if (echt) echt();
}

int glutGet(unsigned int wat)
{
    static int (*echt)(unsigned int);
    int r;
    if (!echt) echt = (int (*)(unsigned int))dlsym(RTLD_NEXT, "glutGet");
    if (!echt) return 0;
    r = echt(wat);
    if (wat == 0x7d0 || wat == 0x7d1 || wat == 0x7dd || wat == 0x7de)
        meld("glutGet(0x%x) -> %d", wat, r);
    return r;
}

void glutSetWindow(int win)
{
    static void (*echt)(int);
    if (!echt) echt = (void (*)(int))dlsym(RTLD_NEXT, "glutSetWindow");
    meld("glutSetWindow %d", win);
    if (echt) echt(win);
}

void glutDestroyWindow(int win)
{
    static void (*echt)(int);
    if (!echt) echt = (void (*)(int))dlsym(RTLD_NEXT, "glutDestroyWindow");
    meld("glutDestroyWindow %d", win);
    if (echt) echt(win);
}

/* ============================ DE GAME-MODE-API ============================ */

void glutGameModeString(const char *string)
{
    static void (*echt)(const char *);
    if (!echt) echt = (void (*)(const char *))dlsym(RTLD_NEXT, "glutGameModeString");
    meld("glutGameModeString \"%s\"   <- de GEWENSTE resolutie/kleurdiepte", string ? string : "(null)");
    if (echt) echt(string);
}

int glutEnterGameMode(void)
{
    static int (*echt)(void);
    int r = -1;
    if (!echt) echt = (int (*)(void))dlsym(RTLD_NEXT, "glutEnterGameMode");
    if (!echt) { meld("glutEnterGameMode ECHTE FUNCTIE NIET GEVONDEN"); return -1; }
    errno = 0;
    r = echt();
    meld("glutEnterGameMode -> %d (0x%x) errno=%d (%s)   <- DE VOLLEDIG-SCHERM-VENSTERCREATIE",
         r, r, errno, errno ? strerror(errno) : "geen");
    return r;
}

void glutLeaveGameMode(void)
{
    static void (*echt)(void);
    if (!echt) echt = (void (*)(void))dlsym(RTLD_NEXT, "glutLeaveGameMode");
    meld("glutLeaveGameMode");
    if (echt) echt();
}

void glutInitWindowSize(int b, int h)
{
    static void (*echt)(int, int);
    if (!echt) echt = (void (*)(int, int))dlsym(RTLD_NEXT, "glutInitWindowSize");
    meld("glutInitWindowSize %dx%d", b, h);
    if (echt) echt(b, h);
}

void glutInitWindowPosition(int x, int y)
{
    static void (*echt)(int, int);
    if (!echt) echt = (void (*)(int, int))dlsym(RTLD_NEXT, "glutInitWindowPosition");
    meld("glutInitWindowPosition +%d+%d", x, y);
    if (echt) echt(x, y);
}

void glutDisplayFunc(void (*func)(void))
{
    static void (*echt)(void (*)(void));
    if (!echt) echt = (void (*)(void (*)(void)))dlsym(RTLD_NEXT, "glutDisplayFunc");
    meld("glutDisplayFunc func=%p   <- hier registreert hij zijn tekenfunctie", (void *)func);
    if (echt) echt(func);
}

void glutReshapeFunc(void (*func)(int, int))
{
    static void (*echt)(void (*)(int, int));
    if (!echt) echt = (void (*)(void (*)(int, int)))dlsym(RTLD_NEXT, "glutReshapeFunc");
    meld("glutReshapeFunc func=%p", (void *)func);
    if (echt) echt(func);
}

void glutPostRedisplay(void)
{
    static void (*echt)(void);
    static long n;
    if (!echt) echt = (void (*)(void))dlsym(RTLD_NEXT, "glutPostRedisplay");
    n++;
    if (n <= 5 || (n % 200) == 0) meld("glutPostRedisplay nummer %ld", n);
    if (echt) echt();
}

void glutSetCursor(int cursor)
{
    static void (*echt)(int);
    if (!echt) echt = (void (*)(int))dlsym(RTLD_NEXT, "glutSetCursor");
    meld("glutSetCursor %d", cursor);
    if (echt) echt(cursor);
}

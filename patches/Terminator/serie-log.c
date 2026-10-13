/* serie-log.c - 32-bits LD_PRELOAD die het SERIËLE verkeer van de gast vastlegt.
 *
 * WAAROM: de gast zoekt een Mitsubishi-projector op een seriële poort en `/dev/ttyS0` geeft EPERM.
 * De vraag is niet "is de poort geblokkeerd" -- dat weten we -- maar **wat zegt de gast tegen een
 * stille seriële lijn, en waar wacht hij op**. Als hij een Mitsubishi-sonde stuurt en op een bepaald
 * antwoord wacht, dan is een synthetisch ANTWOORD een begrensd ding om te maken.
 *
 * HET LAADBEWIJS (verplicht, van de GLUT-ronde): de constructor schrijft bij het LADEN een regel.
 * Zonder dat betekent een leeg logboek "niet geladen" of "nooit aangeroepen", en dat konden we vorige
 * keer niet scheiden.
 *
 * VOORAF VASTGELEGD WAT IK VERWACHT:
 *   * met een werkende knoop op /dev/ttyS0 -> `open` SLAAGT, en dan zie ik een van twee dingen:
 *     (a) de gast schrijft bytes (een sonde) en doet daarna een `read` die 0 of EAGAIN teruggeeft
 *         -> dan is de volgende stap een synthetisch ANTWOORD, en dat is begrensd werk;
 *     (b) de gast opent en configureert alleen (ioctl/termios) en schrijft niets
 *         -> dan zoekt hij alleen aanwezigheid en helpt een antwoord niet.
 *   * zonder een werkende knoop -> `open` faalt met EPERM en er valt niets te meten; dan is er ook
 *     niets geschreven en is dat de verklaring, niet een leeg logboek.
 *   * en de TERUGVAL-uitkomst: staat er WEL een sonde maar komt er geen antwoord, dan is dat niet
 *     "de poort is stuk" maar "de kast verwacht antwoord van een projector die er niet is".
 *
 * Logboek: $RAVENGUN_SERIE_LOG, anders /tmp/ravengun-serie.log
 * Bouwen: gcc -m32 -shared -fPIC -O1 -o libravengun-serie-log.so serie-log.c -ldl
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

static double t0; static int t0_gezet;
static int tty_fd[64]; static int tty_n;

static double nu(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec/1e9; }

static void meld(const char *fmt, ...)
{
    char buf[1024], kern[768]; va_list ap; int fd, pre, n;
    const char *pad = getenv("RAVENGUN_SERIE_LOG");
    if (!pad || !*pad) pad = "/tmp/ravengun-serie.log";
    if (!t0_gezet){ t0=nu(); t0_gezet=1; }
    va_start(ap,fmt); vsnprintf(kern,sizeof kern,fmt,ap); va_end(ap);
    pre = snprintf(buf,sizeof buf,"+%9.6f pid=%-6d ",nu()-t0,(int)getpid());
    n = pre + snprintf(buf+pre,sizeof buf-pre,"%s\n",kern);
    if (n<=0 || n>=(int)sizeof buf) n=(int)sizeof buf-1;
    fd = (int)syscall(SYS_open,pad,O_WRONLY|O_CREAT|O_APPEND,0644);
    if (fd>=0){ ssize_t r=syscall(SYS_write,fd,buf,(size_t)n); (void)r; syscall(SYS_close,fd); }
}

static int is_serie_pad(const char *p)
{
    if (!p) return 0;
    return (strncmp(p,"/dev/tty",8)==0) || (strncmp(p,"/dev/sierra",11)==0) ||
           (strncmp(p,"/dev/ttyUSB",11)==0) || (strncmp(p,"/dev/ttyACM",11)==0);
}
static void onthoud(int fd){ if (fd>=0 && tty_n<64) tty_fd[tty_n++]=fd; }
static int is_tty_fd(int fd){ int i; for(i=0;i<tty_n;i++) if(tty_fd[i]==fd) return 1;
    if (fd>=0 && isatty(fd)) return 1; return 0; }

/* ============================ HET LAADBEWIJS ============================ */
__attribute__((constructor))
static void wikkel_geladen(void)
{
    meld("WIKKEL GELADEN (libravengun-serie-log actief in pid %d)", (int)getpid());
}

/* ============================ de aanroepen ============================ */

int open(const char *pad, int flags, ...)
{
    static int (*echt)(const char*,int,...);
    mode_t mode=0; int r;
    if (flags & O_CREAT){ va_list ap; va_start(ap,flags); mode=(mode_t)va_arg(ap,int); va_end(ap); }
    if (!echt) echt=(int(*)(const char*,int,...))dlsym(RTLD_NEXT,"open");
    if (!echt){ errno=ENOSYS; return -1; }
    errno=0; r=echt(pad,flags,mode);
    if (is_serie_pad(pad)) {
        int e=errno;
        if (r>=0){ meld("SERIE open \"%s\" flags=0x%x -> fd=%d",pad,flags,r); onthoud(r); }
        else meld("SERIE open \"%s\" flags=0x%x -> FOUT errno=%d (%s)",pad,flags,e,strerror(e));
        errno=e;
    }
    return r;
}

int open64(const char *pad, int flags, ...)
{
    static int (*echt)(const char*,int,...);
    mode_t mode=0; int r;
    if (flags & O_CREAT){ va_list ap; va_start(ap,flags); mode=(mode_t)va_arg(ap,int); va_end(ap); }
    if (!echt) echt=(int(*)(const char*,int,...))dlsym(RTLD_NEXT,"open64");
    if (!echt){ errno=ENOSYS; return -1; }
    errno=0; r=echt(pad,flags,mode);
    if (is_serie_pad(pad)) { int e=errno;
        if (r>=0){ meld("SERIE open64 \"%s\" -> fd=%d",pad,r); onthoud(r); }
        else meld("SERIE open64 \"%s\" -> FOUT errno=%d",pad,e);
        errno=e; }
    return r;
}

int ioctl(int fd, unsigned long req, ...)
{
    static int (*echt)(int,unsigned long,...);
    void *arg; int r; va_list ap;
    if (!echt) echt=(int(*)(int,unsigned long,...))dlsym(RTLD_NEXT,"ioctl");
    if (!echt){ errno=ENOSYS; return -1; }
    va_start(ap,req); arg=va_arg(ap,void*); va_end(ap);
    errno=0; r=echt(fd,req,arg);
    if (is_tty_fd(fd)) { int e=errno;
        meld("SERIE ioctl fd=%d req=0x%lx -> %d%s%s",fd,req,r,
             r<0?" errno=":"",r<0?strerror(e):"");
        errno=e; }
    return r;
}

ssize_t write(int fd, const void *buf, size_t n)
{
    static ssize_t (*echt)(int,const void*,size_t);
    ssize_t r; int i, p; char hex[256]; char zicht[80];
    if (!echt) echt=(ssize_t(*)(int,const void*,size_t))dlsym(RTLD_NEXT,"write");
    if (!echt){ errno=ENOSYS; return -1; }
    errno=0; r=echt(fd,buf,n);
    if (is_tty_fd(fd)) {
        int e=errno;
        p=0; hex[0]=0;
        for (i=0;i<(int)n && i<32 && p<(int)sizeof hex-4;i++)
            p+=snprintf(hex+p,sizeof hex-p,"%02x ",((const unsigned char*)buf)[i]);
        p=0; zicht[0]=0;
        for (i=0;i<(int)n && i<24 && p<(int)sizeof zicht-2;i++){
            unsigned char c=((const unsigned char*)buf)[i];
            zicht[p++] = (c>=32 && c<127) ? (char)c : '.';
        }
        zicht[p]=0;
        meld("SERIE write fd=%d %ld bytes -> %s |%s|",fd,(long)n,hex,zicht);
        errno=e;
    }
    return r;
}

ssize_t read(int fd, void *buf, size_t n)
{
    static ssize_t (*echt)(int,void*,size_t);
    ssize_t r; int i, p; char hex[256];
    if (!echt) echt=(ssize_t(*)(int,void*,size_t))dlsym(RTLD_NEXT,"read");
    if (!echt){ errno=ENOSYS; return -1; }
    errno=0; r=echt(fd,buf,n);
    if (is_tty_fd(fd)) {
        int e=errno;
        if (r>0){ p=0; hex[0]=0;
            for (i=0;i<(int)r && i<32 && p<(int)sizeof hex-4;i++)
                p+=snprintf(hex+p,sizeof hex-p,"%02x ",((const unsigned char*)buf)[i]);
            meld("SERIE read fd=%d -> %ld bytes: %s",fd,(long)r,hex);
        } else if (r==0) meld("SERIE read fd=%d -> 0 (einde/geen antwoord)",fd);
        else meld("SERIE read fd=%d -> FOUT errno=%d (%s)",fd,e,strerror(e));
        errno=e;
    }
    return r;
}

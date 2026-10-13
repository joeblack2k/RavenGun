
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
static struct timespec T0; static int t0s; static char lp[256];
static int sys3(int nr, long a, long b, long c){ long r; __asm__ volatile("int $0x80":"=a"(r):"0"(nr),"b"(a),"c"(b),"d"(c):"memory"); return (int)r; }
static void L(const char *fmt, ...) __attribute__((format(printf,1,2)));
static void L(const char *fmt, ...){
  char b[600]; va_list ap; struct timespec t; int n, f;
  if (!t0s) { clock_gettime(CLOCK_MONOTONIC,&T0); t0s=1; const char *p=getenv("RG_EXIT_LOG"); snprintf(lp,sizeof lp,"%s",p?p:"/tmp/rg-exit.log"); }
  clock_gettime(CLOCK_MONOTONIC,&t);
  n = snprintf(b,sizeof b,"[exitlog] + %.6f pid %d ", (t.tv_sec-T0.tv_sec)+(t.tv_nsec-T0.tv_nsec)/1e9, getpid());
  va_start(ap,fmt); n += vsnprintf(b+n,sizeof b-n,fmt,ap); va_end(ap);
  f = sys3(5,(long)lp,0x441,0644); if (f>=0){ sys3(4,f,(long)b,n); sys3(6,f,0,0);} }
static void *real(const char *n){ return dlsym(RTLD_NEXT,n); }
typedef int (*f6)(int,int,int,int,int,int);


int glutInitDisplayMode(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutInitDisplayMode");
  L("CALL glutInitDisplayMode(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutInitDisplayMode -> 0x%x\n",rv);
  return rv; }

int glutInitWindowSize(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutInitWindowSize");
  L("CALL glutInitWindowSize(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutInitWindowSize -> 0x%x\n",rv);
  return rv; }

int glutInitWindowPosition(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutInitWindowPosition");
  L("CALL glutInitWindowPosition(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutInitWindowPosition -> 0x%x\n",rv);
  return rv; }

int glutCreateWindow(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutCreateWindow");
  L("CALL glutCreateWindow(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutCreateWindow -> 0x%x\n",rv);
  return rv; }

int glutGameModeString(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutGameModeString");
  L("CALL glutGameModeString(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutGameModeString -> 0x%x\n",rv);
  return rv; }

int glutEnterGameMode(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutEnterGameMode");
  L("CALL glutEnterGameMode(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutEnterGameMode -> 0x%x\n",rv);
  return rv; }

int glutDisplayFunc(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutDisplayFunc");
  L("CALL glutDisplayFunc(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutDisplayFunc -> 0x%x\n",rv);
  return rv; }

int glutReshapeFunc(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutReshapeFunc");
  L("CALL glutReshapeFunc(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutReshapeFunc -> 0x%x\n",rv);
  return rv; }

int glutSetCursor(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutSetCursor");
  L("CALL glutSetCursor(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutSetCursor -> 0x%x\n",rv);
  return rv; }

int glutSetWindow(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glutSetWindow");
  L("CALL glutSetWindow(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glutSetWindow -> 0x%x\n",rv);
  return rv; }

int glXQueryVersion(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glXQueryVersion");
  L("CALL glXQueryVersion(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glXQueryVersion -> 0x%x\n",rv);
  return rv; }

int glXGetClientString(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glXGetClientString");
  L("CALL glXGetClientString(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glXGetClientString -> 0x%x\n",rv);
  return rv; }

int glXGetProcAddressARB(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glXGetProcAddressARB");
  L("CALL glXGetProcAddressARB(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glXGetProcAddressARB -> 0x%x\n",rv);
  return rv; }

int glXSwapIntervalSGI(int a,int b,int c,int d,int e,int f){
  static f6 r; int rv; if(!r) r=(f6)real("glXSwapIntervalSGI");
  L("CALL glXSwapIntervalSGI(0x%x,0x%x,0x%x) from %p\n",a,b,c,__builtin_return_address(0));
  rv = r ? r(a,b,c,d,e,f) : 0;
  L("RET  glXSwapIntervalSGI -> 0x%x\n",rv);
  return rv; }




void exit(int code){ static void (*r)(int); if(!r) r=(void(*)(int))real("exit");
  L("CALL exit(%d) from %p\n",code,__builtin_return_address(0)); r(code); __builtin_unreachable(); }
void _exit(int code){ static void (*r)(int); if(!r) r=(void(*)(int))real("_exit");
  L("CALL _exit(%d) from %p\n",code,__builtin_return_address(0)); r(code); __builtin_unreachable(); }
void glutMainLoop(void){ static void (*r)(void); if(!r) r=(void(*)(void))real("glutMainLoop");
  L("CALL glutMainLoop() from %p\n",__builtin_return_address(0)); r(); L("RET  glutMainLoop\n"); }
void glutSwapBuffers(void){ static void (*r)(void); static int n; if(!r) r=(void(*)(void))real("glutSwapBuffers");
  if(++n<=3) L("CALL glutSwapBuffers() #%d\n",n); r(); }

int system(const char *c){ static int (*r)(const char*); if(!r) r=(int(*)(const char*))real("system");
  L("CALL system(\"%.200s\") from %p\n",c?c:"(null)",__builtin_return_address(0)); return r(c); }
int kill(pid_t p,int sig){ static int (*r)(pid_t,int); if(!r) r=(int(*)(pid_t,int))real("kill");
  L("CALL kill(%d,%d) from %p\n",(int)p,sig,__builtin_return_address(0)); return r(p,sig); }
void abort(void){ static void (*r)(void); if(!r) r=(void(*)(void))real("abort");
  L("CALL abort() from %p\n",__builtin_return_address(0)); r(); __builtin_unreachable(); }

#include <sys/select.h>
#include <poll.h>
int usleep(unsigned int us){ static int (*r)(unsigned); if(!r) r=(int(*)(unsigned))real("usleep");
  if (us >= 50000) L("CALL usleep(%u) from %p\n",us,__builtin_return_address(0)); return r(us); }
unsigned int sleep(unsigned int s){ static unsigned (*r)(unsigned); if(!r) r=(unsigned(*)(unsigned))real("sleep");
  L("CALL sleep(%u) from %p\n",s,__builtin_return_address(0)); return r(s); }
int nanosleep(const struct timespec *a, struct timespec *b){ static int (*r)(const struct timespec*,struct timespec*); if(!r) r=(int(*)(const struct timespec*,struct timespec*))real("nanosleep");
  if (a && (a->tv_sec > 0 || a->tv_nsec >= 50000000)) L("CALL nanosleep(%ld.%09ld) from %p\n",(long)a->tv_sec,(long)a->tv_nsec,__builtin_return_address(0)); return r(a,b); }
int select(int n, fd_set *a, fd_set *b, fd_set *c, struct timeval *t){ static int (*r)(int,fd_set*,fd_set*,fd_set*,struct timeval*); if(!r) r=(int(*)(int,fd_set*,fd_set*,fd_set*,struct timeval*))real("select");
  if (t && (t->tv_sec > 0 || t->tv_usec >= 50000)) L("CALL select(n=%d, timeout=%ld.%06ld) from %p\n",n,(long)t->tv_sec,(long)t->tv_usec,__builtin_return_address(0)); return r(n,a,b,c,t); }
int poll(struct pollfd *f, nfds_t n, int ms){ static int (*r)(struct pollfd*,nfds_t,int); if(!r) r=(int(*)(struct pollfd*,nfds_t,int))real("poll");
  if (ms >= 50 || ms < 0) L("CALL poll(n=%u, timeout=%d ms, fd0=%d) from %p\n",(unsigned)n,ms,n?f[0].fd:-1,__builtin_return_address(0)); return r(f,n,ms); }

/* Omgeving-aanpassing: de game geeft glutInit een VASTE argv ["bbh3context","-display",":0.0"] (zie 0x80c4720).
   Dat is het scherm van een kast. Hier wordt de waarde achter "-display" door $DISPLAY vervangen
   (alleen als RG_GLUT_DISPLAY_FROM_ENV gezet is); verder verandert er niets. */
void glutInit(int *argcp, char **argv){ static void (*r)(int*,char**); if(!r) r=(void(*)(int*,char**))real("glutInit");
  int i; const char *d = getenv("DISPLAY");
  L("CALL glutInit(argc=%d) from %p\n", argcp?*argcp:-1, __builtin_return_address(0));
  if (argcp && argv && getenv("RG_GLUT_DISPLAY_FROM_ENV") && d && *d)
    for (i = 0; i + 1 < *argcp; i++)
      if (argv[i] && !strcmp(argv[i], "-display")) { L("  argv[%d]: -display %s -> %s (DISPLAY)\n", i+1, argv[i+1], d); argv[i+1] = (char *)d; }
  r(argcp, argv); L("RET  glutInit\n"); }

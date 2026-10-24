#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/syscall.h>
static struct timespec T0; static int t0set; static char lp[256];
static int sys3(int nr, long a, long b, long c){ long r; __asm__ volatile("int $0x80":"=a"(r):"0"(nr),"b"(a),"c"(b),"d"(c):"memory"); return (int)r; }
int ioctl(int fd, unsigned long req, ...){
  va_list ap; void *arg; int r, e; char path[128] = "?", lk[64], line[400]; struct timespec t; int n;
  va_start(ap, req); arg = va_arg(ap, void*); va_end(ap);
  r = syscall(SYS_ioctl, fd, req, arg); e = errno;
  if (r < 0 && e == ENOTTY && getenv("RG_FAKE_DSP")) {
    char pp[64], l2[64]; ssize_t k2; snprintf(l2, sizeof l2, "/proc/self/fd/%d", fd);
    k2 = readlink(l2, pp, sizeof pp - 1); if (k2 > 0) pp[k2] = 0; else pp[0] = 0;
    if (!strcmp(pp, "/dev/dsp")) {          /* ONLY for the /dev/dsp fd: pretend an OSS device accepted the call */
      unsigned type = (req >> 8) & 0xff, nr = req & 0xff;
      r = 0; errno = 0; e = 0;
      if (type == 'P' && arg) {
        if (nr == 4) *(int *)arg = 4096;                       /* GETBLKSIZE */
        else if (nr == 11) *(int *)arg = 0x1b;                  /* GETFMTS: U8|S16LE.. */
        else if (nr == 15) *(int *)arg = 0x1;                   /* GETCAPS */
        else if (nr == 12 || nr == 13) { int *a = arg; a[0] = 16; a[1] = 16; a[2] = 4096; a[3] = 65536; } /* GETOSPACE/ISPACE */
      }
    }
  }
  if (!t0set) { clock_gettime(CLOCK_MONOTONIC, &T0); t0set = 1; const char *p = getenv("RG_IOCTL_LOG"); snprintf(lp, sizeof lp, "%s", p ? p : "/tmp/rg-ioctl.log"); }
  snprintf(lk, sizeof lk, "/proc/self/fd/%d", fd);
  { ssize_t k = readlink(lk, path, sizeof path - 1); if (k > 0) path[k] = 0; else strcpy(path, "?"); }
  clock_gettime(CLOCK_MONOTONIC, &t);
  n = snprintf(line, sizeof line, "[ioctl] + %.6f pid %d ioctl(fd %d \"%s\", req 0x%lx, arg %p) -> %d%s%s\n",
    (t.tv_sec - T0.tv_sec) + (t.tv_nsec - T0.tv_nsec)/1e9, getpid(), fd, path, req, arg, r,
    r < 0 ? " errno=" : "", r < 0 ? strerror(e) : "");
  { int f = sys3(5, (long)lp, 0x441, 0644); if (f >= 0) { sys3(4, f, (long)line, n); sys3(6, f, 0, 0); } }
  errno = e; return r;
}

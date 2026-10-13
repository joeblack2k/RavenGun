/* /tmp-only transparent entry+return tracer (NOT in the repo). Per listed address: a 5-byte jmp to a
   stub that saves flags+regs, logs "aanroep" (raw syscalls), swaps the return address for a thunk
   (which logs eax on return), restores everything, runs the displaced original bytes, jumps back.
   Does not change registers, flags or stack contents seen by the guest (except the return slot,
   restored by the thunk). List line: name addr nbytes hexbytes */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#define MAXH 64
struct h { char name[40]; uint32_t addr; int n; uint8_t orig[16]; uint32_t stub; };
static struct h H[MAXH]; static int HN;
static char logpath[256]; static struct timespec T0;
static volatile int counts[MAXH];
static __thread struct { int id; uint32_t ret; uint32_t a1; } RS[256]; static __thread int RD;
static int sys3(int nr, long a, long b, long c){ long r; __asm__ volatile("int $0x80":"=a"(r):"0"(nr),"b"(a),"c"(b),"d"(c):"memory"); return (int)r; }
static void rawlog(const char *s){
  int fd = sys3(5,(long)logpath, 0x441, 0644);
  if (fd < 0) return;
  sys3(4, fd, (long)s, (long)strlen(s)); sys3(6, fd, 0, 0);
}
static char *put_s(char *p, const char *s){ while(*s) *p++=*s++; return p; }
static char *put_u(char *p, unsigned long v){ char t[12]; int n=0; if(!v) t[n++]='0'; while(v){t[n++]='0'+v%10; v/=10;} while(n) *p++=t[--n]; return p; }
static char *put_x(char *p, uint32_t v){ int i; *p++='0'; *p++='x'; for(i=28;i>=0;i-=4) *p++="0123456789abcdef"[(v>>i)&15]; return p; }
static char *put_i(char *p, int v){ if (v<0){*p++='-'; return put_u(p,(unsigned long)(-(long)v));} return put_u(p,(unsigned long)v); }
static char *put_t(char *p){ struct timespec t; sys3(265, CLOCK_MONOTONIC, (long)&t, 0);
  long long us = (long long)(t.tv_sec - T0.tv_sec)*1000000LL + (t.tv_nsec - T0.tv_nsec)/1000;
  p = put_s(p, "[trace] + "); p = put_u(p, (unsigned long)(us/1000000)); *p++='.';
  { unsigned long f = (unsigned long)(us%1000000), d; for (d=100000; d; d/=10) *p++='0'+(f/d)%10; }
  p = put_s(p, " pid "); return put_u(p, sys3(20,0,0,0)); }
static uint8_t *thunk;
void __attribute__((used, force_align_arg_pointer)) rg_trace(int id, uint32_t *sp){
  uint32_t *fr = sp + 9; char b[400], *p = b;
  counts[id]++;
  p = put_t(p); p = put_s(p, " ENTER "); p = put_s(p, H[id].name);
  p = put_s(p, " (#"); p = put_u(p, counts[id]); p = put_s(p, ") ret="); p = put_x(p, fr[0]);
  p = put_s(p, " args="); p = put_x(p, fr[1]); *p++=','; p = put_x(p, fr[2]); *p++=','; p = put_x(p, fr[3]);
  { uint32_t a0 = fr[1]; unsigned char v;
    if (a0 > 0x1000 && sys3(218, a0 & ~0xfffu, 4096, (long)&v) == 0) {
      const char *c = (const char *)(uintptr_t)a0; int k, ok = 1;
      for (k = 0; k < 3; k++) if (c[k] < 32 || c[k] > 126) { ok = 0; break; }
      if (!ok) { int q; p = put_s(p, " a0mem="); for (q = 0; q < 16; q++) { unsigned char cc = (unsigned char)c[q]; *p++="0123456789abcdef"[cc>>4]; *p++="0123456789abcdef"[cc&15]; } }
      if (ok) { p = put_s(p, " a0=\""); for (k = 0; k < 60 && c[k] >= 32 && c[k] < 127 && ((uintptr_t)(c+k) & 0xfff || sys3(218,(long)(c+k),4096,(long)&v)==0); k++) *p++ = c[k]; p = put_s(p, "\""); }
    } }
  *p++='\n'; *p = 0; rawlog(b);
  if (RD < 256) { RS[RD].id = id; RS[RD].ret = fr[0]; RS[RD].a1 = fr[2]; RD++; fr[0] = (uint32_t)(uintptr_t)thunk; }
}
void __attribute__((used, force_align_arg_pointer)) rg_ret(uint32_t *sp){
  char b[260], *p = b; int id = 0; uint32_t eax = sp[7], a1 = 0;
  if (RD > 0) { RD--; id = RS[RD].id; sp[9] = RS[RD].ret; a1 = RS[RD].a1; } else { sp[9] = 0; }
  p = put_t(p); p = put_s(p, " RETURN "); p = put_s(p, H[id].name); p = put_s(p, " eax=");
  p = put_x(p, eax); p = put_s(p, " ("); p = put_i(p, (int)eax); p = put_s(p, ")");
  { unsigned char v; if (a1 > 0x1000 && sys3(218, a1 & ~0xfffu, 4096, (long)&v) == 0 && (a1 & 0xfff) < 0xff0) { p = put_s(p, " [arg1]="); p = put_x(p, *(volatile uint32_t*)(uintptr_t)a1); } }
  p = put_s(p, "\n"); *p = 0; rawlog(b);
}
static uint8_t *code; static size_t codeoff;
static void make_stub(int i){
  struct h *x = &H[i]; uint8_t *p = code + codeoff, *s = p; uint32_t rel;
  *p++=0x9c; *p++=0x60; *p++=0x89; *p++=0xe0; *p++=0x50;
  *p++=0x68; *(uint32_t*)p=i; p+=4;
  *p++=0xe8; rel=(uint32_t)(uintptr_t)rg_trace-(uint32_t)(uintptr_t)(p+4); *(uint32_t*)p=rel; p+=4;
  *p++=0x83; *p++=0xc4; *p++=0x08;
  *p++=0x61; *p++=0x9d;
  memcpy(p, x->orig, x->n); p+=x->n;
  *p++=0xe9; rel=(x->addr + x->n)-(uint32_t)(uintptr_t)(p+4); *(uint32_t*)p=rel; p+=4;
  codeoff = (size_t)(p - code); codeoff = (codeoff+15)&~15u;
  x->stub = (uint32_t)(uintptr_t)s;
}
static void make_thunk(void){
  uint8_t *p = code + codeoff; uint32_t rel; thunk = p;
  *p++=0x6a; *p++=0x00;                    /* push 0 (slot for the original return) */
  *p++=0x9c; *p++=0x60;                    /* pushfd; pushad */
  *p++=0x89; *p++=0xe0; *p++=0x50;         /* mov eax,esp; push eax */
  *p++=0xe8; rel=(uint32_t)(uintptr_t)rg_ret-(uint32_t)(uintptr_t)(p+4); *(uint32_t*)p=rel; p+=4;
  *p++=0x83; *p++=0xc4; *p++=0x04;         /* add esp,4 */
  *p++=0x61; *p++=0x9d; *p++=0xc3;         /* popad; popfd; ret */
  codeoff = (size_t)(p - code); codeoff = (codeoff+15)&~15u;
}
static int tryhook(int i){
  struct h *x = &H[i]; uint8_t *a = (uint8_t*)(uintptr_t)x->addr; unsigned char v;
  if (sys3(218, x->addr & ~0xfffu, 4096, (long)&v) != 0) return 0;
  if (memcmp(a, x->orig, x->n) != 0) return 0;
  { uint32_t rel = x->stub - (x->addr + 5);
    if (mprotect((void*)(uintptr_t)(x->addr & ~0xfffu), 8192, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) return 0;
    a[0]=0xe9; memcpy(a+1,&rel,4); memset(a+5,0x90,x->n-5); }
  return 1;
}
static void *spin(void *arg){
  int left = HN, i, patched[MAXH]; (void)arg; memset(patched, 0, sizeof patched);
  { struct timespec t; for (;;) { sys3(265, CLOCK_MONOTONIC, (long)&t, 0); if (t.tv_sec - T0.tv_sec > 20) break; if (left <= 0) break;
    for (i = 0; i < HN; i++) if (!patched[i] && tryhook(i)) { patched[i]=1; left--; } } }
  { char b[120], *p = put_t(b); p = put_s(p, " patched "); p = put_u(p, HN-left); p = put_s(p, " of "); p = put_u(p, HN); p = put_s(p, "\n"); *p = 0; rawlog(b); }
  return NULL;
}
__attribute__((constructor)) static void init(void){
  const char *lst = getenv("RG_TRACE_LIST"), *lp = getenv("RG_TRACE_LOG");
  FILE *f; char line[256]; pthread_t t;
  sys3(265, CLOCK_MONOTONIC, (long)&T0, 0);
  snprintf(logpath, sizeof logpath, "%s", lp ? lp : "/tmp/rg-trace.log");
  if (!lst || !(f = fopen(lst, "r"))) return;
  while (HN < MAXH && fgets(line, sizeof line, f)) {
    char nm[64], hex[64]; unsigned a; int n, k;
    if (sscanf(line, "%63s %x %d %63s", nm, &a, &n, hex) != 4) continue;
    snprintf(H[HN].name, 40, "%s", nm); H[HN].addr = a; H[HN].n = n;
    for (k = 0; k < n; k++) { unsigned v; sscanf(hex + 2*k, "%2x", &v); H[HN].orig[k] = (uint8_t)v; }
    HN++;
  }
  fclose(f);
  code = mmap(NULL, 8192, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0);
  if (code == MAP_FAILED) { rawlog("[trace] stub mmap failed\n"); return; }
  { int i; for (i = 0; i < HN; i++) make_stub(i); make_thunk(); }
  { char b[120], *p = put_t(b); p = put_s(p, " ctor, tracepoints="); p = put_u(p, HN); p = put_s(p, "\n"); *p = 0; rawlog(b); }
  pthread_create(&t, NULL, spin, NULL);
}

/* TEST ONLY: simulates "the loader writes the real code again after the hook patched". Wacht tot
   byte0 van hasp_login 0xe9 is, schrijft de 5 originele bytes terug, en logt of de hook de sprong herstelt. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void *th(void *a){(void)a;
  volatile unsigned char *p=(unsigned char*)0x083bbe60; const unsigned char orig[5]={0x53,0x83,0xec,0x08,0x8b};
  FILE *f=fopen(getenv("CLOBBER_LOG"),"a"); double t0=now(); int gewist=0,vorig=-1;
  while(now()-t0<2.0){ unsigned char b; if(access("/proc/self/maps",R_OK)){} 
    b=*p; if(b!=vorig){ fprintf(f,"+%.4f byte0=%02x\n",now()-t0,b); fflush(f); vorig=b; }
    if(b==0xe9 && !gewist){ memcpy((void*)p,orig,5); gewist=1; fprintf(f,"+%.4f CLOBBER: originele 5 bytes teruggeschreven\n",now()-t0); fflush(f);} usleep(50);}
  return 0;}
__attribute__((constructor)) static void c(void){ pthread_t t; if(getenv("CLOBBER_LOG")) pthread_create(&t,0,th,0);}

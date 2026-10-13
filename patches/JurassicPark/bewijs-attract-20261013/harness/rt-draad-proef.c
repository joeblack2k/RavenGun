#include <pthread.h>
#include <stdio.h>
#include <string.h>
static void *f(void *a){ return a; }
int main(void){ pthread_attr_t at; struct sched_param sp; pthread_t t; int rc;
  pthread_attr_init(&at); pthread_attr_setinheritsched(&at, PTHREAD_EXPLICIT_SCHED); pthread_attr_setschedpolicy(&at, SCHED_FIFO);
  memset(&sp,0,sizeof sp); sp.sched_priority=50; pthread_attr_setschedparam(&at,&sp);
  rc = pthread_create(&t,&at,f,0); printf("pthread_create(SCHED_FIFO/50) -> %d (%s)\n", rc, rc?strerror(rc):"ok"); if(!rc) pthread_join(t,0); return rc; }

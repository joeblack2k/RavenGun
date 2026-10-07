/* movetest: leest gedurende N seconden sensoren/knoppen van de eerste Move via PSMoveAPI en
 * rapporteert statistiek (rapportfrequentie, bereik per as, geziene knoppen, trigger-bereik). */
#include <psmoveapi/psmove.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char**argv){
  double secs=argc>1?atof(argv[1]):15;
  if(!psmove_init(PSMOVE_CURRENT_VERSION)){fprintf(stderr,"psmove_init faalde\n");return 2;}
  PSMove*m=psmove_connect(); if(!m){fprintf(stderr,"geen Move\n");return 2;}
  printf("verbinding: %s, model: %s\n", psmove_connection_type(m)==Conn_Bluetooth?"Bluetooth":"anders",
         psmove_get_model(m)==Model_ZCM1?"ZCM1":"ZCM2");
  psmove_set_leds(m,0,255,0); psmove_update_leds(m);
  int ax[9][2]; for(int i=0;i<9;i++){ax[i][0]=1<<30;ax[i][1]=-(1<<30);}
  unsigned seen=0; int tmin=255,tmax=0; long reports=0; double t0=now(),last=t0,maxgap=0;
  while(now()-t0<secs){
    int seq=psmove_poll(m);
    if(seq){ double t=now(); if(reports&&t-last>maxgap)maxgap=t-last; last=t; reports++;
      int v[9]; psmove_get_accelerometer(m,&v[0],&v[1],&v[2]); psmove_get_gyroscope(m,&v[3],&v[4],&v[5]);
      psmove_get_magnetometer(m,&v[6],&v[7],&v[8]);
      for(int i=0;i<9;i++){if(v[i]<ax[i][0])ax[i][0]=v[i]; if(v[i]>ax[i][1])ax[i][1]=v[i];}
      seen|=psmove_get_buttons(m); int tr=psmove_get_trigger(m); if(tr<tmin)tmin=tr; if(tr>tmax)tmax=tr;
      if(reports%60==0){psmove_set_leds(m,0,255,0);psmove_update_leds(m);}
    }
  }
  double el=now()-t0;
  printf("rapporten: %ld in %.1fs = %.1f/s, grootste gat %.1f ms\n",reports,el,reports/el,maxgap*1e3);
  const char*n[9]={"acc.x","acc.y","acc.z","gyro.x","gyro.y","gyro.z","mag.x","mag.y","mag.z"};
  for(int i=0;i<9;i++)printf("  %-6s min %6d max %6d\n",n[i],ax[i][0],ax[i][1]);
  printf("trigger: min %d max %d\n",tmin,tmax);
  struct{unsigned b;const char*s;}B[]={{Btn_TRIANGLE,"Driehoek"},{Btn_CIRCLE,"Cirkel"},{Btn_CROSS,"Kruis"},{Btn_SQUARE,"Vierkant"},
    {Btn_SELECT,"Select"},{Btn_START,"Start"},{Btn_PS,"PS"},{Btn_MOVE,"Move"},{Btn_T,"T(trigger)"}};
  printf("knoppen gezien:"); for(unsigned i=0;i<sizeof B/sizeof*B;i++) if(seen&B[i].b) printf(" %s",B[i].s); printf("\n");
  psmove_set_leds(m,0,0,0); psmove_update_leds(m); psmove_disconnect(m); return 0;
}

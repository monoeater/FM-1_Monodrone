#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "drone_dsp.h"
int plat_cpu2_run(void (*f)(uint32_t), uint32_t a) { (void)f; (void)a; return 0; }
int plat_cpu2_wait(void) { return 0; }
static float b[512];
int main(){
 srand(7); dr_defaults((dr_ctl_t*)&dr_ctl); dr_init();
 float pk=0; long bad=0; uint8_t *c=(uint8_t*)&dr_ctl;
 for(int blk=0; blk<44100*600/256; blk++){
   if(blk%40==0){ int k=rand()%sizeof(dr_ctl_t); c[k]= rand()%128; 
     for(int i=0;i<4;i++){ if(dr_ctl.L[i].engine>=DR_ENGINES) dr_ctl.L[i].engine%=DR_ENGINES; }
     if(rand()%5==0) for(int i=0;i<16;i++) dr_ctl.fx[i]= rand()%2?127:0;
     if(rand()%7==0) for(int i=0;i<4;i++){ dr_ctl.L[i].on=1; dr_ctl.L[i].p[0]=dr_ctl.L[i].p[1]=dr_ctl.L[i].p[2]=127; for(int k=0;k<4;k++) dr_ctl.L[i].note[k]=rand()%128; dr_ctl.L[i].nv=rand()%6; }
     if(rand()%6==0){ for(int i=0;i<PU_N;i++) for(int k=0;k<4;k++) dr_ctl.pk[i][k]=rand()%2?127:0; for(int k=0;k<MC_N;k++) dr_ctl.macro[k]=rand()%128; }
     dr_ctl.punch &= 0xFFF; if(rand()%3==0) dr_ctl.punch = rand() & 0xFFF; }
   dr_render(b,256);
   for(int i=0;i<512;i++){ if(!isfinite(b[i])) bad++; else if(fabsf(b[i])>pk) pk=fabsf(b[i]); }
 }
 printf("10 min random: peak %.3f, non-finite %ld\n", pk, bad);
}

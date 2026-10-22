// Simulacion en simavr: tarjetas de prueba con la tabla de formatos en EEPROM
// uso: ./sim sw1 sw2 sw3 (programa en b/fw_pad.bin)
#include <stdio.h>
#include <stdlib.h>
#include <simavr/sim_avr.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_eeprom.h>
#include <simavr/sim_irq.h>
avr_t *avr; uint64_t outbits=0; int nout=0,b0=1,b1=1;
static void pb_cb(struct avr_irq_t *irq,uint32_t v,void*p){int pin=(int)(intptr_t)p;
 if(pin==0){if(b0&&!v){outbits<<=1;nout++;}b0=v;} else {if(b1&&!v){outbits=(outbits<<1)|1;nout++;}b1=v;}}
static void runus(double us){avr_cycle_count_t e=avr->cycle+(avr_cycle_count_t)(us*16);while(avr->cycle<e){int s=avr_run(avr);if(s==cpu_Done||s==cpu_Crashed){printf("crash\n");exit(1);}}}
static void setpin(char port,int pin,int v){avr_raise_irq(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ(port),pin),v);}
static void sendW(const char*s){for(;*s;s++){int pin=(*s=='1')?5:4;setpin('C',pin,0);runus(50);setpin('C',pin,1);runus(2000);}}
static void flush_msg(){ if(nout==34){uint32_t d=(outbits>>1)&0xFFFFFFFF;int hi=__builtin_popcount(d>>16)+((outbits>>33)&1),lo=__builtin_popcount(d&0xFFFF)+(outbits&1);
 printf(" %u%s",d,(hi%2==0&&lo%2==1)?"":"(PARIDAD MAL)"); outbits=0;nout=0;} }
static void esperar(double us){ // corre y separa mensajes por pausa
 for(double t=0;t<us;t+=100000){ int antes=nout; runus(100000); if(nout==antes) flush_msg(); } flush_msg(); if(nout){printf(" [%d bits sueltos]",nout);nout=0;outbits=0;} }
int main(int c,char**v){int sw1=atoi(v[1]),sw2=atoi(v[2]),sw3=atoi(v[3]);
 avr=avr_make_mcu_by_name("atmega328p");avr_init(avr);avr->frequency=16000000;
 FILE*f=fopen("b/fw_pad.bin","rb");if(fread(avr->flash,1,0x8000,f)){};fclose(f);
 if(c>4){ static uint8_t ee[1024]; for(int i=0;i<1024;i++)ee[i]=0xFF; FILE*g=fopen(v[4],"rb"); int n=fread(ee,1,1024,g); fclose(g);
   avr_eeprom_desc_t d={.ee=ee,.offset=0,.size=1024}; avr_ioctl(avr,AVR_IOCTL_EEPROM_SET,&d); (void)n; }
 else { static uint8_t ee[1024]; for(int i=0;i<1024;i++)ee[i]=0xFF; avr_eeprom_desc_t d={.ee=ee,.offset=0,.size=1024}; avr_ioctl(avr,AVR_IOCTL_EEPROM_SET,&d); }
 avr_ioport_external_t e={.name='D',.mask=0x1C,.value=(sw1?0:0x10)|(sw2?0:0x08)|(sw3?0:0x04)};avr_ioctl(avr,AVR_IOCTL_IOPORT_SET_EXTERNAL('D'),&e);
 avr_ioport_external_t e2={.name='C',.mask=0x30,.value=0x30};avr_ioctl(avr,AVR_IOCTL_IOPORT_SET_EXTERNAL('C'),&e2);
 setpin('D',4,!sw1);setpin('D',3,!sw2);setpin('D',2,!sw3);setpin('C',4,1);setpin('C',5,1);
 avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('B'),0),pb_cb,(void*)0);
 avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('B'),1),pb_cb,(void*)1);
 runus(200000);
 if(sw3){printf("prueba:");esperar(11000000);printf("\n");return 0;}
 const char*cards[][2]={{"26 bits FC144 6136","11001000000010111111110001"},{"35 bits FC3787 8958","01111011001011000000100010111111101"},
  {"36 bits BCD 797 00799","011110010111000000000111100110011010"},{"26 bits FC144 1234","01001000000000100110100100"},{"26 bits FC10 1234","00000101000000100110100100"}};
 for(int i=0;i<5;i++){ printf("%-24s ->",cards[i][0]); sendW(cards[i][1]); esperar(500000); printf("\n"); }
 return 0;}

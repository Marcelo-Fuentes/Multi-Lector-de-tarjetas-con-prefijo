#include <stdio.h>
#include <stdlib.h>
#include <simavr/sim_avr.h>
#include <simavr/avr_ioport.h>
#include <simavr/sim_irq.h>
avr_t *avr; uint64_t outbits=0; int nout=0,b0=1,b1=1;
static void pb_cb(struct avr_irq_t *irq,uint32_t v,void*p){int pin=(int)(intptr_t)p;
 if(pin==0){if(b0&&!v){outbits<<=1;nout++;}b0=v;} else {if(b1&&!v){outbits=(outbits<<1)|1;nout++;}b1=v;}}
static void runus(double us){avr_cycle_count_t e=avr->cycle+(avr_cycle_count_t)(us*16);while(avr->cycle<e){int s=avr_run(avr);if(s==cpu_Done||s==cpu_Crashed){printf("crash\n");exit(1);}}}
static void setpin(char port,int pin,int v){avr_raise_irq(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ(port),pin),v);}
static void report(const char*l){ if(nout==34){uint32_t d=(outbits>>1)&0xFFFFFFFF;int hi=__builtin_popcount(d>>16)+((outbits>>33)&1),lo=__builtin_popcount(d&0xFFFF)+(outbits&1);
 printf("%s: 34 bits -> %u  paridad %s\n",l,d,(hi%2==0&&lo%2==1)?"OK":"MAL");} else printf("%s: %d bits\n",l,nout); outbits=0;nout=0;}
static void sendW(const char*s){for(;*s;s++){int pin=(*s=='1')?5:4;setpin('C',pin,0);runus(50);setpin('C',pin,1);runus(2000);}}
int main(int c,char**v){int sw1=atoi(v[1]),sw2=atoi(v[2]),sw3=atoi(v[3]);
 avr=avr_make_mcu_by_name("atmega328p");avr_init(avr);avr->frequency=16000000;
 FILE*f=fopen("b/fw_pad.bin","rb");fread(avr->flash,1,0x8000,f);fclose(f);
 avr_ioport_external_t e={.name='D',.mask=0x1C,.value=(sw1?0:0x10)|(sw2?0:0x08)|(sw3?0:0x04)};avr_ioctl(avr,AVR_IOCTL_IOPORT_SET_EXTERNAL('D'),&e);
 avr_ioport_external_t e2={.name='C',.mask=0x30,.value=0x30};avr_ioctl(avr,AVR_IOCTL_IOPORT_SET_EXTERNAL('C'),&e2);
 setpin('D',4,!sw1);setpin('D',3,!sw2);setpin('D',2,!sw3);setpin('C',4,1);setpin('C',5,1);
 avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('B'),0),pb_cb,(void*)0);
 avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('B'),1),pb_cb,(void*)1);
 runus(200000);
 if(sw3){runus(3000000);report("prueba");return 0;}
 sendW("11001000000010111111110001");runus(400000);report("tarjeta 26 (6136)");
 sendW("01111011001011000000100010111111101");runus(400000);report("tarjeta 35 (8958)");
 sendW("011110010111000000000111100110011010");runus(400000);report("tarjeta 36 (00799)");
 return 0;}

#ifndef _DELAY_H_
#define _DELAY_H_

#include "stm32f10x.h"

#define Delay_nop_1us {__nop();__nop();\
__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
}

void Systick_Init(uint32_t time);
void Delay_nop_nus(uint32_t time);
void Delay_nop_nms(uint32_t time);

#endif

#ifndef _GIMBAL_H_
#define _GIMBAL_H_

#include "stm32f10x.h"

#define GIMBAL_TIM TIM1
#define GIMBAL_PWM_PSC 71
#define GIMBAL_PWM_ARR 19999
#define GIMBAL_PULSE_MIN 500
#define GIMBAL_PULSE_MAX 2500
#define GIMBAL_PULSE_MID 1500
#define GIMBAL_STEP 25

void Gimbal_Init(void);
void Gimbal_SetPulse(uint8_t axis, uint16_t pulse);
void Gimbal_Nudge(uint8_t axis, int8_t dir);
void Gimbal_Center(void);
void Gimbal_SetDirection(uint8_t direction);
void Gimbal_Process(void);

#endif

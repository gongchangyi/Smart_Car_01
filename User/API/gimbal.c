#include "gimbal.h"

extern volatile uint32_t g_ms_tick;

static uint16_t g_pulse[2] = { GIMBAL_PULSE_MID, GIMBAL_PULSE_MID };
static uint8_t g_direction = 0x05;
static uint32_t g_last_move_ms = 0;

#define GIMBAL_MOVE_INTERVAL_MS 30

void Gimbal_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef base;
    TIM_OCInitTypeDef oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    base.TIM_Period = GIMBAL_PWM_ARR;
    base.TIM_Prescaler = GIMBAL_PWM_PSC;
    base.TIM_ClockDivision = TIM_CKD_DIV1;
    base.TIM_CounterMode = TIM_CounterMode_Up;
    base.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(GIMBAL_TIM, &base);

    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OutputNState = TIM_OutputNState_Disable;
    oc.TIM_Pulse = GIMBAL_PULSE_MID;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity = TIM_OCNPolarity_High;
    oc.TIM_OCIdleState = TIM_OCIdleState_Set;
    oc.TIM_OCNIdleState = TIM_OCIdleState_Reset;
    TIM_OC1Init(GIMBAL_TIM, &oc);
    TIM_OC4Init(GIMBAL_TIM, &oc);
    TIM_OC1PreloadConfig(GIMBAL_TIM, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(GIMBAL_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(GIMBAL_TIM, ENABLE);
    TIM_CtrlPWMOutputs(GIMBAL_TIM, ENABLE);
    TIM_Cmd(GIMBAL_TIM, ENABLE);
    Gimbal_Center();
}

void Gimbal_SetPulse(uint8_t axis, uint16_t pulse)
{
    if (pulse < GIMBAL_PULSE_MIN) pulse = GIMBAL_PULSE_MIN;
    if (pulse > GIMBAL_PULSE_MAX) pulse = GIMBAL_PULSE_MAX;
    if (axis == 1) {
        TIM_SetCompare1(GIMBAL_TIM, pulse);
        g_pulse[0] = pulse;
    } else if (axis == 2) {
        TIM_SetCompare4(GIMBAL_TIM, pulse);
        g_pulse[1] = pulse;
    }
}

void Gimbal_Nudge(uint8_t axis, int8_t dir)
{
    uint8_t index = (axis == 1) ? 0 : 1;
    Gimbal_SetPulse(axis, (uint16_t)((int32_t)g_pulse[index] + dir * GIMBAL_STEP));
}

void Gimbal_Center(void)
{
    Gimbal_SetPulse(1, GIMBAL_PULSE_MID);
    Gimbal_SetPulse(2, GIMBAL_PULSE_MID);
}

void Gimbal_SetDirection(uint8_t direction)
{
    if (direction >= 0x05 && direction <= 0x09) g_direction = direction;
}

void Gimbal_Process(void)
{
    if ((uint32_t)(g_ms_tick - g_last_move_ms) < GIMBAL_MOVE_INTERVAL_MS) return;
    g_last_move_ms = g_ms_tick;
    switch (g_direction) {
        case 0x06: Gimbal_Nudge(1, +1); break;
        case 0x07: Gimbal_Nudge(1, -1); break;
        case 0x08: Gimbal_Nudge(2, +1); break;
        case 0x09: Gimbal_Nudge(2, -1); break;
        default: break;
    }
}

#include "motor.h"

// 当前速度（0~100），换算成 CCR 比较值
static uint8_t  g_motor_speed = MOTOR_SPEED_DEFAULT;
static uint16_t g_speed_ccr   = 0;

// 设置某一定时器通道的比较值（占空比）
static void PWM_SetCompare(TIM_TypeDef* TIMx, uint8_t ch, uint16_t val)
{
    switch(ch)
    {
        case 1: TIMx->CCR1 = val; break;
        case 2: TIMx->CCR2 = val; break;
        case 3: TIMx->CCR3 = val; break;
        case 4: TIMx->CCR4 = val; break;
        default: break;
    }
}

// 电机初始化：配置 TIM3 + TIM4 输出 PWM，覆盖全部 8 个方向脚
// TIM3 默认映射: PA6(CH1) PA7(CH2) PB0(CH3) PB1(CH4)
// TIM4 默认映射: PB6(CH1) PB7(CH2) PB8(CH3) PB9(CH4)
void Motor_Init(void)
{
    GPIO_InitTypeDef          GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef   TIM_TimeBaseStructure;
    TIM_OCInitTypeDef         TIM_OCInitStructure;

    // 使能时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3 | RCC_APB1Periph_TIM4, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    // 配置 8 个方向脚为定时器复用推挽输出
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;   // PA6, PA7 -> TIM3 CH1/CH2
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOB, &GPIO_InitStructure);                     // PB0/1 -> TIM3 CH3/4; PB6/7/8/9 -> TIM4 CH1/2/3/4

    // TIM 时基：1kHz PWM（72MHz / 72 / 1000 = 1kHz）
    TIM_TimeBaseStructure.TIM_Period        = PWM_ARR;
    TIM_TimeBaseStructure.TIM_Prescaler     = 71;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    // PWM 通道配置（8 个通道全部初始化为 PWM1 模式）
    TIM_OCInitStructure.TIM_OCMode       = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState  = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse        = 0;
    TIM_OCInitStructure.TIM_OCPolarity   = TIM_OCPolarity_High;

    TIM_OC1Init(TIM3, &TIM_OCInitStructure);
    TIM_OC2Init(TIM3, &TIM_OCInitStructure);
    TIM_OC3Init(TIM3, &TIM_OCInitStructure);
    TIM_OC4Init(TIM3, &TIM_OCInitStructure);
    TIM_OC1Init(TIM4, &TIM_OCInitStructure);
    TIM_OC2Init(TIM4, &TIM_OCInitStructure);
    TIM_OC3Init(TIM4, &TIM_OCInitStructure);
    TIM_OC4Init(TIM4, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM4, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_ARRPreloadConfig(TIM4, ENABLE);

    TIM_Cmd(TIM3, ENABLE);
    TIM_Cmd(TIM4, ENABLE);

    // 计算默认速度对应的比较值
    g_speed_ccr = (g_motor_speed * (PWM_ARR + 1)) / 100;

    // 所有电机停止
    Motor_Stop();
}

// 停止所有电机（所有 PWM 通道占空比=0）
void Motor_Stop(void)
{
    TIM3->CCR1 = 0; TIM3->CCR2 = 0; TIM3->CCR3 = 0; TIM3->CCR4 = 0;
    TIM4->CCR1 = 0; TIM4->CCR2 = 0; TIM4->CCR3 = 0; TIM4->CCR4 = 0;
}

// 设置速度（0~100）
void Motor_SetSpeed(uint8_t speed)
{
    if(speed > 100) speed = 100;
    g_motor_speed = speed;
    g_speed_ccr   = (speed * (PWM_ARR + 1)) / 100;
}

// dir: 1=按当前速度通电转动, 0=停止(占空比0)
void Motor_Single(uint8_t motor, uint8_t dir)
{
    uint16_t cmp = dir ? g_speed_ccr : 0;
    switch(motor)
    {
        case 1:  PWM_SetCompare(TIM3, 3, cmp); break;  // 右后逆 PB0/TIM3_CH3
        case 2:  PWM_SetCompare(TIM3, 4, cmp); break;  // 右后顺 PB1/TIM3_CH4
        case 3:  PWM_SetCompare(TIM4, 1, cmp); break;  // 左后顺 PB6/TIM4_CH1
        case 4:  PWM_SetCompare(TIM4, 2, cmp); break;  // 左后逆 PB7/TIM4_CH2
        case 5:  PWM_SetCompare(TIM4, 3, cmp); break;  // 右前逆 PB8/TIM4_CH3
        case 6:  PWM_SetCompare(TIM4, 4, cmp); break;  // 右前顺 PB9/TIM4_CH4
        case 7:  PWM_SetCompare(TIM3, 1, cmp); break;  // 左前顺 PA6/TIM3_CH1
        case 8:  PWM_SetCompare(TIM3, 2, cmp); break;  // 左前逆 PA7/TIM3_CH2
        default: break;
    }
}

// 前进（所有轮子）
void Motor_Forward(void)
{
    Motor_Single(2, 1);  // 右后顺
    Motor_Single(3, 1);  // 左后顺
    Motor_Single(6, 1);  // 右前顺
    Motor_Single(7, 1);  // 左前顺
}

// 后退
void Motor_Backward(void)
{
    Motor_Single(1, 1);  // 右后逆
    Motor_Single(4, 1);  // 左后逆
    Motor_Single(5, 1);  // 右前逆
    Motor_Single(8, 1);  // 左前逆
}

// 左转
void Motor_TurnLeft(void)
{
    Motor_Single(1, 1);
    Motor_Single(2, 0);
    Motor_Single(3, 1);
    Motor_Single(4, 0);
}

// 右转
void Motor_TurnRight(void)
{
    Motor_Single(1, 0);
    Motor_Single(2, 1);
    Motor_Single(3, 0);
    Motor_Single(4, 1);
}

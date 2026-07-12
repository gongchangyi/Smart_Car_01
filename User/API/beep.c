#include "beep.h"
#include "delay.h"     // Delay_nop_nms

// 初始化蜂鸣器引脚为推挽输出，默认关闭（不响）
void BEEP_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = BEEP_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;     // 推挽输出
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(BEEP_PORT, &gpio);

    BEEP_Off();
}

void BEEP_On(void)
{
    if (BEEP_ACTIVE_LEVEL)
        GPIO_SetBits(BEEP_PORT, BEEP_PIN);
    else
        GPIO_ResetBits(BEEP_PORT, BEEP_PIN);
}

void BEEP_Off(void)
{
    if (BEEP_ACTIVE_LEVEL)
        GPIO_ResetBits(BEEP_PORT, BEEP_PIN);
    else
        GPIO_SetBits(BEEP_PORT, BEEP_PIN);
}

// 响 ms 毫秒后自动关闭（调用处会短暂阻塞，用于"发现障碍"提示音）
void BEEP_Beep(uint16_t ms)
{
    BEEP_On();
    Delay_nop_nms(ms);
    BEEP_Off();
}

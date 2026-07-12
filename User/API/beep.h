#ifndef _BEEP_H_
#define _BEEP_H_

#include "stm32f10x.h"

// 蜂鸣器引脚：硬件实际接在 PA15（用户确认）。
// 注意：PA15 默认是 JTAG 的 JTDI 调试脚，本项目在 main.c 初始化时已通过
// GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE) 关闭 JTAG、保留 SWD，
// 从而把 PA15 释放为普通 GPIO，故可正常用作蜂鸣器输出。
#define BEEP_PORT   GPIOA
#define BEEP_PIN    GPIO_Pin_15

// 蜂鸣器有效电平：1 = 高电平响（常见信盈达/正点原子板）。
// 若你的板子是低电平才响，把这里改成 0 即可，无需改其他代码。
#define BEEP_ACTIVE_LEVEL  1

void BEEP_Init(void);
void BEEP_On(void);
void BEEP_Off(void);
void BEEP_Beep(uint16_t ms);   // 响 ms 毫秒后自动关闭（内部用阻塞延时）

#endif

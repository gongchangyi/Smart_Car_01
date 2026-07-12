#include "delay.h"


void Systick_Init(uint32_t time)
{
	SysTick_Config(time);
}

// 全局毫秒计数器（SysTick 每 1ms 中断自增一次）
volatile uint32_t g_ms_tick = 0;

//中断服务函数名字固定，没有参数，没有返回值
void SysTick_Handler(void)
{
	g_ms_tick++;	//1ms进入一次
}

void Delay_nop_nus(uint32_t time)
{
	for(uint32_t i=0;i<time;i++)
		Delay_nop_1us;
}

void Delay_nop_nms(uint32_t time)
{
	for(uint32_t i=0;i<time;i++)
	{
		for(uint32_t j=0;j<1000;j++)
			Delay_nop_1us;
	}
}

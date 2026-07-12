#include "delay.h"


void Systick_Init(uint32_t time)
{
	SysTick_Config(time);
}

// ȫ�ֺ����������SysTick ÿ 1ms �ж�����һ�Σ�
volatile uint32_t g_ms_tick = 0;

//�жϷ��������̶ֹ���û�в�����û�з���ֵ
void SysTick_Handler(void)
{
	g_ms_tick++;	//1ms����һ��
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

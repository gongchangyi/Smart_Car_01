#include "bluetooth.h"

// 接收缓冲区
static volatile uint8_t bt_rx_data = 0;
static volatile uint8_t bt_rx_ready = 0;

// USART2初始化
void Bluetooth_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    // 配置TX引脚 (PA2)
    GPIO_InitStructure.GPIO_Pin = BT_TX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BT_TX_PORT, &GPIO_InitStructure);

    // 配置RX引脚 (PA3)
    GPIO_InitStructure.GPIO_Pin = BT_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(BT_RX_PORT, &GPIO_InitStructure);

    // USART配置
    USART_InitStructure.USART_BaudRate = BT_BAUDRATE;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(BT_USART, &USART_InitStructure);

    // 使能接收中断
    USART_ITConfig(BT_USART, USART_IT_RXNE, ENABLE);

    // NVIC配置
    NVIC_InitStructure.NVIC_IRQChannel = BT_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 使能USART
    USART_Cmd(BT_USART, ENABLE);
}

// USART2中断服务函数
void USART2_IRQHandler(void)
{
    if(USART_GetITStatus(BT_USART, USART_IT_RXNE) != RESET)
    {
        bt_rx_data = USART_ReceiveData(BT_USART);
        bt_rx_ready = 1;
        USART_ClearITPendingBit(BT_USART, USART_IT_RXNE);
    }
}

// 发送单字节
void Bluetooth_SendByte(uint8_t byte)
{
    USART_SendData(BT_USART, byte);
    while(USART_GetFlagStatus(BT_USART, USART_FLAG_TXE) == RESET);
}

// 发送字符串
void Bluetooth_SendString(char *str)
{
    while(*str)
    {
        Bluetooth_SendByte(*str++);
    }
}

// 获取命令
uint8_t Bluetooth_GetCommand(void)
{
    if(bt_rx_ready) {
        bt_rx_ready = 0;
        return bt_rx_data;
    }
    return 0;
}
#include "wifi_uart.h"
#include "motor.h"
#include "gimbal.h"

#define WIFI_RX_BUF_SIZE 64

static volatile uint8_t wifi_rx_buf[WIFI_RX_BUF_SIZE];
static volatile uint16_t wifi_rx_count = 0;
static volatile uint8_t wifi_rx_over = 0;
static volatile uint8_t wifi_stop_flag = 0;
static volatile uint8_t g_wifi_frame_cmd = 0;

void WifiUart_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    gpio.GPIO_Pin = WIFI_TX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(WIFI_TX_PORT, &gpio);
    gpio.GPIO_Pin = WIFI_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(WIFI_RX_PORT, &gpio);

    uart.USART_BaudRate = WIFI_BAUDRATE;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(WIFI_USART, &uart);
    USART_ITConfig(WIFI_USART, USART_IT_RXNE, ENABLE);
    USART_ITConfig(WIFI_USART, USART_IT_IDLE, ENABLE);

    nvic.NVIC_IRQChannel = WIFI_USART_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    USART_Cmd(WIFI_USART, ENABLE);
}

void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(WIFI_USART, USART_IT_RXNE) != RESET) {
        uint8_t data = (uint8_t)USART_ReceiveData(WIFI_USART);
        if (!wifi_rx_over && wifi_rx_count < WIFI_RX_BUF_SIZE)
            wifi_rx_buf[wifi_rx_count++] = data;
    }
    if (USART_GetFlagStatus(WIFI_USART, USART_FLAG_IDLE) != RESET) {
        (void)WIFI_USART->SR;
        (void)WIFI_USART->DR;
        wifi_rx_over = 1;
    }
}

static uint8_t Wifi_MapCarCommand(uint8_t command)
{
    switch (command) {
        case 0x00: return '2';
        case 0x01: return '3';
        case 0x02: return '1';
        case 0x03: return '5';
        case 0x04: return '6';
        default: return 0;
    }
}

void WifiUart_Process(void)
{
    uint8_t data[WIFI_RX_BUF_SIZE];
    uint16_t count, i;

    if (!wifi_rx_over) return;
    USART_ITConfig(WIFI_USART, USART_IT_RXNE, DISABLE);
    USART_ITConfig(WIFI_USART, USART_IT_IDLE, DISABLE);
    count = wifi_rx_count;
    if (count > WIFI_RX_BUF_SIZE) count = WIFI_RX_BUF_SIZE;
    for (i = 0; i < count; ++i) data[i] = wifi_rx_buf[i];
    wifi_rx_count = 0;
    wifi_rx_over = 0;
    USART_ITConfig(WIFI_USART, USART_IT_RXNE, ENABLE);
    USART_ITConfig(WIFI_USART, USART_IT_IDLE, ENABLE);

    for (i = 0; i < count; ++i) {
        uint8_t car;
        if (i + 5 < count && data[i] == 0xAA && data[i + 1] == 0x55) {
            car = Wifi_MapCarCommand(data[i + 2]);
            if (car) {
                g_wifi_frame_cmd = car;
                if (car == '2') wifi_stop_flag = 1;
            }
            Gimbal_SetDirection(data[i + 3]);
            i += 5;
            continue;
        }
        if (i + 2 < count && data[i] == 0xAA && data[i + 1] == 0x55) {
            car = Wifi_MapCarCommand(data[i + 2]);
            if (car) {
                g_wifi_frame_cmd = car;
                if (car == '2') wifi_stop_flag = 1;
            } else if (data[i + 2] >= 0x05 && data[i + 2] <= 0x09) {
                Gimbal_SetDirection(data[i + 2]);
            }
            i += 2;
        }
    }
}

void WifiUart_SendByte(uint8_t byte)
{
    USART_SendData(WIFI_USART, byte);
    while (USART_GetFlagStatus(WIFI_USART, USART_FLAG_TXE) == RESET) {}
}

void WifiUart_SendString(char *str)
{
    while (*str) WifiUart_SendByte((uint8_t)*str++);
}

uint8_t WifiUart_GetCommand(void)
{
    uint8_t command = g_wifi_frame_cmd;
    g_wifi_frame_cmd = 0;
    return command;
}

uint8_t WifiUart_StopPending(void)
{
    return wifi_stop_flag;
}

void WifiUart_ClearStop(void)
{
    wifi_stop_flag = 0;
}

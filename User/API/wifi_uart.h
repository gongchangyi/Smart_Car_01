#ifndef _WIFI_UART_H_
#define _WIFI_UART_H_

#include "stm32f10x.h"

#define WIFI_USART USART3
#define WIFI_USART_IRQn USART3_IRQn
#define WIFI_TX_PORT GPIOB
#define WIFI_TX_PIN GPIO_Pin_10
#define WIFI_RX_PORT GPIOB
#define WIFI_RX_PIN GPIO_Pin_11
#define WIFI_BAUDRATE 9600

void WifiUart_Init(void);
void WifiUart_Process(void);
void WifiUart_SendByte(uint8_t byte);
void WifiUart_SendString(char *str);
uint8_t WifiUart_GetCommand(void);
uint8_t WifiUart_StopPending(void);
void WifiUart_ClearStop(void);

#endif

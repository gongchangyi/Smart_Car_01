#ifndef _BLUETOOTH_H_
#define _BLUETOOTH_H_

#include "stm32f10x.h"

// ============================================
// 蓝牙模块引脚（信盈达小车V2.2原理图）
// ============================================

#define BT_USART                 USART2
#define BT_USART_IRQn            USART2_IRQn
#define BT_USART_IRQHandler      USART2_IRQHandler

#define BT_TX_PORT               GPIOA
#define BT_TX_PIN                GPIO_Pin_2    // PA2

#define BT_RX_PORT               GPIOA
#define BT_RX_PIN                GPIO_Pin_3    // PA3

#define BT_BAUDRATE              9600

// 命令定义
#define CMD_FORWARD              'F'
#define CMD_BACKWARD             'B'
#define CMD_LEFT                 'L'
#define CMD_RIGHT                'R'
#define CMD_STOP                 'S'
#define CMD_MODE_TRACK           'T'
#define CMD_MODE_AVOID           'A'
#define CMD_MODE_BLUETOOTH       'X'

// 速度微调指令（单字符，直接由蓝牙发送，替代原双字符 "00"/"11"）
// 'm' -> 速度减1%
// 'n' -> 速度加1%

// 函数声明
void Bluetooth_Init(void);
void Bluetooth_SendByte(uint8_t byte);
void Bluetooth_SendString(char *str);
uint8_t Bluetooth_GetCommand(void);
uint8_t Bluetooth_StopPending(void);   // 是否有待处理的紧急停止请求
void Bluetooth_ClearStop(void);        // 清除紧急停止标志

#endif

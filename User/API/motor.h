#ifndef _MOTOR_H_
#define _MOTOR_H_

#include "stm32f10x.h"

// ============================================
// 信盈达智能车电机控制引脚（以源码为准，用户实测确认）
// ============================================

// 右后电机
#define MOTOR_RB_IA_PORT    GPIOB
#define MOTOR_RB_IA_PIN     GPIO_Pin_0    // PB0 -> TIM3_CH3
#define MOTOR_RB_IB_PORT    GPIOB
#define MOTOR_RB_IB_PIN     GPIO_Pin_1    // PB1 -> TIM3_CH4

// 左后电机
#define MOTOR_LB_IA_PORT    GPIOB
#define MOTOR_LB_IA_PIN     GPIO_Pin_6    // PB6 -> TIM4_CH1
#define MOTOR_LB_IB_PORT    GPIOB
#define MOTOR_LB_IB_PIN     GPIO_Pin_7    // PB7 -> TIM4_CH2

// 右前电机
#define MOTOR_RF_IA_PORT    GPIOB
#define MOTOR_RF_IA_PIN     GPIO_Pin_8    // PB8 -> TIM4_CH3
#define MOTOR_RF_IB_PORT    GPIOB
#define MOTOR_RF_IB_PIN     GPIO_Pin_9    // PB9 -> TIM4_CH4

// 左前电机
#define MOTOR_LF_IA_PORT    GPIOA
#define MOTOR_LF_IA_PIN     GPIO_Pin_6    // PA6 -> TIM3_CH1
#define MOTOR_LF_IB_PORT    GPIOA
#define MOTOR_LF_IB_PIN     GPIO_Pin_7    // PA7 -> TIM3_CH2

// 电机方向定义
#define MOTOR_DIR_STOP      0
#define MOTOR_DIR_FORWARD   1
#define MOTOR_DIR_BACKWARD  2

// 速度控制（PWM 占空比 0~100）
#define MOTOR_SPEED_DEFAULT 70      // 默认速度 70%（PWM已验证生效）
#define PWM_ARR              999    // TIM 自动重装载值(ARR)，PWM频率=72MHz/(72*1000)=1kHz

// 函数声明
void Motor_Init(void);
void Motor_Stop(void);
void Motor_Forward(void);
void Motor_Backward(void);
void Motor_TurnLeft(void);
void Motor_TurnRight(void);
void Motor_Single(uint8_t motor, uint8_t dir);
void Motor_SetSpeed(uint8_t speed);   // 设置速度(0~100)，预留给后续蓝牙调速

#endif

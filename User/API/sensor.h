#ifndef _SENSOR_H_
#define _SENSOR_H_

#include "stm32f10x.h"

// ============================================
// 红外循迹传感器引脚（5路黑线检测）
// ============================================

#define SENSOR_1_PORT       GPIOC
#define SENSOR_1_PIN        GPIO_Pin_7       // 最左 (OUT1)

#define SENSOR_2_PORT       GPIOC
#define SENSOR_2_PIN        GPIO_Pin_8       // 左   (OUT2)

#define SENSOR_3_PORT       GPIOC
#define SENSOR_3_PIN        GPIO_Pin_9       // 中   (OUT3)

#define SENSOR_4_PORT       GPIOC
#define SENSOR_4_PIN        GPIO_Pin_10      // 右   (OUT4)

#define SENSOR_5_PORT       GPIOC
#define SENSOR_5_PIN        GPIO_Pin_11      // 最右 (OUT5)

// 循迹状态（低电平有效：检测到黑线=0）
#define SENSOR_ON_LINE      0
#define SENSOR_OFF_LINE     1

// 循迹结果
#define LINE_CENTER         0   // 在中间
#define LINE_LEFT           1   // 偏左
#define LINE_LEFT_FAR       2   // 偏左很远
#define LINE_RIGHT          3   // 偏右
#define LINE_RIGHT_FAR      4   // 偏右很远
#define LINE_LOST           5   // 丢线

// ============================================
// 超声波避障传感器引脚（HC-SR04 兼容模块）
// 用户实测确认（原理图标 PA4/PA5 为错，以此为准）：
//   TRIG -> PB14 (推挽输出)
//   ECHO -> PC6  (浮空输入，FT 5V 容忍，可直连模块 5V 回波)
// 模块供电：VCC 接 5~8V（内部稳压到 5V），所以 8V 供电不会烧
// ============================================

#define USONIC_TRIG_PORT    GPIOB
#define USONIC_TRIG_PIN     GPIO_Pin_14      // 触发信号输出（用户实测确认）

#define USONIC_ECHO_PORT    GPIOC
#define USONIC_ECHO_PIN     GPIO_Pin_6       // 回响信号输入（用户实测确认）

// 避障距离阈值（单位：cm，小于此值判定是有障碍）
#define USONIC_OBSTACLE_CM  50              // 50cm以内视为障碍物（提前右转避障）
#define USONIC_MAX_CM       400             // 超声波最大量程上限

// 函数声明：红外循迹
void Sensor_Init(void);
uint8_t Sensor_ReadLine(void);
uint8_t Sensor_ReadBits(void);    // 返回5路原始状态(bit0=最左..bit4=最右, 1=压黑线)

// 函数声明：超声波避障
void USONIC_Init(void);
uint32_t USONIC_GetDistance(void);           // 返回距离(cm)，0表示超时/无回波
uint8_t USONIC_IsObstacle(void);             // 返回1=有障碍，0=安全
uint8_t USONIC_GetLastError(void);           // 返回上次测距错误码：0成功/1无回波/2脉宽异常
uint8_t USONIC_DWT_OK(void);                 // 返回1=DWT计时可用，0=已退回nop计时

#endif

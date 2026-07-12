#include "sensor.h"
#include "delay.h"

// ============================================
// DWT 硬件周期计数器（Cortex-M3 内置，72MHz 下 1 计数 = 1/72 us）
// 用于超声波回波高电平宽度的精确计时，避免软件循环延时误差
// ============================================
#define DWT_CTRL_REG    (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT_REG  (*(volatile uint32_t*)0xE0001004)
#define DEMCR_REG       (*(volatile uint32_t*)0xE000EDFC)

static void DWT_Init(void)
{
    DEMCR_REG |= (1u << 24);     // 使能 DWT（TRCENA）
    DWT_CYCCNT_REG = 0;          // 计数清零
    DWT_CTRL_REG |= 1u;          // 启动 CYCCNT
}

// 返回自 DWT 启动后的微秒数（0~约59秒回绕，足够单次测距使用）
static uint32_t DWT_Micros(void)
{
    return DWT_CYCCNT_REG / 72;  // 72MHz -> 1us
}

// ============================================
// 红外循迹传感器（5路黑线检测）
// ============================================

void Sensor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    // 5路循迹引脚配置为上拉输入
    GPIO_InitStructure.GPIO_Pin = SENSOR_1_PIN | SENSOR_2_PIN | SENSOR_3_PIN |
                                   SENSOR_4_PIN | SENSOR_5_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(SENSOR_1_PORT, &GPIO_InitStructure);
}

static uint8_t Sensor_ReadSingle(GPIO_TypeDef* port, uint16_t pin)
{
    return GPIO_ReadInputDataBit(port, pin);
}

uint8_t Sensor_ReadLine(void)
{
    uint8_t s1 = Sensor_ReadSingle(SENSOR_1_PORT, SENSOR_1_PIN);  // 最左
    uint8_t s2 = Sensor_ReadSingle(SENSOR_2_PORT, SENSOR_2_PIN);  // 左
    uint8_t s3 = Sensor_ReadSingle(SENSOR_3_PORT, SENSOR_3_PIN);  // 中
    uint8_t s4 = Sensor_ReadSingle(SENSOR_4_PORT, SENSOR_4_PIN);  // 右
    uint8_t s5 = Sensor_ReadSingle(SENSOR_5_PORT, SENSOR_5_PIN);  // 最右

    if(s3 == SENSOR_ON_LINE)       return LINE_CENTER;
    else if(s2 == SENSOR_ON_LINE)  return LINE_LEFT;
    else if(s1 == SENSOR_ON_LINE)  return LINE_LEFT_FAR;
    else if(s4 == SENSOR_ON_LINE)  return LINE_RIGHT;
    else if(s5 == SENSOR_ON_LINE)  return LINE_RIGHT_FAR;
    else                            return LINE_LOST;
}

// ============================================
// 超声波避障传感器（HC-SR04 兼容模块）
// TRIG: PA4(输出) -> 发送>10us触发脉冲
// ECHO: PA5(输入) -> 接收回响脉冲，高电平宽度正比于距离
// 距离(cm) = 高电平时间(us) * 340m/s / 2 = 时间 / 58
// 注意：超声波模块最小测量周期约 60ms，调用间隔必须 >= 60ms
// ============================================

void USONIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    // 启动 DWT 硬件计时器（用于精确测距）
    DWT_Init();

    // 使能GPIOB(TRIG)和GPIOC(ECHO)时钟（引脚以用户实测为准：TRIG=PB14, ECHO=PC6）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);

    // TRIG (PA4): 推挽输出，初始低电平
    GPIO_InitStructure.GPIO_Pin  = USONIC_TRIG_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(USONIC_TRIG_PORT, &GPIO_InitStructure);
    GPIO_ResetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN);

    // ECHO (PA5): 浮空输入（超声波模块输出数字电平）
    GPIO_InitStructure.GPIO_Pin  = USONIC_ECHO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(USONIC_ECHO_PORT, &GPIO_InitStructure);
}

// 超声波测距错误状态（供调试区分失败原因）
// 0=成功  1=TRIG发出后ECHO无上升沿(模块未回波)  2=捕获到上升沿但脉宽异常
static uint8_t g_us_err = 0;

uint8_t USONIC_GetLastError(void)
{
    return g_us_err;
}

// 获取超声波测距结果（单位：cm）
// 返回值：0 表示超时/无回波（前方无障碍或模块未响应）；有效值 2~400 cm
uint32_t USONIC_GetDistance(void)
{
    uint32_t t_start, t_end, width_us;

    // ---- 第1步：发送触发脉冲（>10us 高电平）----
    GPIO_SetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN);
    Delay_nop_nus(20);           // 延时20us（满足 >10us 要求）
    GPIO_ResetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN);

    // ---- 第2步：等待回响上升沿（ECHO变高），加超时保护 ----
    // 模块收到触发后约0.5ms内开始回波；这里最多等 30ms
    g_us_err = 0;
    t_start = DWT_Micros();
    while(GPIO_ReadInputDataBit(USONIC_ECHO_PORT, USONIC_ECHO_PIN) == 0)
    {
        if(DWT_Micros() - t_start > 30)   // 30ms 内无回响 -> 模块未响应/前方无反射面
        {
            g_us_err = 1;          // 无上升沿
            return 0;
        }
    }

    // ---- 第3步：测量回响高电平脉冲宽度（硬件精确计时）----
    t_start = DWT_Micros();
    while(GPIO_ReadInputDataBit(USONIC_ECHO_PORT, USONIC_ECHO_PIN) == 1)
    {
        if(DWT_Micros() - t_start > (USONIC_MAX_CM * 58 / 1000 + 5))
        {
            break;              // 超过最大量程时长，防止死循环
        }
    }
    t_end = DWT_Micros();
    width_us = t_end - t_start;

    // ---- 第4步：换算为距离（声速340m/s，往返：cm = us / 58）----
    if(width_us == 0)
    {
        g_us_err = 2;          // 脉宽异常
        return 0;
    }
    return width_us / 58;
}

// 判断前方是否有障碍物（基于超声波距离阈值）
// 注意：dist==0（无回波）表示前方空旷，视为安全；只有有效测距且 < 阈值才判障碍
uint8_t USONIC_IsObstacle(void)
{
    uint32_t dist = USONIC_GetDistance();

    if(dist > 0 && dist < USONIC_OBSTACLE_CM)
    {
        return 1;               // 前方近距离有障碍物
    }
    return 0;                   // 安全（含空旷无回波）
}

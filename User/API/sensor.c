#include "sensor.h"
#include "delay.h"
#include "stm32f10x_tim.h"
#include "misc.h"

// ============================================
// DWT 硬件周期计数器（Cortex-M3 内置，72MHz 下 1 计数 = 1/72 us）
// 用于超声波回波高电平宽度的精确计时，避免软件循环延时误差
// 关键：DWT 在某些复位/调试环境下可能没跑起来。若 CYCCNT 不递增，
// 下面所有"等待回波"的 while 会变成死循环卡死整个程序（这正是之前
// 调试"无反应"的头号嫌疑）。所以这里加了自校验，失败就退回 nop 计时。
// ============================================
#define DWT_CTRL_REG    (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT_REG  (*(volatile uint32_t*)0xE0001004)
#define DEMCR_REG       (*(volatile uint32_t*)0xE000EDFC)

static uint8_t g_dwt_ok = 0;   // 1=DWT计时可用, 0=已退回nop计时

static void DWT_Init(void)
{
    DEMCR_REG |= (1u << 24);     // 使能 DWT（TRCENA）
    DWT_CYCCNT_REG = 0;          // 计数清零
    DWT_CTRL_REG |= 1u;          // 启动 CYCCNT

    // 自校验：跑几微秒后看 CYCCNT 是否真的增加
    uint32_t a = DWT_CYCCNT_REG;
    Delay_nop_nus(2);            // 约 2us ≈ 144 个时钟周期
    g_dwt_ok = (DWT_CYCCNT_REG > a) ? 1 : 0;
}

uint8_t USONIC_DWT_OK(void)
{
    return g_dwt_ok;
}

// DWT 仅用于启动诊断(DWT: 打印)，测距已改用 TIM2 中断方案，不再依赖它。

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

// 返回5路原始状态: bit0=最左(OUT1/PC7) ... bit4=最右(OUT5/PC11)
// 1=压到黑线(灭/低电平 SENSOR_ON_LINE), 0=白面(亮/高电平)
uint8_t Sensor_ReadBits(void)
{
    uint8_t b = 0;
    if (Sensor_ReadSingle(SENSOR_1_PORT, SENSOR_1_PIN) == SENSOR_ON_LINE) b |= 0x01;
    if (Sensor_ReadSingle(SENSOR_2_PORT, SENSOR_2_PIN) == SENSOR_ON_LINE) b |= 0x02;
    if (Sensor_ReadSingle(SENSOR_3_PORT, SENSOR_3_PIN) == SENSOR_ON_LINE) b |= 0x04;
    if (Sensor_ReadSingle(SENSOR_4_PORT, SENSOR_4_PIN) == SENSOR_ON_LINE) b |= 0x08;
    if (Sensor_ReadSingle(SENSOR_5_PORT, SENSOR_5_PIN) == SENSOR_ON_LINE) b |= 0x10;
    return b;
}

// ============================================
// 超声波避障传感器（HC-SR04 兼容模块）
// 测量方案：参考老师源码，采用 EXTI 外部中断(PC6 上升/下降沿)
//           + TIM2 定时器(10us 中断) 精确计数回波脉宽。
// TRIG: PB14(推挽输出)  ECHO: PC6(浮空输入, FT 5V 容忍, 可直连 5V 回波)
// 引脚以用户实测确认（原理图标 PA4/PA5 为错）。
// 该方案由硬件中断精确捕获回波边沿，比软件轮询更可靠。
// ============================================

// 超声波测距运行状态（移植自老师 __SR04_TypeDef）
typedef struct {
    uint32_t sendCount;      // 触发脉冲发送计数(以 10us 为单位)
    uint8_t  recvCountFlag;  // 1=正在接收回波高电平
    uint32_t recvCount;      // 回波高电平持续计数(以 10us 为单位)
    float    leng;           // 测得距离(cm)
} __SR04_TypeDef;

static __SR04_TypeDef sr04 = {0};

// ---- GPIO + 外部中断配置 ----
static void Sr04_IOConfig(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    EXTI_InitTypeDef  EXTI_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);

    // TRIG(PB14): 推挽输出，初始低电平
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin   = USONIC_TRIG_PIN;
    GPIO_Init(USONIC_TRIG_PORT, &GPIO_InitStructure);

    // ECHO(PC6): 浮空输入（FT 引脚可接模块 5V 回波）
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin   = USONIC_ECHO_PIN;
    GPIO_Init(USONIC_ECHO_PORT, &GPIO_InitStructure);

    GPIO_ResetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN);

    // ECHO 接到 EXTI_Line6 (PC6)，上升/下降沿都触发
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource6);
    EXTI_InitStructure.EXTI_Line    = EXTI_Line6;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x0F;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0x0F;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

// ---- TIM2: 10us 中断，用于发触发脉冲 + 计数回波脉宽 ----
static void Sr04_TIMConfig(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef        NVIC_InitStructure;

    // 系统时钟 72MHz，APB1 上 TIM 时钟也为 72MHz
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_TimeBaseStructure.TIM_Prescaler     = 36 - 1;   // 72MHz/36 = 2MHz
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_Period        = 20 - 1;   // 2MHz/20 = 10us 中断
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x01;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0x00;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}

void USONIC_Init(void)
{
    // 保留 DWT 计时自校验（仅供启动诊断 DWT: 打印，测距不再依赖它）
    DWT_Init();
    Sr04_IOConfig();
    Sr04_TIMConfig();   // 启动后 TIM2 自动每 ~60ms 触发一次、后台持续测距
}

// 触发脉冲：在 TIM2 中断里每 10us 调用，自动产生 20us 高 + ~60ms 周期
static void Sr04_SendTTL(void)
{
    sr04.sendCount++;
    if(sr04.sendCount == 1 && GPIO_ReadOutputDataBit(USONIC_TRIG_PORT, USONIC_TRIG_PIN) == Bit_RESET)
    {
        GPIO_SetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN);   // 拉高，触发开始
    }
    else if(sr04.sendCount == 3 && GPIO_ReadOutputDataBit(USONIC_TRIG_PORT, USONIC_TRIG_PIN) == Bit_SET)
    {
        GPIO_ResetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN); // 20us 后拉低，触发结束
    }
    else if(sr04.sendCount > 6000)   // 6000*10us = 60ms 周期（满足模块最小测量间隔）
    {
        sr04.sendCount = 0;
        GPIO_ResetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN);
    }
}

// ECHO 外部中断：上升沿开始计数，下降沿结束计数
void EXTI9_5_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line6) != RESET)
    {
        if(GPIO_ReadInputDataBit(USONIC_ECHO_PORT, USONIC_ECHO_PIN) == Bit_SET)
        {
            sr04.recvCount     = 0;
            sr04.recvCountFlag = 1;   // 回波高电平开始
        }
        else
        {
            sr04.recvCountFlag = 0;   // 回波高电平结束
        }
        EXTI_ClearITPendingBit(EXTI_Line6);
    }
}

// 10us 一次的 TIM2 中断：发触发脉冲 + 计数回波脉宽
void TIM2_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
    {
        Sr04_SendTTL();
        if(sr04.recvCountFlag == 1) sr04.recvCount++;  // 仅在高电平期间计数
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

// 计算距离(cm)：距离 = 高电平时间 * 声速 / 2
// 高电平时间 = recvCount * 10us；声速 340m/s => recvCount*34/200 cm
static float Sr04_GetLength(void)
{
    if(sr04.recvCountFlag == 0)                 // 接收完成后才更新距离
        sr04.leng = sr04.recvCount * 34 / 200.0f;
    if(sr04.leng > 400) sr04.leng = 380;        // 超量程钳位
    return sr04.leng;
}

// 获取超声波测距结果（单位：cm），0 表示尚未测得有效距离
uint32_t USONIC_GetDistance(void)
{
    float d = Sr04_GetLength();
    if(d <= 0.0f) return 0;            // 未测得有效值（启动初期或模块无回波）
    return (uint32_t)(d + 0.5f);       // 四舍五入取整
}

// 判断前方是否有障碍物（基于超声波距离阈值）
// 注意：dist==0（无回波/未就绪）视为安全；只有有效测距且 < 阈值才判障碍
uint8_t USONIC_IsObstacle(void)
{
    uint32_t dist = USONIC_GetDistance();
    if(dist > 0 && dist < USONIC_OBSTACLE_CM)
        return 1;               // 前方近距离有障碍物
    return 0;                   // 安全（含空旷无回波）
}

// 错误码：中断方案无明确的逐次错误码，始终视为已就绪
uint8_t USONIC_GetLastError(void)
{
    return 0;
}

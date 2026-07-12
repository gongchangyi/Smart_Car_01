#include "main.h"
#include "lcd.h"
#include "beep.h"

// 蓝牙遥控 - 8个指令控制4个轮子 + 超声波避障（V3:超声波测距+绕行避障）

// 全局变量：记录当前运动状态
static uint8_t g_current_cmd = 0;  // 当前运动命令
static uint8_t g_user_speed  = MOTOR_SPEED_DEFAULT;  // 用户设定的速度(0~100)
static uint8_t g_show_image  = 0;   // 0=状态界面, 1=图片模式(发O切图片, 发P切回来)

// 动图播放状态 (发A开始, 发B/P返回; 与图片模式互斥)
static uint8_t g_play_anim     = 0;   // 0=非动图, 1=播放动图
static uint8_t g_anim_frame    = 0;   // 当前播放帧索引
static uint32_t g_anim_last_ms = 0;   // 上一帧切换时间(ms)
#define ANIM_FRAME_MS   150            // 每帧间隔(ms), 越小播放越快

#ifdef USE_ANIM
static uint8_t g_play_anim     = 0;   // 0=非动图, 1=播放动图(发G开始, 发P停止)
static uint8_t g_anim_frame    = 0;   // 当前播放帧索引
static uint32_t g_anim_last_ms = 0;   // 上一帧切换时间(ms)
#define ANIM_FRAME_MS   150            // 每帧间隔(ms), 越小播放越快
#endif

// 系统毫秒计数器（由 SysTick 中断自增，定义于 delay.c）
extern volatile uint32_t g_ms_tick;
#define USONIC_MEAS_INTERVAL   100  // 超声波最小测量间隔(ms)，须大于模块死区+最长测距耗时

// 避障消抖计数器
static uint8_t g_obstacle_count = 0;      // 连续检测到障碍物的次数
#define USONIC_DEBOUNCE_THRESHOLD  2       // 连续2次确认才触发避障（超声波本身较稳定，不需要太多滤波）
#define AVOID_SPIN_TIMEOUT        150      // 遇障最大旋转次数(150*20ms≈3s)，超时则停车等待

// 停止所有电机（互斥复位全部方向引脚，避免H桥直通）
static void All_Motor_Stop(void)
{
    Motor_Single(1, 0);
    Motor_Single(2, 0);
    Motor_Single(3, 0);
    Motor_Single(4, 0);
    Motor_Single(5, 0);
    Motor_Single(6, 0);
    Motor_Single(7, 0);
    Motor_Single(8, 0);
}

// 执行运动命令（每次先清全部方向脚再置目标，保证互斥）
static void Execute_Motion(uint8_t cmd)
{
    // 先停止所有电机，确保方向引脚不会同时置高（H桥直通保护）
    All_Motor_Stop();

    switch(cmd)
    {
        case '1':  // 后退
            Motor_Single(1, 1);   //右后逆时针
            Motor_Single(4, 1);   //左后逆时针
            Motor_Single(5, 1);   //右前逆时针
            Motor_Single(8, 1);   //左前逆时针
            break;

        case '2':  // 停止
        case '4':  // 停止
            // 已在上面All_Motor_Stop处理
            break;

        case '3':  // 前进
            Motor_Single(2, 1);    //右后顺时针
            Motor_Single(3, 1);    //左后顺时针
            Motor_Single(6, 1);    //右前顺时针
            Motor_Single(7, 1);    //左前顺时针
            break;

        case '5':  // 左转圈（原地左转）：左侧轮子逆时针转（后退），右侧轮子顺时针转（前进）
            Motor_Single(4, 1); // 左后逆 (后退)
            Motor_Single(8, 1); // 左前逆 (后退)
            Motor_Single(2, 1); // 右后顺 (前进)
            Motor_Single(6, 1); // 右前顺 (前进)
            break;

        case '6':  // 右转圈（原地右转）：左侧轮子顺时针转（前进），右侧轮子逆时针转（后退）
            Motor_Single(1, 1); // 右后逆 (后退)
            Motor_Single(5, 1); // 右前逆 (后退)
            Motor_Single(3, 1); // 左后顺 (前进)
            Motor_Single(7, 1); // 左前顺 (前进)
            break;

        case '7':  // 左转：右侧轮子顺时针转（前进），左侧不转
            Motor_Single(2, 1);  // 右后顺（前进）
            Motor_Single(6, 1);  // 右前顺（前进）
            // 左侧保持停止
            break;

        case '8':  // 右转：左侧轮子顺时针转（前进），右侧不转
            Motor_Single(3, 1);  // 左后顺（前进）
            Motor_Single(7, 1);  // 左前顺（前进）
            // 右侧保持停止
            break;
    }
}

// 根据当前速度动态计算避障判定距离（速度越快，越早判障、留出更多刹车/转向余量）
// 线性映射：阈值(cm) = 30 + speed/2，并钳位到 [60, 85]
//   speed=0~60 -> 60cm（低速也保持够用的判障距离，不会太短）
//   speed=70   -> 65cm（默认速度，经实测反应合适）
//   speed=100  -> 80cm（满速，提前避障）
#define OBSTACLE_MIN_CM   60      // 低速/中速时的最小判障距离（避免判障过短）
#define OBSTACLE_MAX_CM   85      // 满速时的最大判障距离
static uint16_t Obstacle_Threshold_CM(void)
{
    uint16_t th = (uint16_t)(30 + g_user_speed / 2);  // 0->30, 70->65, 100->80
    if(th < OBSTACLE_MIN_CM) th = OBSTACLE_MIN_CM;    // 低速不低于60cm
    if(th > OBSTACLE_MAX_CM) th = OBSTACLE_MAX_CM;
    return th;
}

// 前方是否被障碍物挡住（用动态阈值判断；dist=0 视为空旷/未就绪=畅通）
static uint8_t Front_Blocked(void)
{
    uint32_t d = USONIC_GetDistance();
    return (d > 0 && d < Obstacle_Threshold_CM()) ? 1 : 0;
}

// 避障绕行动作：持续旋转直到前方畅通（带超时），返回恢复后的运动命令
// 超声波为单点测距，无方向信息，统一向右旋转避障
static uint8_t Avoid_Obstacle(void)
{
    uint16_t spin = 0;    // 旋转计数（超时保护）

    // 持续右转，直到前方无障碍（车头已朝向开阔处）或超时
    Bluetooth_SendString("AVOID:SPIN\r\n");
    Execute_Motion('8');   // 右转

    while(Front_Blocked() && (spin < AVOID_SPIN_TIMEOUT))
    {
        Delay_nop_nms(20);
        spin++;
    }
    All_Motor_Stop();

    if(spin >= AVOID_SPIN_TIMEOUT)
    {
        // 旋转很久仍前方有障碍（宽墙/封闭空间），停车等待新指令
        Bluetooth_SendString("AVOID:STUCK\r\n");
        return '2';       // 停止
    }

    // 前方已畅通，恢复前进
    return '3';
}

// ===================== LCD 状态显示 =====================
// 屏幕布局 (ST7735 128x160):
//   第一行 : 郑州轻工业大学        (白)
//   第二行 : 电源电压 VOLT:x.xV     (白)
//   第三行 : 行驶速度 SPEED:xx%     (绿)
//   第四行 : 行驶状态 前进/后退/停止/左转/右转 (黄)

// 板上无 ADC 采样电池电压, 先用占位值; 后续接分压电阻+ADC 后替换
#define LCD_BATT_PLACEHOLDER  "7.4V"

// 第一行标题: 7 个汉字居中 (每字16px, x=(128-7*16)/2=8)
static void LCD_DrawTitle(void)
{
    uint8_t x = 8;
    uint8_t idx[7] = { HZ_ZHENG, HZ_ZHOU, HZ_QING, HZ_GONG, HZ_YE, HZ_DA, HZ_XUE };
    uint8_t i;
    for (i = 0; i < 7; i++)
    {
        LCD_ShowHz(x, 8, idx[i], LCD_WHITE, LCD_BLACK);
        x += 16;
    }
}

// 静态信息: 学号 + 姓名 (一次性绘制, 不随状态变化)
static void LCD_DrawInfo(void)
{
    // 学号 542307010327 (12位数字, 8x16, 居中 x=(128-12*8)/2=16)
    LCD_ShowString(16, 108, "542307010327", LCD_CYAN, LCD_BLACK);
    // 姓名 张毅 (2个汉字16x16, 居中 x=(128-2*16)/2=48)
    LCD_ShowHz(48,      132, HZ_ZHANG, LCD_CYAN, LCD_BLACK);
    LCD_ShowHz(48 + 16, 132, HZ_YI,    LCD_CYAN, LCD_BLACK);
}

// 第二行: 电源电压 (占位值, 待接入 ADC)
static void LCD_ShowVoltage(uint8_t x, uint8_t y)
{
    char buf[20];
    int i = 0;
    const char* p = "VOLT:";
    while (*p) buf[i++] = *p++;
    p = LCD_BATT_PLACEHOLDER;
    while (*p) buf[i++] = *p++;
    buf[i] = '\0';
    LCD_ShowString(x, y, buf, LCD_WHITE, LCD_BLACK);
}

// 第三行: 速度百分比
static void LCD_ShowSpeed(uint8_t x, uint8_t y, uint8_t spd)
{
    char buf[20];
    int i = 0;
    const char* p = "SPEED:";
    while (*p) buf[i++] = *p++;
    if (spd >= 100) { buf[i++]='1'; buf[i++]='0'; buf[i++]='0'; }
    else            { buf[i++] = '0' + spd/10; buf[i++] = '0' + spd%10; }
    buf[i++] = '%';
    buf[i] = '\0';
    LCD_ShowString(x, y, buf, LCD_GREEN, LCD_BLACK);
}

// 第四行: 行驶状态 (中文两字)
static void LCD_ShowStatus(uint8_t x, uint8_t y, uint8_t cmd)
{
    uint8_t a = HZ_TING, b = HZ_ZHI;   // 默认: 停止
    switch (cmd)
    {
        case '1': a = HZ_HOU;  b = HZ_TUI;   break;  // 后退
        case '3': a = HZ_QIAN; b = HZ_JIN;   break;  // 前进
        case '5':
        case '7': a = HZ_ZUO;  b = HZ_ZHUAN; break;  // 左转 / 左转圈
        case '6':
        case '8': a = HZ_YOU;  b = HZ_ZHUAN; break;  // 右转 / 右转圈
        case '2':
        case '4':
        default: a = HZ_TING;  b = HZ_ZHI;   break;  // 停止
    }
    LCD_ShowHz(x,      y, a, LCD_YELLOW, LCD_BLACK);
    LCD_ShowHz(x + 16, y, b, LCD_YELLOW, LCD_BLACK);
}

// 刷新动态三行 (电压/速度/状态); 始终重绘, 保证返回时能完整覆盖旧画面
static void LCD_UpdateStatus(void)
{
    LCD_ShowVoltage(4, 36);
    LCD_ShowSpeed(4, 60, g_user_speed);
    LCD_ShowStatus(48, 84, g_current_cmd);
}

int main(void)
{
    // 中断优先级分组
    NVIC_SetPriorityGrouping(5);

    // 关闭JTAG，保留SWD调试口
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    // SysTick初始化
    Systick_Init(72000);

    // 初始化蓝牙
    Bluetooth_Init();

    // 等待系统稳定
    Delay_nop_nms(500);

    // 初始化电机
    Motor_Init();

    // 初始化蜂鸣器（PB15，避障提示音）
    BEEP_Init();

    // 初始化超声波避障传感器
    USONIC_Init();

    // 初始化 1.8寸 TFT LCD (ST7735, SPI2)
    LCD_Init();

    // 诊断：打印 ECHO(PC6) 初始化后的实时电平，帮助判断硬件是否回波
    //   0 = 引脚常低（模块未回波/未工作）
    //   1 = 引脚被拉高（5V 电平问题，但 PB14/PC6 为 FT 引脚可耐受 5V）
    Bluetooth_SendString("ECHO_LVL:");
    Bluetooth_SendByte(GPIO_ReadInputDataBit(USONIC_ECHO_PORT, USONIC_ECHO_PIN) ? '1' : '0');
    Bluetooth_SendString(" (TRIG=PB14, ECHO=PC6)\r\n");

    // 诊断：DWT 计时器自检结果（1=可用精确计时, 0=已退回nop计时）
    Bluetooth_SendString("DWT:");
    Bluetooth_SendString(USONIC_DWT_OK() ? "OK\r\n" : "FALLBACK\r\n");

    // 诊断：验证 TRIG(PB14) 输出是否可控（翻转后读回应变化）
    GPIO_SetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN);
    Bluetooth_SendString("TRIG_T:");
    Bluetooth_SendByte(GPIO_ReadOutputDataBit(USONIC_TRIG_PORT, USONIC_TRIG_PIN) ? '1' : '0');
    GPIO_ResetBits(USONIC_TRIG_PORT, USONIC_TRIG_PIN);
    Bluetooth_SendString("\r\n");

    // 发送启动信息 (V4: 带图片功能, 用于确认新固件是否烧录成功)
    Bluetooth_SendString("BOOT V6 ANIM-OK\r\n");

    // 绘制显示屏首屏 (标题 + 学号/姓名 + 当前状态)
    LCD_DrawTitle();
    LCD_DrawInfo();
    LCD_UpdateStatus();

    // 主循环
    while(1)
    {
        // 获取蓝牙命令
        uint8_t cmd = Bluetooth_GetCommand();

        if(cmd != 0)
        {
            // 回传收到的命令
            Bluetooth_SendString("CMD:");
            Bluetooth_SendByte(cmd);
            Bluetooth_SendString("\r\n");

            // 显示控制命令: 'O' 静态图, 'A' 动图, 'P' 返回(静图不清屏), 'B'/'C' 清屏返回
            if (cmd == 'O')
            {
                g_show_image = 1;
                g_play_anim  = 0;
                LCD_ShowImage();
                Bluetooth_SendString("LCD:IMAGE\r\n");
            }
            else if (cmd == 'A')   // 先清屏, 再播放动图
            {
                if (LCD_AnimFrameCount() == 0)
                {
                    Bluetooth_SendString("LCD:NO-ANIM\r\n");
                }
                else
                {
                    g_show_image   = 0;
                    g_play_anim    = 1;
                    g_anim_frame   = 0;
                    g_anim_last_ms = g_ms_tick;
                    LCD_Clear(LCD_BLACK);     // 先清空显示屏
                    LCD_ShowFrame(0);         // 显示第一帧
                    Bluetooth_SendString("LCD:ANIM\r\n");
                }
            }
            else if (cmd == 'P')   // 返回状态页(静图模式不清屏; 若来自动图则清残留)
            {
                uint8_t was_anim = g_play_anim;
                g_show_image = 0;
                g_play_anim  = 0;
                if (was_anim) LCD_Clear(LCD_BLACK);
                LCD_DrawTitle();
                LCD_DrawInfo();
                LCD_UpdateStatus();
                Bluetooth_SendString("LCD:STATUS\r\n");
            }
            else if (cmd == 'B' || cmd == 'C')   // 清空显示屏并显示信息页
            {
                g_show_image = 0;
                g_play_anim  = 0;
                LCD_Clear(LCD_BLACK);
                LCD_DrawTitle();
                LCD_DrawInfo();
                LCD_UpdateStatus();
                Bluetooth_SendString("LCD:STATUS\r\n");
            }
            // 速度调节命令（'9'加速 / '0'减速）
            if(cmd == '9' || cmd == '0')
            {
                int ns = g_user_speed + (cmd == '9' ? 10 : -10);
                if(ns > 100) ns = 100;
                if(ns < 0)   ns = 0;
                g_user_speed = (uint8_t)ns;
                Motor_SetSpeed(g_user_speed);
                // 关键：用新速度立即刷新当前正在执行的动作（否则 CCR 不更新，调速无效）
                if(g_current_cmd >= '1' && g_current_cmd <= '8')
                {
                    Execute_Motion(g_current_cmd);
                }
            }
            else
            {
                // 更新当前运动状态
                g_current_cmd = cmd;
                g_obstacle_count = 0;  // 新命令重置消抖计数器

                // 执行运动命令
                Execute_Motion(cmd);
            }

            // 回传当前速度
            Bluetooth_SendString("SPEED:");
            if(g_user_speed >= 100) Bluetooth_SendString("100");
            else
            {
                Bluetooth_SendByte(g_user_speed / 10 + '0');
                Bluetooth_SendByte(g_user_speed % 10 + '0');
            }
            Bluetooth_SendString("%\r\n");

            // 刷新显示屏状态行 (速度/状态随命令变化, 图片/动图模式下跳过)
            if (!g_show_image && !g_play_anim) LCD_UpdateStatus();
        }

        // 动图播放（非阻塞, 按帧间隔切换; 不阻塞蓝牙接收与避障）
        if (g_play_anim)
        {
            uint32_t now = g_ms_tick;
            if ((now - g_anim_last_ms) >= ANIM_FRAME_MS)
            {
                g_anim_last_ms = now;
                g_anim_frame = (g_anim_frame + 1) % LCD_AnimFrameCount();
                LCD_ShowFrame(g_anim_frame);
            }
        }

        // 超声波周期测距（所有状态都测，间隔 >= 100ms 保护模块死区）
        // 这样即使车静止不动，也能从串口看到 DIST 读数，方便验证硬件是否工作
        {
            static uint32_t last_meas_ms = 0;
            static uint32_t last_dist   = USONIC_MAX_CM + 1;  // 上一次有效距离缓存，初始视为超远(安全)
            static uint8_t  meas_count  = 0;                   // 用于 1Hz 节流打印
            uint32_t now = g_ms_tick;

            if((now - last_meas_ms) >= USONIC_MEAS_INTERVAL)
            {
                last_meas_ms = now;

                // 单次测距（结果仅用于避障判断，不再经蓝牙打印，避免刷屏）
                uint32_t dist = USONIC_GetDistance();

                // 关键：仅当本次测距成功(dist>0)才更新有效距离缓存。
                // 偶发无回波时沿用上次有效值，避免漏判障碍导致撞墙。
                if(dist > 0)
                {
                    last_dist = dist;
                }

                // 1Hz 节流距离回显：每 10 次测量(约1秒)打印一次，方便确认模块是否回波
                if(++meas_count >= 10)
                {
                    meas_count = 0;
                    Bluetooth_SendString("DIST:");
                    if(dist == 0)
                    {
                        // 无回波：再次用缓存值提示，并附错误码
                        Bluetooth_SendString("NORSP(cache=");
                        if(last_dist > USONIC_MAX_CM) Bluetooth_SendString("?");
                        else
                        {
                            Bluetooth_SendByte('0' + last_dist / 100);
                            Bluetooth_SendByte('0' + (last_dist / 10) % 10);
                            Bluetooth_SendByte('0' + last_dist % 10);
                        }
                        Bluetooth_SendString(") e");
                        Bluetooth_SendByte('0' + USONIC_GetLastError());
                        Bluetooth_SendString("\r\n");
                    }
                    else
                    {
                        Bluetooth_SendByte('0' + dist / 100);
                        Bluetooth_SendByte('0' + (dist / 10) % 10);
                        Bluetooth_SendByte('0' + dist % 10);
                        Bluetooth_SendString("cm\r\n");
                    }
                }

                // 仅在前进/转圈状态下做障碍判断（用缓存的有效距离）
                if(g_current_cmd == '3' || g_current_cmd == '5' || g_current_cmd == '6')
                {
                    // 判断障碍：有效距离 < 动态阈值才触发（阈值随速度增大）
                    if(last_dist > 0 && last_dist < Obstacle_Threshold_CM())
                    {
                        g_obstacle_count++;
                        if(g_obstacle_count >= USONIC_DEBOUNCE_THRESHOLD)
                        {
                            Bluetooth_SendString("WARN:OBSTACLE\r\n");
                            BEEP_Beep(300);   // 发现障碍：蜂鸣器提示 300ms

                            // 执行绕行避障，获取恢复后的命令
                            g_current_cmd = Avoid_Obstacle();

                            // 关键修复：恢复前进/恢复运动必须真正执行，
                            // 否则车会停在 Avoid_Obstacle 内的 All_Motor_Stop 状态，
                            // 表现为"右转到位后直接停止、面向空旷也不前进"
                            Execute_Motion(g_current_cmd);

                            // 重置消抖计数器
                            g_obstacle_count = 0;
                        }
                    }
                    else
                    {
                        // 本次未检测到障碍物（或空旷无回波），清零计数器
                        g_obstacle_count = 0;
                    }
                }
            }
        }

        // 延时
        Delay_nop_nms(20);
    }
}

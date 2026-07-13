#include "main.h"
#include "lcd.h"
#include "beep.h"
#include "sensor.h"          // 红外循迹(5路: PC7~PC11)

// 蓝牙遥控 - 8个指令控制4个轮子 + 超声波避障（V3:超声波测距+绕行避障）

// 全局变量：记录当前运动状态
static uint8_t g_current_cmd = 0;  // 当前运动命令
static uint8_t g_user_speed  = MOTOR_SPEED_DEFAULT;  // 用户设定的速度(0~100)
static uint8_t g_show_image  = 0;   // 0=状态界面, 1=图片模式(发O切图片, 发P切回来)

// 红外循迹模式（'H' 进入，收到运动指令'1'~'8'或紧急停止退出）
static uint8_t  g_track_mode      = 0;   // 0=非循迹, 1=循迹模式
static uint8_t  g_track_last      = 0;   // 上次循迹动作(避免每轮重复重启电机)
static uint8_t  g_track_lost      = 0;   // 丢线标志: 五灯全灭/全亮后正在旋转搜索黑线
static uint8_t  g_track_alarm     = 0;   // 已报警标志: 避免 NO-LINE 重复刷屏
static uint32_t g_track_lost_ms   = 0;   // 进入搜索的时刻(g_ms_tick), 用于超时判定

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
// 避障：判定成功后先停，再以 AVOID_SPIN_SPEED 原地转圈（右顺+左逆=cmd'5'）找空当；
// 转圈中前方一畅通就直接前进；转满一圈仍被挡则后退一点换位置再转；总超时则停车。
#define AVOID_SPIN_SPEED       40      // 避障转圈/后退时的临时速度(%)（用户要求40%）
#define TRACK_SPEED            33      // 循迹正常行驶速度(%)（用户要求33%，慢一点方便判定方向）
#define TRACK_SWING_SPEED      30      // 丢线后原地摆动找线速度(%)
#define TRACK_SEARCH_MS        6000    // 丢线后旋转搜索最长时长(ms, 约3圈); 超时未找到则停车报警等待干预
#define AVOID_SPIN_PER_ROUND   75      // 原地转一圈约需的 20ms 次数(@40%)，转满一圈仍堵则后退换位置
#define AVOID_SPIN_TOTAL       600     // 避障总超时(600*20ms=12s)，超时则停车等待新指令
#define AVOID_RETREAT_MS       300     // 每次后退的时长(ms)，"后退一点点"

// 前向声明：状态页绘制函数定义在文件后部，需先声明供 ReturnToStatusPage 调用
static void LCD_DrawTitle(void);
static void LCD_DrawInfo(void);
static void LCD_UpdateStatus(void);

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

// 进入动图模式(若固件含动图数据); 返回 1=已开启, 0=无动图
static uint8_t StartAnimMode(void)
{
    if (LCD_AnimFrameCount() == 0) return 0;
    g_show_image   = 0;
    g_play_anim    = 1;
    g_anim_frame   = 0;
    g_anim_last_ms = g_ms_tick;
    LCD_Clear(LCD_BLACK);
    LCD_ShowFrame(0);
    return 1;
}

// 退出动图/静图模式, 返回状态信息页
static void ReturnToStatusPage(void)
{
    uint8_t was_anim = g_play_anim;
    g_show_image = 0;
    g_play_anim  = 0;
    if (was_anim) LCD_Clear(LCD_BLACK);
    LCD_DrawTitle();
    LCD_DrawInfo();
    LCD_UpdateStatus();
}

// 根据当前速度动态计算避障判定距离。
// 用户标定（2026-07-12，实测）：20%~70% 区间按速度递增设定转弯反应距离：
//   20%->30  30%->40  40%->50  50%->60  60%->75  70%->90  (cm)
// 20%~50% 段斜率 +1cm/%，50%~70% 段斜率 +1.5cm/%，分段线性插值。
// 其余速度档（<20% 或 >70%）本次暂不调整，沿用旧公式 55+speed 钳位 [55,150]。
// HC-SR04 量程 400cm，余量充足。
#define OBSTACLE_MIN_CM   55      // 低速档(<20%)使用的最小判障距离
#define OBSTACLE_MAX_CM   150     // 高速档(>70%)使用的最大判障距离
static uint16_t Obstacle_Threshold_CM(void)
{
    uint8_t s = g_user_speed;

    // 用户标定区间 20%~70%：分段线性，精确匹配标定点
    if (s >= 20 && s <= 70)
    {
        if      (s <= 30) return (uint16_t)(30 + (s - 20));          // 20~30: +1cm/%
        else if (s <= 40) return (uint16_t)(40 + (s - 30));          // 30~40: +1cm/%
        else if (s <= 50) return (uint16_t)(50 + (s - 40));          // 40~50: +1cm/%
        else if (s <= 60) return (uint16_t)(60 + (s - 50) * 3 / 2);  // 50~60: +1.5cm/%
        else              return (uint16_t)(75 + (s - 60) * 3 / 2);  // 60~70: +1.5cm/%
    }

    // 其他速度档（<20% 或 >70%）：沿用原公式 55+speed，钳位 [55,150]
    uint16_t th = (uint16_t)(55 + s);
    if(th < OBSTACLE_MIN_CM) th = OBSTACLE_MIN_CM;
    if(th > OBSTACLE_MAX_CM) th = OBSTACLE_MAX_CM;
    return th;
}

// 前方是否被障碍物挡住（用动态阈值判断；dist=0 视为空旷/未就绪=畅通）
static uint8_t Front_Blocked(void)
{
    uint32_t d = USONIC_GetDistance();
    return (d > 0 && d < Obstacle_Threshold_CM()) ? 1 : 0;
}

// 避障绕行动作（新方案）：
//   判定成功后先停止 -> 以 40% 速度原地转圈（右侧顺时针+左侧逆时针=cmd'5'）找空当；
//   转圈过程中若前方畅通，直接向前（恢复用户速度）；
//   若转满一圈仍被挡（原地打转找不到空当），则后退一点点换位置，接着转，直到能前进；
//   总超时(12s)仍被挡（封闭/宽墙）则停车等待新指令。
//   超声波为单点正前方测距：转圈时车头扫过 360°，某方向畅通即 !Front_Blocked 即前进。
// 避障期间检查用户是否发来新遥控指令（'1'~'8'）或紧急停止，收到即中断、由用户接管。
static uint8_t Avoid_UserAbort(void)
{
    // 最高优先级：中断已物理停车，这里同步逻辑状态并退出避障
    if (Bluetooth_StopPending())
    {
        Bluetooth_ClearStop();
        g_current_cmd = '2';
        return '2';
    }
    uint8_t c = Bluetooth_GetCommand();
    if (c >= '1' && c <= '8')
    {
        g_current_cmd = c;   // 更新为用户的新指令
        return c;
    }
    return 0;
}

static uint8_t Avoid_Obstacle(void)
{
    uint8_t  user_spd = g_user_speed;   // 记住用户原速度，避障结束恢复
    uint16_t spin = 0;

    // 进入避障前先检查：若已收到停止指令，直接返回停止，绝不启动绕行电机
    if (Bluetooth_StopPending())
    {
        Bluetooth_ClearStop();
        return '2';
    }

    // 1. 先停止
    All_Motor_Stop();

    // 2. 以 40% 速度原地转圈（右顺+左逆 = cmd'5'），找空当
    Motor_SetSpeed(AVOID_SPIN_SPEED);
    Bluetooth_SendString("AVOID:SPIN@40\r\n");
    Execute_Motion('5');                 // 左转圈（原地）

    while (spin < AVOID_SPIN_TOTAL)
    {
        // 用户发指令则立即接管（停止/转向），中断避障
        if (Avoid_UserAbort())
        {
            All_Motor_Stop();
            Motor_SetSpeed(user_spd);    // 恢复用户速度
            return g_current_cmd;
        }

        // 转圈过程中若前方畅通，直接向前（恢复用户速度）
        if (!Front_Blocked())
        {
            All_Motor_Stop();
            Motor_SetSpeed(user_spd);
            Bluetooth_SendString("AVOID:OK\r\n");
            return '3';                  // 前进
        }

        Delay_nop_nms(20);
        spin++;

        // 转满一圈仍被挡：后退一点点换位置，再继续转圈
        if (spin % AVOID_SPIN_PER_ROUND == 0)
        {
            Bluetooth_SendString("AVOID:RETREAT\r\n");
            Execute_Motion('1');         // 后退
            Delay_nop_nms(AVOID_RETREAT_MS);
            All_Motor_Stop();
            Execute_Motion('5');         // 继续转圈
        }
    }

    // 总超时仍被挡（封闭/宽墙），停车等待新指令
    All_Motor_Stop();
    Motor_SetSpeed(user_spd);
    Bluetooth_SendString("AVOID:STUCK\r\n");
    return '2';                          // 停止
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

    // 初始化蜂鸣器（PA15，避障提示音）
    BEEP_Init();

    // 初始化超声波避障传感器
    USONIC_Init();

    // 初始化红外循迹传感器(5路: PC7~PC11)
    Sensor_Init();

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
        // ===== 紧急停止（最高优先级）=====
        // 串口中断收到 '2'/'4' 时已立即物理停车；这里同步逻辑状态，
        // 确保避障/恢复等任何逻辑都不会再把电机启动起来。
        if(Bluetooth_StopPending())
        {
            Bluetooth_ClearStop();
            Motor_Stop();               // 再次确保停车
            g_current_cmd   = '2';
            g_obstacle_count = 0;
            g_track_mode    = 0;        // 紧急停止同时退出循迹
            g_track_lost    = 0;        // 清丢线搜索状态
            BEEP_Off();                 // 关闭丢线持续报警
            if(!g_show_image && !g_play_anim) LCD_UpdateStatus();
        }

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
            else if (cmd == 'H')   // 进入红外循迹模式
            {
                g_track_mode      = 1;
                g_track_lost      = 0;   // 进入循迹清掉上次的丢线/报警状态
                BEEP_Off();
                g_track_last      = 0;
                g_user_speed      = TRACK_SPEED;   // 循迹默认33%，仅'm'/'n'微调±1%('9'/'0'大调在循迹下忽略)
                Motor_SetSpeed(TRACK_SPEED);
                g_current_cmd     = '3';
                Execute_Motion('3');
                Bluetooth_SendString("TRACK:ON\r\n");
            }
            // 速度微调: 'm'减1% / 'n'加1% (单字符, 循迹/手动模式下均生效)
            else if (cmd == 'm' || cmd == 'n')
            {
                int ns = g_user_speed + (cmd == 'n' ? 1 : -1);
                if (ns > 100) ns = 100;
                if (ns < 0)   ns = 0;
                g_user_speed = (uint8_t)ns;
                Motor_SetSpeed(g_user_speed);
                Bluetooth_SendString("SPD:");
                Bluetooth_SendByte('0' + g_user_speed / 10);
                Bluetooth_SendByte('0' + g_user_speed % 10);
                Bluetooth_SendString("%\r\n");
                if (!g_show_image && !g_play_anim) LCD_UpdateStatus();
            }
            // 速度调节命令（'9'加速 / '0'减速）：循迹模式下固定速度, 大调忽略(仅'm'/'n'微调生效)
            else if (cmd == '9' || cmd == '0')
            {
                if (g_track_mode)
                {
                    Bluetooth_SendString("TRACK:FIXED-SPD\r\n");
                }
                else
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
            }
            // 运动/其他指令：若在循迹模式且为运动指令(1~8)，先退出循迹让指令立即生效
            else if (cmd >= '1' && cmd <= '8')
            {
                if (g_track_mode)
                {
                    g_track_mode = 0;
                    Bluetooth_SendString("TRACK:OFF\r\n");
                }
                g_current_cmd = cmd;
                g_obstacle_count = 0;  // 新命令重置消抖计数器
                Execute_Motion(cmd);
            }
            // 其他未识别字符：忽略

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

        // ===== 红外循迹控制（'H' 模式）=====
        // 灯编号(左->右): 1=PC7(最左) 2=PC8(左) 3=PC9(中) 4=PC10(右) 5=PC11(最右)
        // 位含义(Sensor_ReadBits): bit=1 表示 灯亮=压到黑线; bit=0 表示 灯灭=白面
        // 直行条件: 灯2 与 灯4 同态(都亮 或 都灭) -> 方向正确, 直行
        //   - 即用户要求: 灯3灭 且 (灯2亮&灯4亮 或 灯2灭&灯4灭) 时继续直行
        //   - 也覆盖 灯3亮 且 灯2/灯4同态 的标准居中情形
        // 纠偏: 灯2/灯4 不同态 -> 用弧线前进(边走边转)纠正, 方向修正如下:
        if (g_track_mode)
        {
            uint8_t b  = Sensor_ReadBits();          // bit0=最左..bit4=最右, 1=灯灭(压黑线), 0=灯亮(白面)
            uint8_t s2 = (b & 0x02) ? 1 : 0;         // 灯2(左)
            uint8_t s4 = (b & 0x08) ? 1 : 0;         // 灯4(右)

            if (b == 0 || b == 0x1F)
            {
                // ===== 五灯全亮(b==0,全在白面丢线) 或 五灯全灭(b==0x1F,全压黑块): 原地旋转搜索黑线 =====
                // 策略: 持续原地转圈扫描, 直到扫到黑线(b 出现有效亮灯)才停; 绝不中途停车放弃。
                // 超过 TRACK_SEARCH_MS 仍未找到 -> 持续蜂鸣器报警作为"仍在搜索"提示, 但继续旋转。
                if (!g_track_lost)
                {
                    // 首次丢失: 立即进入原地旋转搜索
                    g_track_lost    = 1;
                    g_track_lost_ms = g_ms_tick;
                    Motor_SetSpeed(TRACK_SWING_SPEED);
                    Execute_Motion('5');              // 原地左转圈, 扫一圈找黑线
                    g_track_last = '5';               // 标记当前动作为旋转
                    Bluetooth_SendString("TRACK:LOST\r\n");
                    StartAnimMode();                  // 丢线搜索期间播放动图
                }
                else
                {
                    // 搜索中: 在 TRACK_SEARCH_MS 窗口内持续原地旋转, 每轮重设动作防止被打断
                    if ((g_ms_tick - g_track_lost_ms) >= TRACK_SEARCH_MS)
                    {
                        // 搜索超时(10s): 停车 + 持续报警, 等待用户把车放回黑线
                        // 用户放回黑线(b 出现有效亮灯)即自动恢复, 无需重发 'H'
                        if (!g_track_alarm)
                        {
                            All_Motor_Stop();
                            BEEP_On();                // 持续报警(直到放回黑线自动关闭)
                            Bluetooth_SendString("TRACK:NO-LINE\r\n");
                            g_track_alarm = 1;
                            ReturnToStatusPage();    // 超时停车, 返回状态信息页
                        }
                        g_track_last = 0;             // 标记已停车, 防止下方重设旋转
                    }
                    else if (g_track_last != '5')
                    {
                        // 仍在搜索窗口内: 持续旋转扫描黑线
                        Motor_SetSpeed(TRACK_SWING_SPEED);
                        Execute_Motion('5');
                        g_track_last = '5';
                    }
                    // 注意: g_track_lost 保持=1, g_track_mode 保持=1, 不退出循迹
                }
                // 丢失/搜索期间不执行正常循迹逻辑
            }
            else
            {
                // ===== 检测到黑线 =====
                if (g_track_lost)
                {
                    g_track_lost  = 0;                // 已找回黑线, 退出搜索
                    g_track_alarm = 0;                // 清报警标志
                    BEEP_Off();
                    Bluetooth_SendString("TRACK:FOUND\r\n");
                    ReturnToStatusPage();            // 找回黑线, 返回状态信息页
                }

                uint8_t tcmd = '3';
                uint8_t tspd = g_user_speed;          // 直行用当前速度(33%, 可'm'/'n'微调)

                if (s2 == s4)
                {
                    tcmd = '3';                       // 灯2/灯4 同态: 直行
                }
                else
                {
                    // 灯2/灯4 不同态: 弧线前进纠偏
                    // 方向(实测确认): 左灯(灯2)压线=车偏右 -> 左转弧线'7'; 右灯(灯4)压线=车偏左 -> 右转弧线'8'
                    tspd = g_user_speed;
                    if (s2 && !s4)                    // 左灯亮(压线)、右灯灭 -> 左转弧线
                    {
                        tcmd = '7';
                    }
                    else                             // 右灯亮(压线)、左灯灭 -> 右转弧线
                    {
                        tcmd = '8';
                    }
                }

                // 状态变化才重启电机（同一动作不重复 All_Motor_Stop）
                if (tcmd != g_track_last)
                {
                    Motor_SetSpeed(tspd);
                    Execute_Motion(tcmd);
                    g_track_last = tcmd;
                }
                g_current_cmd = tcmd;   // 供超声波避障判断(前进'3'时遇障触发避障)
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
                // 循迹模式(测试黑线)下关闭避障，避免误触发打断循迹
                if(!g_track_mode && (g_current_cmd == '3' || g_current_cmd == '5' || g_current_cmd == '6'))
                {
                    // 判断障碍：有效距离 < 动态阈值才触发（阈值随速度增大）
                    if(last_dist > 0 && last_dist < Obstacle_Threshold_CM())
                    {
                        g_obstacle_count++;
                        if(g_obstacle_count >= USONIC_DEBOUNCE_THRESHOLD)
                        {
                            Bluetooth_SendString("WARN:OBSTACLE\r\n");
                            BEEP_Beep(100);   // 发现障碍：蜂鸣器短提示 100ms（缩短以尽快起步转弯，减少反应延迟内的冲撞距离）

                            // 执行绕行避障，获取恢复后的命令
                            g_current_cmd = Avoid_Obstacle();

                            // 循迹模式下若避障超时停车，退出循迹等待新指令（避免反复前进撞墙）
                            if (g_track_mode && g_current_cmd == '2')
                            {
                                g_track_mode = 0;
                                Bluetooth_SendString("TRACK:OFF\r\n");
                            }

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

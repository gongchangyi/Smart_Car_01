#ifndef _LCD_H_
#define _LCD_H_

#include "stm32f10x.h"

// ============================================
// 1.8寸 TFT LCD (ST7735, SPI) 引脚
// 依据原理图，已避开电机(PB0/1/6/7/8/9,PA6/7)、超声波(PB14,PC6)、蓝牙(PA2/3)
// ============================================

#define LCD_CS_PORT     GPIOA
#define LCD_CS_PIN      GPIO_Pin_5     // PA5  片选(软件控制)

#define LCD_DC_PORT     GPIOA
#define LCD_DC_PIN      GPIO_Pin_12    // PA12 数据/命令

#define LCD_RES_PORT    GPIOB
#define LCD_RES_PIN     GPIO_Pin_12    // PB12 复位

// 背光 LCD_BACK_LIGHT = PA4, 软件控制(拉高点亮)
#define LCD_BL_PORT     GPIOA
#define LCD_BL_PIN      GPIO_Pin_4

// 注: 实际板卡背光为 PA4 (与蓝牙RX的PA3不冲突)，可正常软件控制；
//     之前按原理图标 PA3 是错的，已按实测更正。

// 颜色 (RGB565)
#define LCD_BLACK   0x0000
#define LCD_BLUE    0x001F
#define LCD_RED     0xF800
#define LCD_GREEN   0x07E0
#define LCD_CYAN    0x07FF
#define LCD_YELLOW  0xFFE0
#define LCD_WHITE   0xFFFF

#define LCD_WIDTH    128
#define LCD_HEIGHT   160

// 汉字索引 (对应 lcd_font.h 的 HZ_LIB)
#define HZ_ZHENG 0
#define HZ_ZHOU  1
#define HZ_QING  2
#define HZ_GONG  3
#define HZ_YE    4
#define HZ_DA    5
#define HZ_XUE   6
#define HZ_QIAN  7
#define HZ_JIN   8
#define HZ_HOU   9
#define HZ_TUI   10
#define HZ_TING  11
#define HZ_ZHI   12
#define HZ_ZUO   13
#define HZ_ZHUAN 14
#define HZ_YOU   15
#define HZ_ZHANG 16
#define HZ_YI    17
#define HZ_SHI   18
#define HZ_CHANG 19
#define HZ_ZHU   20
#define HZ_ZHI   21
#define HZ_PENG  22

// 函数声明
void LCD_Init(void);
void LCD_Clear(uint16_t color);
void LCD_Fill(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint16_t color);
void LCD_DrawPoint(uint8_t x, uint8_t y, uint16_t color);
void LCD_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint16_t fc, uint16_t bc);
void LCD_ShowString(uint8_t x, uint8_t y, const char* str, uint16_t fc, uint16_t bc);
void LCD_ShowHz(uint8_t x, uint8_t y, uint8_t index, uint16_t fc, uint16_t bc);
void LCD_ShowImage(void);  // 全屏显示图片 (128x160 RGB565)
void LCD_ShowFrame(uint8_t idx);   // 显示动图第 idx 帧 (128x160 RGB565)
uint8_t LCD_AnimFrameCount(void);  // 返回动图总帧数

#endif

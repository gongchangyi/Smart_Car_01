#include "lcd.h"
#include "lcd_font.h"
#include "lcd_img.h"
#include "lcd_anim.h"
#include "delay.h"

// ============================================
// 底层 IO / SPI
// ============================================
static void LCD_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);

    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    // CS / DC / 背光 都在 GPIOA 上, 一次性初始化
    gpio.GPIO_Pin = LCD_CS_PIN | LCD_DC_PIN | LCD_BL_PIN;  GPIO_Init(LCD_CS_PORT, &gpio);
    gpio.GPIO_Pin = LCD_RES_PIN; GPIO_Init(LCD_RES_PORT, &gpio);

    GPIO_SetBits(LCD_CS_PORT, LCD_CS_PIN);   // CS 默认高(不选中)
    GPIO_SetBits(LCD_BL_PORT, LCD_BL_PIN);   // 背光点亮(拉高)
}

static void LCD_SPI_Init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    // PB13=SCK, PB15=MOSI -> 复用推挽输出 (SPI2 默认映射)
    gpio.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    spi.SPI_Direction = SPI_Direction_1Line_Tx;   // 只发
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;               // SPI Mode0
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8; // 36M/8 = 4.5MHz
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_Init(SPI2, &spi);
    SPI_Cmd(SPI2, ENABLE);
}

static void LCD_WriteByte(uint8_t dat)
{
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI2, dat);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET);
}

static void LCD_WriteCmd(uint8_t cmd)
{
    GPIO_ResetBits(LCD_DC_PORT, LCD_DC_PIN);
    GPIO_ResetBits(LCD_CS_PORT, LCD_CS_PIN);
    LCD_WriteByte(cmd);
    GPIO_SetBits(LCD_CS_PORT, LCD_CS_PIN);
}

static void LCD_WriteData(uint8_t dat)
{
    GPIO_SetBits(LCD_DC_PORT, LCD_DC_PIN);
    GPIO_ResetBits(LCD_CS_PORT, LCD_CS_PIN);
    LCD_WriteByte(dat);
    GPIO_SetBits(LCD_CS_PORT, LCD_CS_PIN);
}

static void LCD_SetWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    LCD_WriteCmd(0x2A);
    LCD_WriteData(0x00); LCD_WriteData(x0);
    LCD_WriteData(0x00); LCD_WriteData(x1);
    LCD_WriteCmd(0x2B);
    LCD_WriteData(0x00); LCD_WriteData(y0);
    LCD_WriteData(0x00); LCD_WriteData(y1);
    LCD_WriteCmd(0x2C);   // RAMWR
}

// ============================================
// ST7735 初始化序列 (1.8" 128x160)
// ============================================
static void LCD_Init_Seq(void)
{
    GPIO_ResetBits(LCD_RES_PORT, LCD_RES_PIN);
    Delay_nop_nms(20);
    GPIO_SetBits(LCD_RES_PORT, LCD_RES_PIN);
    Delay_nop_nms(20);

    LCD_WriteCmd(0x11); // Sleep out
    Delay_nop_nms(120);

    LCD_WriteCmd(0xB1);
    LCD_WriteData(0x01); LCD_WriteData(0x2C); LCD_WriteData(0x2D);
    LCD_WriteCmd(0xB2);
    LCD_WriteData(0x01); LCD_WriteData(0x2C); LCD_WriteData(0x2D);
    LCD_WriteCmd(0xB3);
    LCD_WriteData(0x01); LCD_WriteData(0x2C); LCD_WriteData(0x2D);
    LCD_WriteData(0x01); LCD_WriteData(0x2C); LCD_WriteData(0x2D);

    LCD_WriteCmd(0xB4);
    LCD_WriteData(0x07);

    LCD_WriteCmd(0xC0);
    LCD_WriteData(0xA2); LCD_WriteData(0x02); LCD_WriteData(0x84);
    LCD_WriteCmd(0xC1);
    LCD_WriteData(0xC5);
    LCD_WriteCmd(0xC2);
    LCD_WriteData(0x0A); LCD_WriteData(0x00);
    LCD_WriteCmd(0xC3);
    LCD_WriteData(0x8A); LCD_WriteData(0x2A);
    LCD_WriteCmd(0xC4);
    LCD_WriteData(0x8A); LCD_WriteData(0xEE);

    LCD_WriteCmd(0xC5);
    LCD_WriteData(0x0E);

    // MADCTL: 显示方向/颜色顺序。竖屏; 若颜色红蓝颠倒改为 0x08
    LCD_WriteCmd(0x36);
    LCD_WriteData(0x00);

    LCD_WriteCmd(0xE0);
    LCD_WriteData(0x0F); LCD_WriteData(0x1A); LCD_WriteData(0x0F); LCD_WriteData(0x18);
    LCD_WriteData(0x2F); LCD_WriteData(0x28); LCD_WriteData(0x20); LCD_WriteData(0x22);
    LCD_WriteData(0x1F); LCD_WriteData(0x1B); LCD_WriteData(0x23); LCD_WriteData(0x37);
    LCD_WriteData(0x00); LCD_WriteData(0x07); LCD_WriteData(0x02); LCD_WriteData(0x10);
    LCD_WriteCmd(0xE1);
    LCD_WriteData(0x0F); LCD_WriteData(0x1B); LCD_WriteData(0x0F); LCD_WriteData(0x17);
    LCD_WriteData(0x33); LCD_WriteData(0x2C); LCD_WriteData(0x29); LCD_WriteData(0x2E);
    LCD_WriteData(0x30); LCD_WriteData(0x30); LCD_WriteData(0x29); LCD_WriteData(0x3A);
    LCD_WriteData(0x00); LCD_WriteData(0x07); LCD_WriteData(0x03); LCD_WriteData(0x10);

    LCD_WriteCmd(0x3A); // 16bit/pixel
    LCD_WriteData(0x05);

    LCD_WriteCmd(0x29); // Display ON
    Delay_nop_nms(20);
}

// ============================================
// 对外绘图接口
// ============================================
void LCD_Init(void)
{
    LCD_GPIO_Init();
    LCD_SPI_Init();
    LCD_Init_Seq();
    LCD_Clear(LCD_BLACK);
}

void LCD_DrawPoint(uint8_t x, uint8_t y, uint16_t color)
{
    LCD_SetWindow(x, y, x, y);
    LCD_WriteData(color >> 8);
    LCD_WriteData(color & 0xFF);
}

void LCD_Fill(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint16_t color)
{
    uint32_t i, n;
    LCD_SetWindow(x0, y0, x1, y1);
    n = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
    for (i = 0; i < n; i++)
    {
        LCD_WriteData(color >> 8);
        LCD_WriteData(color & 0xFF);
    }
}

void LCD_Clear(uint16_t color)
{
    LCD_Fill(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, color);
}

void LCD_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint16_t fc, uint16_t bc)
{
    uint8_t idx = chr - 0x20;
    if (idx >= 95) idx = 0;
    const uint8_t* p = ASCII_LIB[idx];
    uint8_t i, j;
    LCD_SetWindow(x, y, x + 7, y + 15);
    for (i = 0; i < 16; i++)
    {
        uint8_t line = p[i];
        for (j = 0; j < 8; j++)
        {
            uint16_t c = (line & (0x80 >> j)) ? fc : bc;
            LCD_WriteData(c >> 8);
            LCD_WriteData(c & 0xFF);
        }
    }
}

// 全屏显示预存图片 (RGB565, 128x160)
void LCD_ShowImage(void)
{
    uint32_t i;
    LCD_SetWindow(0, 0, IMG_W - 1, IMG_H - 1);
    for (i = 0; i < (uint32_t)IMG_W * IMG_H; i++)
    {
        uint16_t c = g_lcd_img[i];
        LCD_WriteData(c >> 8);
        LCD_WriteData(c & 0xFF);
    }
}

// 显示动图第 idx 帧 (RGB565, 128x160)
void LCD_ShowFrame(uint8_t idx)
{
    if (idx >= LCD_AnimFrameCount()) idx = 0;
    const uint16_t* frame = g_lcd_frames[idx];
    uint32_t i;
    LCD_SetWindow(0, 0, ANIM_W - 1, ANIM_H - 1);
    for (i = 0; i < (uint32_t)ANIM_W * ANIM_H; i++)
    {
        uint16_t c = frame[i];
        LCD_WriteData(c >> 8);
        LCD_WriteData(c & 0xFF);
    }
}

// 返回动图总帧数
uint8_t LCD_AnimFrameCount(void)
{
    return FRAME_COUNT;
}

void LCD_ShowString(uint8_t x, uint8_t y, const char* str, uint16_t fc, uint16_t bc)
{
    uint8_t i = 0;
    while (str[i] != '\0' && (x + 8) <= LCD_WIDTH)
    {
        LCD_ShowChar(x, y, (uint8_t)str[i], fc, bc);
        x += 8;
        i++;
    }
}

void LCD_ShowHz(uint8_t x, uint8_t y, uint8_t index, uint16_t fc, uint16_t bc)
{
    if (index >= (sizeof(HZ_LIB) / sizeof(HZ_LIB[0]))) return;
    const uint8_t* p = HZ_LIB[index];
    uint8_t i, j;
    LCD_SetWindow(x, y, x + 15, y + 15);
    for (i = 0; i < 16; i++)
    {
        uint16_t line = (uint16_t)((p[2 * i] << 8) | p[2 * i + 1]);
        for (j = 0; j < 16; j++)
        {
            uint16_t c = (line & (0x8000 >> j)) ? fc : bc;
            LCD_WriteData(c >> 8);
            LCD_WriteData(c & 0xFF);
        }
    }
}




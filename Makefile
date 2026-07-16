# ===== 工程配置 =====
TARGET      := Smart_Car
BUILD       := build
MCU         := cortex-m3

# ===== 工具链 (arm-none-eabi-gcc) =====
PREFIX      := arm-none-eabi-
CC          := $(PREFIX)gcc
AS          := $(PREFIX)gcc -x assembler-with-cpp
LD          := $(PREFIX)gcc
OBJCOPY     := $(PREFIX)objcopy
SIZE        := $(PREFIX)size
RM          := rm -rf

# ===== 路径（相对当前目录，执行 make 时请 cd 到本目录）=====
USER        := User
API         := User/API
CMSIS       := CMSIS
STARTUP     := StartUp
LIB         := STM32F10x_StdPeriph_Driver
LIBINC      := $(LIB)/inc
LIBSRC      := $(LIB)/src

# ===== 源文件 =====
C_SOURCES   := \
	$(USER)/main.c \
	$(USER)/stm32f10x_it.c \
	$(USER)/system_stm32f10x.c \
	$(API)/delay.c \
	$(API)/motor.c \
	$(API)/sensor.c \
	$(API)/lcd.c \
	$(API)/bluetooth.c \
	$(API)/beep.c \
	$(API)/gimbal.c \
	$(API)/wifi_uart.c \
	$(CMSIS)/core_cm3.c \
	$(wildcard $(LIBSRC)/*.c)

# 注意：GNU 语法启动文件（Keil 自带的 .s 不能直接给 GCC 用）
ASM_SOURCES := $(STARTUP)/startup_stm32f10x_hd_gcc.s

# ===== 头文件 & 宏 =====
INCLUDES    := -I$(USER) -I$(API) -I$(CMSIS) -I$(LIBINC)
DEFINES     := -DSTM32F10X_HD -DUSE_STDPERIPH_DRIVER 

# ===== 编译选项 =====
CFLAGS      := -mcpu=$(MCU) -mthumb -std=c99 -include User/API/compat.h
CFLAGS      += -O2 -g -ffunction-sections -fdata-sections
CFLAGS      += -Wall -Wno-unused
CFLAGS      += $(DEFINES) $(INCLUDES)

AFLAGS      := -mcpu=$(MCU) -mthumb -g $(DEFINES) $(INCLUDES)

LDSCRIPT    := STM32F103RC.ld
LDFLAGS     := -mcpu=$(MCU) -mthumb -T$(LDSCRIPT)
LDFLAGS     += -Wl,-Map=$(BUILD)/$(TARGET).map,--cref
LDFLAGS     += -Wl,--gc-sections
LDFLAGS     += -nostartfiles -specs=nosys.specs -lc -lm

# ===== 对象文件 =====
C_OBJS      := $(addprefix $(BUILD)/, $(notdir $(C_SOURCES:.c=.o)))
A_OBJS      := $(addprefix $(BUILD)/, $(notdir $(ASM_SOURCES:.s=.o)))
OBJS        := $(C_OBJS) $(A_OBJS)

vpath %.c $(dir $(C_SOURCES))
vpath %.s $(dir $(ASM_SOURCES))

.PHONY: all clean flash

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(BUILD)
	$(AS) $(AFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(LD) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# 用 OpenOCD + ST-Link 下载（需先安装 openocd）
flash: $(BUILD)/$(TARGET).bin
	openocd -f openocd.cfg -c "program $< verify reset exit 0x08000000"

clean:
	$(RM) $(BUILD)

/**
  * @file    startup_stm32f10x_hd_gcc.s
  * @brief   GNU 语法启动文件（供 arm-none-eabi-gcc 使用）
  *          对应 STM32F10X_HD 系列 (如 STM32F103RC)
  */
  .syntax unified
  .cpu cortex-m3
  .thumb

  .global g_pfnVectors
  .global Default_Handler

  /* _sidata/_sdata/_edata/_sbss/_ebss/_estack 均由链接脚本 STM32F103RC.ld 提供 */

  .section .text.Reset_Handler
  .weak Reset_Handler
  .type Reset_Handler, %function
Reset_Handler:
  ldr   sp, =_estack          /* 设置栈指针 */

  /* 拷贝 .data 段（从 Flash 到 RAM）*/
  ldr   r0, =_sdata
  ldr   r1, =_edata
  ldr   r2, =_sidata
  b     2f
1:
  ldr   r3, [r2], #4
  str   r3, [r0], #4
2:
  cmp   r0, r1
  bcc   1b

  /* 清零 .bss 段 */
  ldr   r0, =_sbss
  ldr   r1, =_ebss
  mov   r3, #0
  b     4f
3:
  str   r3, [r0], #4
4:
  cmp   r0, r1
  bcc   3b

  bl    SystemInit
  bl    main
5:
  b     5b
  .size Reset_Handler, . - Reset_Handler

  .section .isr_vector,"a",%progbits
  .type g_pfnVectors, %object
  .size g_pfnVectors, .-g_pfnVectors
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word MemManage_Handler
  .word BusFault_Handler
  .word UsageFault_Handler
  .word 0
  .word 0
  .word 0
  .word 0
  .word SVC_Handler
  .word DebugMon_Handler
  .word 0
  .word PendSV_Handler
  .word SysTick_Handler
  /* 外部中断 */
  .word WWDG_IRQHandler
  .word PVD_IRQHandler
  .word TAMPER_IRQHandler
  .word RTC_IRQHandler
  .word FLASH_IRQHandler
  .word RCC_IRQHandler
  .word EXTI0_IRQHandler
  .word EXTI1_IRQHandler
  .word EXTI2_IRQHandler
  .word EXTI3_IRQHandler
  .word EXTI4_IRQHandler
  .word DMA1_Channel1_IRQHandler
  .word DMA1_Channel2_IRQHandler
  .word DMA1_Channel3_IRQHandler
  .word DMA1_Channel4_IRQHandler
  .word DMA1_Channel5_IRQHandler
  .word DMA1_Channel6_IRQHandler
  .word DMA1_Channel7_IRQHandler
  .word ADC1_2_IRQHandler
  .word USB_HP_CAN1_TX_IRQHandler
  .word USB_LP_CAN1_RX0_IRQHandler
  .word CAN1_RX1_IRQHandler
  .word CAN1_SCE_IRQHandler
  .word EXTI9_5_IRQHandler
  .word TIM1_BRK_IRQHandler
  .word TIM1_UP_IRQHandler
  .word TIM1_TRG_COM_IRQHandler
  .word TIM1_CC_IRQHandler
  .word TIM2_IRQHandler
  .word TIM3_IRQHandler
  .word TIM4_IRQHandler
  .word I2C1_EV_IRQHandler
  .word I2C1_ER_IRQHandler
  .word I2C2_EV_IRQHandler
  .word I2C2_ER_IRQHandler
  .word SPI1_IRQHandler
  .word SPI2_IRQHandler
  .word USART1_IRQHandler
  .word USART2_IRQHandler
  .word USART3_IRQHandler
  .word EXTI15_10_IRQHandler
  .word RTCAlarm_IRQHandler
  .word USBWakeUp_IRQHandler
  .word TIM8_BRK_IRQHandler
  .word TIM8_UP_IRQHandler
  .word TIM8_TRG_COM_IRQHandler
  .word TIM8_CC_IRQHandler
  .word ADC3_IRQHandler
  .word FSMC_IRQHandler
  .word SDIO_IRQHandler
  .word TIM5_IRQHandler
  .word SPI3_IRQHandler
  .word UART4_IRQHandler
  .word UART5_IRQHandler
  .word TIM6_IRQHandler
  .word TIM7_IRQHandler
  .word DMA2_Channel1_IRQHandler
  .word DMA2_Channel2_IRQHandler
  .word DMA2_Channel3_IRQHandler
  .word DMA2_Channel4_5_IRQHandler

  .section .text.Default_Handler,"ax",%progbits
  .weak Default_Handler
  .type Default_Handler, %function
Default_Handler:
  b     .
  .size Default_Handler, . - Default_Handler

  .macro IRQHandler name
  .weak \name
  .set  \name, Default_Handler
  .endm

  IRQHandler NMI_Handler
  IRQHandler HardFault_Handler
  IRQHandler MemManage_Handler
  IRQHandler BusFault_Handler
  IRQHandler UsageFault_Handler
  IRQHandler SVC_Handler
  IRQHandler DebugMon_Handler
  IRQHandler PendSV_Handler
  IRQHandler SysTick_Handler
  IRQHandler WWDG_IRQHandler
  IRQHandler PVD_IRQHandler
  IRQHandler TAMPER_IRQHandler
  IRQHandler RTC_IRQHandler
  IRQHandler FLASH_IRQHandler
  IRQHandler RCC_IRQHandler
  IRQHandler EXTI0_IRQHandler
  IRQHandler EXTI1_IRQHandler
  IRQHandler EXTI2_IRQHandler
  IRQHandler EXTI3_IRQHandler
  IRQHandler EXTI4_IRQHandler
  IRQHandler DMA1_Channel1_IRQHandler
  IRQHandler DMA1_Channel2_IRQHandler
  IRQHandler DMA1_Channel3_IRQHandler
  IRQHandler DMA1_Channel4_IRQHandler
  IRQHandler DMA1_Channel5_IRQHandler
  IRQHandler DMA1_Channel6_IRQHandler
  IRQHandler DMA1_Channel7_IRQHandler
  IRQHandler ADC1_2_IRQHandler
  IRQHandler USB_HP_CAN1_TX_IRQHandler
  IRQHandler USB_LP_CAN1_RX0_IRQHandler
  IRQHandler CAN1_RX1_IRQHandler
  IRQHandler CAN1_SCE_IRQHandler
  IRQHandler EXTI9_5_IRQHandler
  IRQHandler TIM1_BRK_IRQHandler
  IRQHandler TIM1_UP_IRQHandler
  IRQHandler TIM1_TRG_COM_IRQHandler
  IRQHandler TIM1_CC_IRQHandler
  IRQHandler TIM2_IRQHandler
  IRQHandler TIM3_IRQHandler
  IRQHandler TIM4_IRQHandler
  IRQHandler I2C1_EV_IRQHandler
  IRQHandler I2C1_ER_IRQHandler
  IRQHandler I2C2_EV_IRQHandler
  IRQHandler I2C2_ER_IRQHandler
  IRQHandler SPI1_IRQHandler
  IRQHandler SPI2_IRQHandler
  IRQHandler USART1_IRQHandler
  IRQHandler USART2_IRQHandler
  IRQHandler USART3_IRQHandler
  IRQHandler EXTI15_10_IRQHandler
  IRQHandler RTCAlarm_IRQHandler
  IRQHandler USBWakeUp_IRQHandler
  IRQHandler TIM8_BRK_IRQHandler
  IRQHandler TIM8_UP_IRQHandler
  IRQHandler TIM8_TRG_COM_IRQHandler
  IRQHandler TIM8_CC_IRQHandler
  IRQHandler ADC3_IRQHandler
  IRQHandler FSMC_IRQHandler
  IRQHandler SDIO_IRQHandler
  IRQHandler TIM5_IRQHandler
  IRQHandler SPI3_IRQHandler
  IRQHandler UART4_IRQHandler
  IRQHandler UART5_IRQHandler
  IRQHandler TIM6_IRQHandler
  IRQHandler TIM7_IRQHandler
  IRQHandler DMA2_Channel1_IRQHandler
  IRQHandler DMA2_Channel2_IRQHandler
  IRQHandler DMA2_Channel3_IRQHandler
  IRQHandler DMA2_Channel4_5_IRQHandler

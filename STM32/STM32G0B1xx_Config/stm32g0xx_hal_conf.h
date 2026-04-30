/**
  ******************************************************************************
  * @file    stm32g0xx_hal_conf.h
  * @brief   HAL configuration for STM32G0B1CBT6 (WeAct USB2CANFDV1)
  ******************************************************************************
  */

#ifndef STM32G0xx_HAL_CONF_H
#define STM32G0xx_HAL_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* ########################## Module Selection ############################## */

#define HAL_MODULE_ENABLED
#define HAL_FDCAN_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED

/* ########################## Oscillator Values ############################# */
/* HSE_VALUE comes from -DHSE_VALUE=$(QUARTZ_FREQU). The WeAct board has a
 * 16 MHz crystal. */
#if !defined(HSE_VALUE) || (HSE_VALUE == 0)
  #undef  HSE_VALUE
  #define HSE_VALUE    16000000U
#endif

#if !defined  (HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    100U
#endif

#if !defined  (HSI_VALUE)
  #define HSI_VALUE             16000000U
#endif

#if !defined  (HSI_STARTUP_TIMEOUT)
  #define HSI_STARTUP_TIMEOUT   5000U
#endif

#if !defined  (HSI48_VALUE)
  #define HSI48_VALUE          48000000U
#endif

#if !defined  (LSI_VALUE)
  #define LSI_VALUE             32000U
#endif

#if !defined  (LSE_VALUE)
  #define LSE_VALUE             32768U
#endif

#if !defined  (LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT   5000U
#endif

/* External I2S clock values are referenced unconditionally by
 * stm32g0xx_hal_rcc_ex.c when computing the peripheral clock frequency for
 * I2S1 / I2S2, even when those peripherals are unused. The numbers below
 * are irrelevant to this firmware (we never call HAL_RCCEx_GetPeriphCLKFreq
 * for I2S); the value matches ST's hal_conf template default. */
#if !defined  (EXTERNAL_I2S1_CLOCK_VALUE)
  #define EXTERNAL_I2S1_CLOCK_VALUE   12288000U
#endif
#if !defined  (EXTERNAL_I2S2_CLOCK_VALUE)
  #define EXTERNAL_I2S2_CLOCK_VALUE   12288000U
#endif

#define VDD_VALUE                3300U
#define TICK_INT_PRIORITY        0U
#define USE_RTOS                 0U
#define PREFETCH_ENABLE          1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE        0U

#define USE_HAL_FDCAN_REGISTER_CALLBACKS  0U
#define USE_HAL_PCD_REGISTER_CALLBACKS    0U

/* ########################## USB Packet Memory ############################# */
/* STM32G0B1 has 2048 bytes of USB packet memory (USB_DRD peripheral, see
 * RM0444). Used by USBD_LL_ConfigurePMA. */
#define USB_PMA_SIZE                 2048U

/* ########################## Assert Selection ############################## */
/* #define USE_FULL_ASSERT    1U */

#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32g0xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32g0xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32g0xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32g0xx_hal_cortex.h"
#endif
#ifdef HAL_FDCAN_MODULE_ENABLED
  #include "stm32g0xx_hal_fdcan.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32g0xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32g0xx_hal_pwr.h"
#endif
#ifdef HAL_PCD_MODULE_ENABLED
  #include "stm32g0xx_hal_pcd.h"
#endif

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line);
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32G0xx_HAL_CONF_H */

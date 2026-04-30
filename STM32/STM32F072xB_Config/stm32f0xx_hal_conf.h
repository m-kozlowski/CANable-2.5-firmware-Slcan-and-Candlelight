/**
  ******************************************************************************
  * @file    stm32f0xx_hal_conf.h
  * @brief   HAL configuration for STM32F072CBT6
  ******************************************************************************
  */

#ifndef STM32F0xx_HAL_CONF_H
#define STM32F0xx_HAL_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* ########################## Module Selection ############################## */

#define HAL_MODULE_ENABLED
#define HAL_CAN_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED

/* ########################## Oscillator Values ############################# */
/* HSE_VALUE is supplied via -DHSE_VALUE=$(QUARTZ_FREQU) from the makefile.
 * F072 needs a non-zero default at preprocess time even when QUARTZ_FREQU=0. */
#if !defined(HSE_VALUE) || (HSE_VALUE == 0)
  #undef  HSE_VALUE
  #define HSE_VALUE    8000000U
#endif

#if !defined  (HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    100U
#endif

#if !defined  (HSI_VALUE)
  #define HSI_VALUE             8000000U
#endif

#if !defined  (HSI_STARTUP_TIMEOUT)
  #define HSI_STARTUP_TIMEOUT   5000U
#endif

#if !defined  (HSI14_VALUE)
  #define HSI14_VALUE          14000000U
#endif

#if !defined  (HSI48_VALUE)
  #define HSI48_VALUE          48000000U
#endif

#if !defined  (LSI_VALUE)
  #define LSI_VALUE             40000U
#endif

#if !defined  (LSE_VALUE)
  #define LSE_VALUE             32768U
#endif

#if !defined  (LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT   5000U
#endif

/* ########################### System Configuration ######################### */
#define VDD_VALUE                3300U
#define TICK_INT_PRIORITY        0U
#define USE_RTOS                 0U
#define PREFETCH_ENABLE          1U
#define INSTRUCTION_CACHE_ENABLE 0U
#define DATA_CACHE_ENABLE        0U

#define USE_HAL_CAN_REGISTER_CALLBACKS  0U
#define USE_HAL_PCD_REGISTER_CALLBACKS  0U

/* ########################## USB Packet Memory ############################# */
/* Size of the USB peripheral's dedicated packet memory area (PMA) in bytes.
 * STM32F072CBT6 has 1024 bytes (RM0091, USB peripheral chapter). Used by
 * USBD_LL_ConfigurePMA in usb_lowlevel.c to detect overflow when assigning
 * endpoint buffers. */
#define USB_PMA_SIZE                 1024U

/* ########################## Assert Selection ############################## */
/* #define USE_FULL_ASSERT    1U */

/* Includes ----------------------------------------------------------------- */
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32f0xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32f0xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32f0xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32f0xx_hal_cortex.h"
#endif
#ifdef HAL_CAN_MODULE_ENABLED
  #include "stm32f0xx_hal_can.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32f0xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32f0xx_hal_pwr.h"
#endif
#ifdef HAL_PCD_MODULE_ENABLED
  #include "stm32f0xx_hal_pcd.h"
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

#endif /* STM32F0xx_HAL_CONF_H */

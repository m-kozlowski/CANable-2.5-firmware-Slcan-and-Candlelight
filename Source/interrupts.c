/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "interrupts.h"
#include "can.h"
#include "led.h"

extern PCD_HandleTypeDef hpcd_USB_FS;

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

// USB IRQ handler. Symbol name comes from settings.h (MCU_USB_IRQ_HANDLER):
// it's USB_LP_IRQHandler on G4 (split LP/HP vectors), USB_IRQHandler on F0
// (single combined vector), or USB_UCPD1_2_IRQHandler on G0 (combined with
// the type-C UCPD controller, which is unused here so the whole vector is
// forwarded to PCD).
void MCU_USB_IRQ_HANDLER(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_FS);
}

#ifdef MCU_USB_HP_IRQ_HANDLER
// G4 has a separate high-priority USB vector for isochronous + double-buffer
// endpoints. We don't use either, but the vector is wired by HAL_PCD's MSP
// init, so handle it identically.
void MCU_USB_HP_IRQ_HANDLER(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_FS);
}
#endif

#ifdef MCU_BXCAN_IRQ_HANDLER
// bxCAN families route every CAN interrupt (Tx mailbox empty, Rx FIFO0/1,
// status change, error) to a single combined vector that's also shared
// with another peripheral on the chip (CEC on F072). The CAN handle is
// owned by can_bxcan.c; we forward via the helper it exports.
void MCU_BXCAN_IRQ_HANDLER(void)
{
    extern void can_irq_handler(void);
    can_irq_handler();
}
#endif

// Handle SysTick interrupt
void SysTick_Handler(void)
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}


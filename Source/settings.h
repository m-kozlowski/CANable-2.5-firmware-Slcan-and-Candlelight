/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Two helper macros because the precompiler is not able to conacatenate a string with a constant
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// ============================================================================================
// The following enums are used for Slcan and Candlelight

typedef enum
{
    false = 0,
    true  = 1,
} bool;

// If command feedback is enabled these error codes are sent to the host.
// This enum is used for Slcan and for Candlelight.
// Slcan sends errors as "#1\r" which means FBK_InvalidCommand.
// Candlelight sends errors with command ELM_ReqGetLastError.
typedef enum // sent as 8 bit
{
    FBK_RetString = 1,            // The reponse has already been sent over USB --> no additional feedback. This is used only internally.
    FBK_Success   = 2,            // Command successfully executed
    // --------------------------
    FBK_InvalidCommand    = '1',  // "#1" = The command is invalid
    FBK_InvalidParameter,         // "#2" = One of the parameters is invalid
    FBK_AdapterMustBeOpen,        // "#3" = The command cannot be executed before opening the adapter
    FBK_AdapterMustBeClosed,      // "#4" = The command cannot be executed after  opening the adapter
    FBK_ErrorFromHAL,             // "#5" = The HAL from ST Microelectronics has reported an error
    FBK_UnsupportedFeature,       // "#6" = The feature is not implemented or not supported by the board
    FBK_TxBufferFull,             // "#7" = Sending is not possible because the buffer is full (only Slcan)
    FBK_BusIsOff,                 // "#8" = Sending is not possible because the processor is blocked in the BusOff state
    FBK_NoTxInSilentMode,         // "#9" = Sending is not possible because the adapter is in Bus Monitoring mode
    FBK_BaudrateNotSet,           // "#:" = Opening the adapter is not possible if no baudrate has been set
    FBK_OptBytesProgrFailed,      // "#;" = Programming the Option Bytes failed
    FBK_ResetRequired,            // "#<" = The user must disconnect and reconnect the USB cable to enter boot mode
    FBK_ParamOutOfRange,          // "#=" = A paramter is outside the valid range
} eFeedback;

// If bus status is BUS_OFF both LED's (green + blue) are permanently ON
// This status is controlled only by hardware
// Slcan sends this in the error report "EXXXXXXXX\r"
typedef enum // sent as 4 bit
{
    BUS_StatusActive     = 0x00, // operational  (must be zero because this is not an error)
    BUS_StatusWarning    = 0x10, // set in can.c (>  96 errors)
    BUS_StatusPassive    = 0x20, // set in can.c (> 128 errors)
    BUS_StatusOff        = 0x30, // set in can.c (> 248 errors)
} eErrorBusStatus;

// If any of these flags is set, both LED's (green + blue) are permanently ON
// These flags are reset after sending them once to the host
// They are set again if the error is still present
// Slcan sends this in the error report "EXXXXXXXX\r"
// Candlelight sends this in a special error packet with a flag (legacy: CAN_ID_Error, ElmüSoft: MSG_Error)
typedef enum // sent as 8 bit
{
    APP_CanRxFail       = 0x01, // the HAL reports an error receiving a CAN packet.
    APP_CanTxFail       = 0x02, // trying to send while in silent mode, while bus off or adaper not open or invalid Tx packet or HAL error
    APP_CanTxOverflow   = 0x04, // a CAN packet could not be sent because the Tx FIFO + buffer are full (mostly because bus is passive).
    APP_UsbInOverflow   = 0x08, // a USB IN packet could not be sent because CAN traffic is faster than USB transfer.
    APP_CanTxTimeout    = 0x10, // a packet in the transmit FIFO was not acknowledged during 500 ms --> abort Tx and clear Tx buffer.
} eErrorAppFlags;

// ============================================================================================
// MCU selection. TARGET_MCU is defined by the makefile (-D$(TARGET_MCU)).
//
// Each chip block below defines a uniform set of MCU_* macros that the rest
// of the firmware uses. No source file outside this header should reference
// chip-specific names (USB_LP_IRQn, FDCAN1_IT0_IRQHandler, USB_DRD_FS, etc.)
// — adding a new chip should be entirely a matter of dropping a new block in
// here. The macros are:
//
//   CAN_FAMILY_FDCAN   - chip has the FDCAN peripheral (CAN FD capable)
//   CAN_FAMILY_BXCAN   - chip has the legacy bxCAN peripheral (classic only)
//   USB_INSTANCE       - PCD_HandleTypeDef.Instance value (USB / USB_DRD_FS)
//   MCU_USB_NVIC_IRQn      - IRQn for HAL_NVIC_SetPriority/EnableIRQ
//   MCU_USB_IRQ_HANDLER    - Handler symbol that interrupts.c defines
//   MCU_USB_HP_NVIC_IRQn   - optional second USB vector (G4 has split LP/HP)
//   MCU_USB_HP_IRQ_HANDLER - paired Handler symbol if the above is defined
//   MCU_FDCAN_NVIC_IRQn    - FDCAN IT0 vector  (FDCAN family only)
//   MCU_FDCAN_IRQ_HANDLER  - FDCAN IT0 Handler (FDCAN family only)
//   MCU_BXCAN_IRQ_HANDLER  - bxCAN combined Handler (bxCAN family only)
//   MCU_FDCAN_CLOCK_HZ     - kernel clock fed into FDCAN, used by can.c to
//                            derive Brp from the requested baudrate
//   MCU_NOMINAL_TQ         - preferred total time-quanta per nominal bit
//                            (chosen so Brp falls in the integer range for
//                            the entire 10k..500k slcan preset family at
//                            this MCU's FDCAN clock; 800k/1M auto-fall back
//                            to a tighter TQ count in can.c)
//   MCU_HAS_PROGRAMMABLE_BOR    - OPTR has BOR_LEV bits (G4)
//   MCU_HAS_PROGRAMMABLE_BOOT0  - OPTR can ignore the BOOT0 pin (G4)
//   MCU_HAS_FLASH_BANK_FIELD    - FLASH_EraseInitTypeDef has Banks (G4)
//                                 (F0 uses PageAddress instead)
//   MCU_HAS_VOLTAGE_SCALING_BOOST  - HAL_PWREx_ControlVoltageScaling exists
//                                    and "boost" mode is needed (G4)
//   MCU_DBG_DEVID_LIST     - comma-separated DBGMCU IDCODE values that this
//                            chip family answers with (used by dfu.c /
//                            system_get_mcu_serie)
//   MCU_DFU_SYSMEM_BASE    - address the ROM bootloader is mapped at (used
//                            by dfu_switch_to_bootloader)
//   FLASH_SIZE             - chip flash size; CMSIS doesn't define this on
//                            F0 so we provide it
// ============================================================================

#if defined(STM32G431xx)
    #include "stm32g4xx.h"
    #include "stm32g4xx_hal.h"
    #define CAN_FAMILY_FDCAN
    #define USB_INSTANCE                USB
    #define MCU_USB_NVIC_IRQn           USB_LP_IRQn
    #define MCU_USB_IRQ_HANDLER         USB_LP_IRQHandler
    #define MCU_USB_HP_NVIC_IRQn        USB_HP_IRQn
    #define MCU_USB_HP_IRQ_HANDLER      USB_HP_IRQHandler
    #define MCU_FDCAN_NVIC_IRQn         FDCAN1_IT0_IRQn
    #define MCU_FDCAN_IRQ_HANDLER       FDCAN1_IT0_IRQHandler
    #define MCU_FDCAN_CLOCK_HZ          160000000U
    #define MCU_NOMINAL_TQ              320U
    #define MCU_HAS_HANDTUNED_DATA_BITRATES  // 8 Mbit needs 50% SP on G431, see can.c
    #define MCU_HAS_PROGRAMMABLE_BOR
    #define MCU_HAS_PROGRAMMABLE_BOOT0
    #define MCU_HAS_FLASH_BANK_FIELD
    #define MCU_HAS_VOLTAGE_SCALING_BOOST
    #define MCU_DFU_SYSMEM_BASE         0x1FFF0000U
#elif defined(STM32G473xx)
    #include "stm32g4xx.h"
    #include "stm32g4xx_hal.h"
    #define CAN_FAMILY_FDCAN
    #define USB_INSTANCE                USB
    #define MCU_USB_NVIC_IRQn           USB_LP_IRQn
    #define MCU_USB_IRQ_HANDLER         USB_LP_IRQHandler
    #define MCU_USB_HP_NVIC_IRQn        USB_HP_IRQn
    #define MCU_USB_HP_IRQ_HANDLER      USB_HP_IRQHandler
    #define MCU_FDCAN_NVIC_IRQn         FDCAN1_IT0_IRQn
    #define MCU_FDCAN_IRQ_HANDLER       FDCAN1_IT0_IRQHandler
    #define MCU_FDCAN_CLOCK_HZ          160000000U
    #define MCU_NOMINAL_TQ              320U
    #define MCU_HAS_HANDTUNED_DATA_BITRATES
    #define MCU_HAS_PROGRAMMABLE_BOR
    #define MCU_HAS_PROGRAMMABLE_BOOT0
    #define MCU_HAS_FLASH_BANK_FIELD
    #define MCU_HAS_VOLTAGE_SCALING_BOOST
    #define MCU_DFU_SYSMEM_BASE         0x1FFF0000U
#elif defined(STM32F072xB)
    #include "stm32f0xx.h"
    #include "stm32f0xx_hal.h"
    #include "can_compat_bxcan.h"
    #define CAN_FAMILY_BXCAN
    #define USB_INSTANCE                USB
    #define MCU_USB_NVIC_IRQn           USB_IRQn
    #define MCU_USB_IRQ_HANDLER         USB_IRQHandler
    // F072 routes every bxCAN interrupt to the CEC_CAN combined vector.
    #define MCU_BXCAN_IRQ_HANDLER       CEC_CAN_IRQHandler
    #define MCU_DFU_SYSMEM_BASE         0x1FFFC800U
    // F0 HAL also lacks a combined "all flash errors" macro; only PGERR
    // and WRPERR are exposed on this family.
    #define FLASH_FLAG_ALL_ERRORS       (FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR)
    // The STM32F072 has only 128 KB flash. Unlike G4, the CMSIS does not define FLASH_SIZE.
    #ifndef FLASH_SIZE
        #define FLASH_SIZE              (128U * 1024U)
    #endif
#else
    #error "TARGET_MCU not defined in makefile"
#endif

// ============================================================================================
// TARGET_BOARD is defined in Makefile

#if defined(Multiboard)
    // MKS Makerbase + Walfront + DSD Tech + Jhoinrch before 2026 use default settings
#elif defined(Jhoinrch)
    // Jhoinrch puts a 25 MHz quartz on all their boards since 2026 (make file defines: QUARTZ_FREQU = 25000000).
#elif defined(OpenlightLabs)
    // OpenlightLabs has the green LED at pin B11
    #define LED_TX_PINS         GPIO_PIN_11
    #define LED_TX_PORTS        GPIOB
#elif defined(OleksiiSolo)
    // Oleksii puts a 8 MHz quartz on the single channel board (make file defines: QUARTZ_FREQU = 8000000).
    #define LED_TX_PINS         GPIO_PIN_5
    #define LED_TX_PORTS        GPIOA
    #define LED_RX_PINS         GPIO_PIN_6
    #define LED_RX_PORTS        GPIOA
    // -------------------
    #define LED_MODE            GPIO_MODE_OUTPUT_PP
    #define LED_ON              GPIO_PIN_SET             // The LED's cathode is connected to ground
    #define LED_OFF             GPIO_PIN_RESET
#elif defined(OleksiiDual)
    // Oleksii puts a 8 MHz quartz on the dual channel board (make file defines: QUARTZ_FREQU = 8000000).
    // The board has 2 CAN connectors and creates 2 Candlelight USB interfaces.
    #define CHANNEL_COUNT       2
    // -------------------      Channel 1:               Channel 2:
    #define CAN_INTERFACES      FDCAN1,                  FDCAN2
    #define CAN_PINS            GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_5 | GPIO_PIN_6 // CANFD Tx, Rx pins
    #define CAN_PORTS           GPIOB,                   GPIOB                   // CANFD Port
    #define CAN_ALTERNATES      GPIO_AF9_FDCAN1,         GPIO_AF9_FDCAN2  // switch pin multiplexer to CAN module
    // -------------------
    #define LED_TX_PINS         GPIO_PIN_5,              GPIO_PIN_3
    #define LED_TX_PORTS        GPIOA,                   GPIOA
    #define LED_RX_PINS         GPIO_PIN_6,              GPIO_PIN_4
    #define LED_RX_PORTS        GPIOA,                   GPIOA
    // -------------------
    #define TERMINATOR_PINS     -1,                      -1  // termination resistor is switched by a manual jumper
    #define TERMINATOR_PORTS    GPIOB,                   GPIOB
    // ---------------------------------------------------------
    #define LED_MODE            GPIO_MODE_OUTPUT_PP
    #define LED_ON              GPIO_PIN_SET             // The LED's cathode is connected to ground
    #define LED_OFF             GPIO_PIN_RESET
#elif defined(F072_Multiboard)
    // FYSETC CANable / generic STM32F072CBT6 board with SN65HVD230 transceiver.
    // CAN_RX = PB8, CAN_TX = PB9, USB on PA11/PA12 (native), 8 MHz HSE crystal.
    // LEDs: LD1 anode = PA0, LD2 anode = PA1, cathodes to GND -> active high.
    // Termination resistor is switched by a manual jumper (JP1) on this board.
    #define CAN_INTERFACES      CAN
    #define CAN_PINS            GPIO_PIN_8 | GPIO_PIN_9     // PB8 = Rx, PB9 = Tx
    #define CAN_PORTS           GPIOB
    #define CAN_ALTERNATES      GPIO_AF4_CAN                // F0 alternate function 4 selects bxCAN
    // -------------------
    #define LED_TX_PINS         GPIO_PIN_0                  // LD1 (PA0)
    #define LED_TX_PORTS        GPIOA
    #define LED_RX_PINS         GPIO_PIN_1                  // LD2 (PA1)
    #define LED_RX_PORTS        GPIOA
    #define LED_MODE            GPIO_MODE_OUTPUT_PP
    #define LED_ON              GPIO_PIN_SET                // LED's cathode is connected to ground
    #define LED_OFF             GPIO_PIN_RESET
    // -------------------
    #define TERMINATOR_PINS     -1                          // manual jumper on this board, no GPIO control
    #define TERMINATOR_PORTS    GPIOB
    // SN65HVD230 RS (slope / standby) is driven LOW from PC13 to enable high-speed mode.
    // can_init() configures this pin; without it the transceiver stays in standby and
    // nothing is heard or transmitted on the bus.
    #define CAN_TRANSCEIVER_RS_PIN     GPIO_PIN_13
    #define CAN_TRANSCEIVER_RS_PORT    GPIOC
    #define CAN_TRANSCEIVER_RS_ENABLE  __HAL_RCC_GPIOC_CLK_ENABLE
#else
    #error "TARGET_BOARD not defined in makefile"
#endif


// ============================================================================================
// Load default settings if no board-specific settings are defined

// define single channel default: green Tx LED is at pin A0
#ifndef LED_TX_PINS
    #define LED_TX_PINS         GPIO_PIN_0
    #define LED_TX_PORTS        GPIOA
#endif

// define single channel default: blue Rx Led is at pin A15
#ifndef LED_RX_PINS
    #define LED_RX_PINS         GPIO_PIN_15
    #define LED_RX_PORTS        GPIOA
#endif

// PP = Push/Pull, OD = Open Drain
// Most boards use inverted voltage (Low = ON): The LED's anode is connected to +3.3V
#ifndef LED_MODE
    #define LED_MODE            GPIO_MODE_OUTPUT_PP
    #define LED_ON              GPIO_PIN_RESET
    #define LED_OFF             GPIO_PIN_SET
#endif

// define single channel default: no terminator pin available
// Some boards have a 120 Ohm termination resistor that can be enabled by a GPIO pin.
// If the board does not support this --> set TERMINATOR_Pin = -1
#ifndef TERMINATOR_PINS
    #define TERMINATOR_PINS     -1
    #define TERMINATOR_PORTS    GPIOB
#endif    
#ifndef TERMINATOR_MODE
    #define TERMINATOR_MODE     GPIO_MODE_OUTPUT_PP
    #define TERMINATOR_ON       GPIO_PIN_SET        // turn on termination resistor
    #define TERMINATOR_OFF      GPIO_PIN_RESET
#endif

// Single-channel defaults for boards that don't override the CAN pin map.
// Each macro is fenced so a board variant block above can override only the
// ones it cares about (notably the alternate-function number, which differs
// between FDCAN families: G4 = AF9, F0 (bxCAN) = AF4).
#ifndef CHANNEL_COUNT
    #define CHANNEL_COUNT       1
#endif
#ifndef CAN_INTERFACES
    #if defined(CAN_FAMILY_FDCAN)
        #define CAN_INTERFACES      FDCAN1
    #elif defined(CAN_FAMILY_BXCAN)
        #define CAN_INTERFACES      CAN
    #endif
#endif
#ifndef CAN_ALTERNATES
    #if defined(CAN_FAMILY_FDCAN)
        #define CAN_ALTERNATES      GPIO_AF9_FDCAN1   // G4 FDCAN1 on PB8/PB9 = AF9
    #elif defined(CAN_FAMILY_BXCAN)
        #define CAN_ALTERNATES      GPIO_AF4_CAN      // F0 bxCAN on PB8/PB9 = AF4
    #endif
#endif
#ifndef CAN_PINS
    #define CAN_PINS            GPIO_PIN_8 | GPIO_PIN_9 // Rx = PB8, Tx = PB9
#endif
#ifndef CAN_PORTS
    #define CAN_PORTS           GPIOB                   // Port B
#endif

// 0x00 = adapter power comes over USB cable
// 0x40 = adapter has own power supply (flag 'Self Powered' in bmAttributes in Configuration descriptor)
// 0xXX = any other value is invalid!
#ifndef USBD_SELF_POWERED
    #define USBD_SELF_POWERED   0x00
#endif





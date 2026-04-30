/*
    The MIT License
    Copyright (c) 2026 ElmueSoft / CANable port to STM32F0xx (bxCAN)

    Compatibility shim for STM32F0xx (bxCAN) builds.

    The upper layers (Slcan/Candlelight buffer + control) were written against the
    FDCAN HAL on STM32G4. STM32F0 only has the legacy bxCAN peripheral, so it
    does not provide FDCAN_* types or constants. Rather than fork the upper
    layers, this header re-defines the FDCAN-style names that those layers
    use, mapped to bxCAN semantics.

    What is preserved verbatim:
      - field names on Tx/Rx headers (Identifier, IdType, DataLength,
        TxFrameType, RxFrameType, FDFormat, BitRateSwitch, ESI, ...)
      - the symbolic constants FDCAN_STANDARD_ID, FDCAN_DATA_FRAME, etc.
      - the IS_FDCAN_NOMINAL_* / IS_FDCAN_DATA_* range-check macros.

    What changes:
      - FDFormat is always FDCAN_CLASSIC_CAN (bxCAN cannot do CAN FD).
      - DataLength is the raw byte count 0..8 (bxCAN DLC field), NOT an
        FDCAN DLC code. This means utils_dlc_to_byte_count() returns the
        same value it was given for codes 0..8, which is correct.
      - The bus-load helper that branches on FDFormat == FDCAN_FD_CAN
        will fall into the classic-CAN paths.
      - All FDCAN_DATA_BITRATE_* commands (Slcan 'Y'/'y') and CAN-FD frame
        commands ('b','B','d','D') return FBK_UnsupportedFeature.

    The new can.c bxCAN implementation translates between these compat types
    and the underlying CAN_* HAL types when calling HAL_CAN_AddTxMessage /
    HAL_CAN_GetRxMessage / HAL_CAN_ConfigFilter.
*/

#pragma once

#include "stm32f0xx_hal.h"

// ----- ID type --------------------------------------------------------------
// Match the FDCAN HAL bit pattern (0 = standard, 0x40000000 = extended) so the
// upper layers can keep using identity comparisons unchanged.
#define FDCAN_STANDARD_ID        (0x00000000U)
#define FDCAN_EXTENDED_ID        (0x40000000U)

// ----- Frame type (data vs remote) -----------------------------------------
#define FDCAN_DATA_FRAME         (0x00000000U)
#define FDCAN_REMOTE_FRAME       (0x20000000U)

// ----- FDFormat: always classic on bxCAN -----------------------------------
#define FDCAN_CLASSIC_CAN        (0x00000000U)
#define FDCAN_FD_CAN             (0x00200000U) // value never produced by bxCAN

// ----- Bit Rate Switch (CAN FD only — never set on bxCAN) ------------------
#define FDCAN_BRS_OFF            (0x00000000U)
#define FDCAN_BRS_ON             (0x00100000U)

// ----- Error State Indicator (CAN FD only — always ACTIVE on bxCAN) --------
#define FDCAN_ESI_ACTIVE         (0x00000000U)
#define FDCAN_ESI_PASSIVE        (0x00800000U)

// ----- Tx Event FIFO control (no-op on bxCAN) ------------------------------
#define FDCAN_NO_TX_EVENTS       (0x00000000U)
#define FDCAN_STORE_TX_EVENTS    (0x00800000U)

// ----- Filter constants (only ID-mask + route-to-FIFO0 are used) -----------
#define FDCAN_FILTER_MASK        (0x00000002U)
#define FDCAN_FILTER_RANGE       (0x00000000U)
#define FDCAN_FILTER_DUAL        (0x00000001U)
#define FDCAN_FILTER_TO_RXFIFO0  (0x00000001U)
#define FDCAN_FILTER_TO_RXFIFO1  (0x00000002U)
#define FDCAN_FILTER_REJECT      (0x00000003U)
#define FDCAN_FILTER_REMOTE      (0x00000000U)
#define FDCAN_ACCEPT_IN_RX_FIFO0 (0x00000000U)
#define FDCAN_ACCEPT_IN_RX_FIFO1 (0x00000001U)
#define FDCAN_REJECT             (0x00000002U)

// ----- Operating modes ------------------------------------------------------
// The upper layers use these as opaque values that get passed to can_open().
// Inside the bxCAN can.c they are translated into CAN_MODE_* register bits.
#define FDCAN_MODE_NORMAL              (0x00000000U)
#define FDCAN_MODE_RESTRICTED_OPERATION (0x00000001U)
#define FDCAN_MODE_BUS_MONITORING      (0x00000002U)
#define FDCAN_MODE_INTERNAL_LOOPBACK   (0x00000003U)
#define FDCAN_MODE_EXTERNAL_LOOPBACK   (0x00000004U)

// ----- Last protocol error codes (bxCAN reports these in CAN_ESR.LEC) ------
// These constants must match the bxCAN LEC field encoding so the new can.c
// can write the LEC value straight into the status struct without a lookup.
#define FDCAN_PROTOCOL_ERROR_NONE      (0U) // No error
#define FDCAN_PROTOCOL_ERROR_STUFF     (1U) // Stuff error
#define FDCAN_PROTOCOL_ERROR_FORM      (2U) // Form  error
#define FDCAN_PROTOCOL_ERROR_ACK       (3U) // Acknowledgment error
#define FDCAN_PROTOCOL_ERROR_BIT1      (4U) // Bit recessive error
#define FDCAN_PROTOCOL_ERROR_BIT0      (5U) // Bit dominant error
#define FDCAN_PROTOCOL_ERROR_CRC       (6U) // CRC error
#define FDCAN_PROTOCOL_ERROR_NO_CHANGE (7U) // Set when LEC was already read since last error

// ----- Tx Header (mirrors FDCAN field names; only Identifier/IdType/DLC/RTR are used) -----
typedef struct
{
    uint32_t Identifier;            // 0..0x1FFFFFFF
    uint32_t IdType;                // FDCAN_STANDARD_ID or FDCAN_EXTENDED_ID
    uint32_t TxFrameType;           // FDCAN_DATA_FRAME  or FDCAN_REMOTE_FRAME
    uint32_t DataLength;            // bxCAN: raw byte count 0..8 (not an FDCAN DLC code)
    uint32_t ErrorStateIndicator;   // ignored by bxCAN (always Active)
    uint32_t BitRateSwitch;         // ignored by bxCAN
    uint32_t FDFormat;              // must be FDCAN_CLASSIC_CAN (only mode supported)
    uint32_t TxEventFifoControl;    // ignored (no Tx Event FIFO on bxCAN)
    uint32_t MessageMarker;         // 0..255 — tracked in software for echo
} FDCAN_TxHeaderTypeDef;

// ----- Rx Header -----------------------------------------------------------
typedef struct
{
    uint32_t Identifier;
    uint32_t IdType;
    uint32_t RxFrameType;
    uint32_t DataLength;            // raw byte count 0..8
    uint32_t ErrorStateIndicator;
    uint32_t BitRateSwitch;
    uint32_t FDFormat;
    uint32_t RxTimestamp;           // 32-bit software timestamp filled by can.c
    uint32_t FilterIndex;
    uint32_t IsFilterMatchingFrame;
} FDCAN_RxHeaderTypeDef;

// ----- Tx Event (synthesized in software when a mailbox completes) ---------
typedef struct
{
    uint32_t Identifier;
    uint32_t IdType;
    uint32_t TxFrameType;
    uint32_t DataLength;
    uint32_t ErrorStateIndicator;
    uint32_t BitRateSwitch;
    uint32_t FDFormat;
    uint32_t EventType;
    uint32_t MessageMarker;
    uint32_t TxTimestamp;           // 32-bit software timestamp
} FDCAN_TxEventFifoTypeDef;

// ----- Filter (subset of FDCAN_FilterTypeDef that the upper layer fills in) -----
typedef struct
{
    uint32_t IdType;        // FDCAN_STANDARD_ID or FDCAN_EXTENDED_ID
    uint32_t FilterIndex;   // 0-based per-type index
    uint32_t FilterType;    // FDCAN_FILTER_MASK only
    uint32_t FilterConfig;  // FDCAN_FILTER_TO_RXFIFO0 only
    uint32_t FilterID1;     // accept ID
    uint32_t FilterID2;     // mask
} FDCAN_FilterTypeDef;

// ----- Protocol status -----------------------------------------------------
typedef struct
{
    uint32_t LastErrorCode;     // FDCAN_PROTOCOL_ERROR_*
    uint32_t DataLastErrorCode; // always FDCAN_PROTOCOL_ERROR_NONE on bxCAN
    uint32_t Activity;
    uint32_t ErrorPassive;      // 1 if EPV (>=128 errors)
    uint32_t Warning;           // 1 if EWG (>=96 errors)
    uint32_t BusOff;            // 1 if BOF (>=248 errors)
    uint32_t RxESIflag;
    uint32_t RxBRSflag;
    uint32_t RxFDFflag;
    uint32_t ProtocolException;
    uint32_t TDCvalue;          // unused (no TDC on bxCAN)
} FDCAN_ProtocolStatusTypeDef;

// ----- Error counters ------------------------------------------------------
typedef struct
{
    uint32_t TxErrorCnt;
    uint32_t RxErrorCnt;
    uint32_t RxErrorPassiveStatus;
    uint32_t ErrorLogging;
} FDCAN_ErrorCountersTypeDef;

// ----- Peripheral pointer alias --------------------------------------------
// On G4 the upper layer references FDCAN_GlobalTypeDef* in only one place
// (CAN_INTERFACES initializer in can.c). On F0 the bxCAN register block is
// CAN_TypeDef. The new can.c uses CAN_TypeDef* directly under #if BXCAN.
typedef CAN_TypeDef       FDCAN_GlobalTypeDef;

// The can_class struct in can.h embeds an FDCAN_HandleTypeDef. On F0 this
// becomes a CAN_HandleTypeDef so the same field name keeps working.
typedef CAN_HandleTypeDef FDCAN_HandleTypeDef;

// ----- bxCAN bit-timing register limits (used by utils.c) -------------------
// bxCAN BTR fields: BRP=10 bits (1..1024), TS1=4 bits (1..16), TS2=3 bits
// (1..8), SJW=2 bits (1..4). The IS_FDCAN_DATA_* checks always fail on bxCAN
// because there is no data-phase timing.
#define IS_FDCAN_NOMINAL_PRESCALER(BRP)  (((BRP) >= 1U) && ((BRP) <= 1024U))
#define IS_FDCAN_NOMINAL_TSEG1(TSEG1)    (((TSEG1) >= 1U) && ((TSEG1) <=   16U))
#define IS_FDCAN_NOMINAL_TSEG2(TSEG2)    (((TSEG2) >= 1U) && ((TSEG2) <=    8U))
#define IS_FDCAN_NOMINAL_SJW(SJW)        (((SJW)   >= 1U) && ((SJW)   <=    4U))
#define IS_FDCAN_DATA_PRESCALER(BRP)     (0)  // CAN FD not supported on bxCAN
#define IS_FDCAN_DATA_TSEG1(TSEG1)       (0)
#define IS_FDCAN_DATA_TSEG2(TSEG2)       (0)
#define IS_FDCAN_DATA_SJW(SJW)           (0)

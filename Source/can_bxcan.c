/*
    The MIT License
    Copyright (c) 2026 ElmueSoft / CANable port to STM32F0xx (bxCAN)

    bxCAN port of can.c for the STM32F072CBT6.

    Architecturally this is a straight-line port of the G4 FDCAN can.c into the
    bxCAN HAL. The public API in can.h is unchanged so the upper layers
    (Slcan / Candlelight buffer + control) compile against this file the same
    way they do against the FDCAN version. The compatibility shim
    can_compat_bxcan.h provides the FDCAN_*-named typedefs and constants that
    the upper layers reference.

    Differences from the G4 FDCAN driver that a reader needs to be aware of:

    1. **No CAN FD.** can_using_FD() and can_using_BRS() always return false.
       can_set_data_baudrate / can_set_data_bit_timing return FBK_UnsupportedFeature.
       FDCAN_FD_CAN frames passed to can_send_packet would never be issued by
       the upper layers because they check can_using_FD() first.

    2. **3 Tx mailboxes, no Tx Event FIFO.** The G4 has an event FIFO that
       reports the (marker, timestamp) tuple of each successfully ACKed frame.
       bxCAN only has the per-mailbox CAN_TSR.TXOKx flags. We track the marker
       associated with each mailbox in `tx_marker[3]` and synthesize the Tx
       event in software in can_process().

    3. **No hardware timestamp counter.** The Rx/Tx timestamps come from
       TIM2 (1 us tick, 32-bit) read at the moment we pull the frame in
       can_process(). The 16->32-bit timewrap dance is gone; see system.c.

    4. **Filter banks.** bxCAN has 14 filter banks on F072 (single-CAN, no
       slave). We use one bank per user filter in 32-bit ID-mask scale, which
       costs a bit of filter capacity per filter but matches the upper-layer
       model (one accept-mask filter per call to can_set_mask_filter()).

    5. **Bus-off recovery.** bxCAN auto-recovers when AutoBusOff is enabled,
       which requires no software action beyond observing the BOF clear. We
       still track recover_bus_off so the same status messages are emitted.
*/

#include "settings.h"
#include "can.h"
#include "error.h"
#include "led.h"
#include "utils.h"
#include "control.h"
#include "buffer.h"
#include "system.h"

#define CAN_TX_TIMEOUT  500  // ms - abort pending Tx requests if not ACKed

eUserFlags           GLB_UserFlags[CHANNEL_COUNT];

CAN_TypeDef*         SET_CanInterfaces[CHANNEL_COUNT] = { CAN_INTERFACES };
GPIO_TypeDef*        SET_CanPorts     [CHANNEL_COUNT] = { CAN_PORTS };
int                  SET_CanPins      [CHANNEL_COUNT] = { CAN_PINS  };
uint8_t              SET_CanAlternates[CHANNEL_COUNT] = { CAN_ALTERNATES };
GPIO_TypeDef*        SET_TermPorts    [CHANNEL_COUNT] = { TERMINATOR_PORTS };
int                  SET_TermPins     [CHANNEL_COUNT] = { TERMINATOR_PINS  };

can_class            can_inst[CHANNEL_COUNT] = {0};

// Per-mailbox marker tracking (bxCAN has no Tx Event FIFO, so we
// remember which marker we put into each Tx mailbox so we can echo it back
// to the host when CAN_TSR.TXOKx fires.) Indexed 0..2 per channel.
static uint8_t       tx_marker[CHANNEL_COUNT][3];
static bool          tx_marker_valid[CHANNEL_COUNT][3];

static void     can_reset(int channel);
static void     can_print_info(int channel);
static bool     can_apply_filters(can_class* inst);
static uint32_t can_calc_bit_count_in_frame(can_class* inst, FDCAN_RxHeaderTypeDef *header);
static uint32_t can_translate_mode(uint32_t fdcan_mode, bool* ok);

// Init / open / close

void can_init()
{
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct;
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        can_reset(C);

        if (SET_TermPins[C] >= 0)
        {
            GPIO_InitStruct.Pin       = SET_TermPins[C];
            GPIO_InitStruct.Mode      = TERMINATOR_MODE;
            GPIO_InitStruct.Alternate = 0;
            GPIO_InitStruct.Pull      = GPIO_NOPULL;
            GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
            HAL_GPIO_Init(SET_TermPorts[C], &GPIO_InitStruct);
        }

        GPIO_InitStruct.Pin       = SET_CanPins[C];
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Alternate = SET_CanAlternates[C];
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(SET_CanPorts[C], &GPIO_InitStruct);

        can_inst[C].handle.Instance = SET_CanInterfaces[C];
    }

#ifdef CAN_TRANSCEIVER_RS_PIN
    // SN65HVD230 RS (slope/standby) pin: drive low for high-speed normal
    // mode. Without this the transceiver stays in standby and ignores all
    // bus traffic.
    CAN_TRANSCEIVER_RS_ENABLE();
    GPIO_InitTypeDef rs_init = {0};
    rs_init.Pin   = CAN_TRANSCEIVER_RS_PIN;
    rs_init.Mode  = GPIO_MODE_OUTPUT_PP;
    rs_init.Pull  = GPIO_NOPULL;
    rs_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CAN_TRANSCEIVER_RS_PORT, &rs_init);
    HAL_GPIO_WritePin(CAN_TRANSCEIVER_RS_PORT, CAN_TRANSCEIVER_RS_PIN, GPIO_PIN_RESET);
#endif
}

// reset variables that must NOT be touched by can_open
static void can_reset(int channel)
{
    can_class* inst = &can_inst[channel];

    inst->bitrate_nominal.Brp = 0;
    inst->bitrate_data   .Brp = 0; // never set on bxCAN
    inst->std_filter_count    = 0;
    inst->ext_filter_count    = 0;
    inst->busload_interval    = 0;
    inst->tx_pending          = 0;
    inst->is_open             = false;
    inst->open_mode           = FDCAN_MODE_NORMAL;

    for (int i=0; i<3; i++)
        tx_marker_valid[channel][i] = false;

    buf_clear_can_buffer(channel);
}

eFeedback can_open(int channel, uint32_t mode)
{
    can_class* inst = &can_inst[channel];
    if (inst->is_open)
        return FBK_AdapterMustBeClosed;

    if (inst->bitrate_nominal.Brp == 0)
        return FBK_BaudrateNotSet;

    bool mode_ok;
    uint32_t bxcan_mode = can_translate_mode(mode, &mode_ok);
    if (!mode_ok)
        return FBK_InvalidParameter; // FDCAN_MODE_RESTRICTED_OPERATION etc.

    if (!can_is_any_open())
    {
        __HAL_RCC_CAN1_FORCE_RESET();
        __HAL_RCC_CAN1_RELEASE_RESET();
    }

    buf_clear_can_buffer(channel);
    error_init(channel);
    led_blink_identify(channel, false);

    inst->open_mode = mode;
    for (int i=0; i<3; i++)
        tx_marker_valid[channel][i] = false;

    CAN_InitTypeDef* init = &inst->handle.Init;
    init->Prescaler            = inst->bitrate_nominal.Brp;
    init->Mode                 = bxcan_mode;
    // SJW is constrained to 1..4 (CAN_SJW_*TQ encodes (TQ-1) in the field)
    init->SyncJumpWidth        = ((inst->bitrate_nominal.Sjw  - 1U) & 0x3U) << 24; // CAN_BTR_SJW position
    // TimeSeg1 (BS1) is 1..16, encoded as (TQ-1)<<16
    init->TimeSeg1             = ((inst->bitrate_nominal.Seg1 - 1U) & 0xFU) << 16;
    // TimeSeg2 (BS2) is 1..8, encoded as (TQ-1)<<20
    init->TimeSeg2             = ((inst->bitrate_nominal.Seg2 - 1U) & 0x7U) << 20;
    init->TimeTriggeredMode    = DISABLE;
    init->AutoBusOff           = ENABLE;  // hardware bus-off recovery
    init->AutoWakeUp           = DISABLE;
    init->AutoRetransmission   = (GLB_UserFlags[channel] & USR_Retransmit) ? ENABLE : DISABLE;
    init->ReceiveFifoLocked    = DISABLE;
    init->TransmitFifoPriority = ENABLE;  // FIFO-style ordering across mailboxes (insertion order)

    // Bus-load math wants the nominal bit length in nanoseconds.
    uint32_t clock_MHz = system_get_can_clock() / 1000000U; // 48 on F072
    inst->nom_bit_len_ns  = 1U + inst->bitrate_nominal.Seg1 + inst->bitrate_nominal.Seg2;
    inst->nom_bit_len_ns *= inst->bitrate_nominal.Brp;
    inst->nom_bit_len_ns *= 1000U;
    inst->nom_bit_len_ns /= clock_MHz;

    inst->bitrate_printed_once = false;
    inst->delay_printed_once   = true; // no TDC on bxCAN
    inst->recover_bus_off      = false;

    if (HAL_CAN_Init(&inst->handle) != HAL_OK)
        return FBK_ErrorFromHAL;

    if (!can_apply_filters(inst))
        return FBK_ErrorFromHAL;

    if (HAL_CAN_Start(&inst->handle) != HAL_OK)
        return FBK_ErrorFromHAL;

    led_turn_TX(channel, false);
    inst->is_open = true;
    return FBK_Success;
}

void can_close_all()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
        can_close(C);
}

void can_close(int channel)
{
    can_class* inst = &can_inst[channel];
    if (!inst->is_open)
        return;

    HAL_CAN_Stop  (&inst->handle);
    HAL_CAN_DeInit(&inst->handle);

    can_reset(channel);
    led_turn_TX(channel, true);
}

// Tx

void can_send_packet(int channel, FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data)
{
    can_class* inst = &can_inst[channel];

    CAN_TxHeaderTypeDef hal_hdr;
    hal_hdr.IDE = (tx_header->IdType == FDCAN_EXTENDED_ID) ? CAN_ID_EXT : CAN_ID_STD;
    hal_hdr.RTR = (tx_header->TxFrameType == FDCAN_REMOTE_FRAME) ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    if (hal_hdr.IDE == CAN_ID_EXT) { hal_hdr.ExtId = tx_header->Identifier; hal_hdr.StdId = 0; }
    else                           { hal_hdr.StdId = tx_header->Identifier; hal_hdr.ExtId = 0; }
    // bxCAN DLC is the raw byte count for classic frames (0..8). The compat
    // shim already stores the byte count there.
    uint32_t dlc = tx_header->DataLength;
    if (dlc > 8U) dlc = 8U;
    hal_hdr.DLC = dlc;
    hal_hdr.TransmitGlobalTime = DISABLE;

    uint32_t tx_mailbox = 0;
    HAL_StatusTypeDef status = HAL_CAN_AddTxMessage(&inst->handle, &hal_hdr, tx_data, &tx_mailbox);
    if (status != HAL_OK)
    {
        error_assert(channel, APP_CanTxFail, true);
        return;
    }

    // Remember which marker is in this mailbox so the Tx echo path in
    // can_process() can pair the TXOKx flag with the right marker.
    int mb_index = (tx_mailbox == CAN_TX_MAILBOX1) ? 1
                 : (tx_mailbox == CAN_TX_MAILBOX2) ? 2 : 0;
    tx_marker      [channel][mb_index] = (uint8_t)tx_header->MessageMarker;
    tx_marker_valid[channel][mb_index] = true;

    if (inst->handle.Init.AutoRetransmission == ENABLE)
    {
        inst->last_tx_tick = HAL_GetTick();
        inst->tx_pending ++;
    }
}

// Look at the per-mailbox status flags, fabricate a Tx event for each ACKed
// frame, and feed it to the upper layer.
static void can_process_tx(int channel)
{
    can_class* inst = &can_inst[channel];

    static const uint32_t txok_flags[3]  = { CAN_FLAG_TXOK0, CAN_FLAG_TXOK1, CAN_FLAG_TXOK2 };
    static const uint32_t rqcp_flags[3]  = { CAN_FLAG_RQCP0, CAN_FLAG_RQCP1, CAN_FLAG_RQCP2 };

    for (int i=0; i<3; i++)
    {
        if (!__HAL_CAN_GET_FLAG(&inst->handle, rqcp_flags[i]))
            continue;
        bool tx_ok = __HAL_CAN_GET_FLAG(&inst->handle, txok_flags[i]) != 0;
        // RQCP also clears any TERR/ALST flags for this mailbox.
        __HAL_CAN_CLEAR_FLAG(&inst->handle, rqcp_flags[i]);

        if (!tx_marker_valid[channel][i])
            continue;
        tx_marker_valid[channel][i] = false;

        if (!tx_ok)
            continue; // failed transmissions are surfaced via error.c

        if (inst->tx_pending > 0)
        {
            inst->last_tx_tick = HAL_GetTick();
            inst->tx_pending --;
        }
        led_flash_TX(channel);

        // Build the Tx event the upper layer expects.
        if (GLB_UserFlags[channel] & USR_ReportTX)
        {
            FDCAN_TxEventFifoTypeDef ev = {0};
            ev.MessageMarker = tx_marker[channel][i];
            ev.TxTimestamp   = system_get_timestamp();
            buf_store_tx_echo(channel, &ev);
        }

        // Bus-load tally. We don't have the original TxFrameType /
        // IdType readily here, so approximate with a classic-data 11-bit
        // frame header overhead. (Multi-byte data is also unknown post-Tx.)
        // Underestimate is OK for bus-load reports.
        if (inst->open_mode == FDCAN_MODE_NORMAL)
        {
            FDCAN_RxHeaderTypeDef stub = {0};
            stub.IdType      = FDCAN_STANDARD_ID;
            stub.RxFrameType = FDCAN_DATA_FRAME;
            stub.FDFormat    = FDCAN_CLASSIC_CAN;
            stub.DataLength  = 0;
            inst->bit_count_total += can_calc_bit_count_in_frame(inst, &stub);
        }
    }
}

// Rx

static void can_drain_rx(int channel, uint32_t fifo, bool is_user_visible)
{
    can_class* inst = &can_inst[channel];
    // bxCAN only ever fills 8 bytes, but the Candlelight legacy upper layer
    // memcpy's 64 bytes from this buffer into a 64-byte raw_data field
    // regardless of the actual DLC. Pad to 64 to avoid an out-of-bounds read.
    uint8_t can_data_buf[64] = {0};

    while (HAL_CAN_GetRxFifoFillLevel(&inst->handle, fifo) > 0U)
    {
        CAN_RxHeaderTypeDef hal_rx;
        if (HAL_CAN_GetRxMessage(&inst->handle, fifo, &hal_rx, can_data_buf) != HAL_OK)
            break;

        // Re-pack into the FDCAN-style header the upper layers expect.
        FDCAN_RxHeaderTypeDef rx_header = {0};
        rx_header.IdType      = (hal_rx.IDE == CAN_ID_EXT)    ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
        rx_header.RxFrameType = (hal_rx.RTR == CAN_RTR_REMOTE)? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
        rx_header.Identifier  = (hal_rx.IDE == CAN_ID_EXT)    ? hal_rx.ExtId : hal_rx.StdId;
        rx_header.DataLength  = hal_rx.DLC;
        rx_header.FDFormat    = FDCAN_CLASSIC_CAN;
        rx_header.BitRateSwitch       = FDCAN_BRS_OFF;
        rx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        rx_header.FilterIndex         = hal_rx.FilterMatchIndex;
        rx_header.RxTimestamp         = system_get_timestamp();

        // Bus load (counts every received frame, even those dropped by the
        // upper-layer filter into FIFO1).
        inst->bit_count_total += can_calc_bit_count_in_frame(inst, &rx_header);

        if (is_user_visible)
            buf_store_rx_packet(channel, &rx_header, can_data_buf);

        led_flash_RX(channel);
    }
}

// Main loop entry point (called ~100 times per millisecond)

void can_process(int channel, uint32_t tick_now)
{
    can_class* inst = &can_inst[channel];
    if (!inst->is_open)
        return;

    can_process_tx (channel);
    can_drain_rx   (channel, CAN_RX_FIFO0, true);  // user-visible packets
    can_drain_rx   (channel, CAN_RX_FIFO1, false); // filtered-out (LED only)

    // Rx FIFO 0 / 1 overrun reporting
    if (__HAL_CAN_GET_FLAG(&inst->handle, CAN_FLAG_FOV0))
    {
        error_assert(channel, APP_CanRxFail, false);
        __HAL_CAN_CLEAR_FLAG(&inst->handle, CAN_FLAG_FOV0);
    }
    if (__HAL_CAN_GET_FLAG(&inst->handle, CAN_FLAG_FOV1))
    {
        error_assert(channel, APP_CanRxFail, false);
        __HAL_CAN_CLEAR_FLAG(&inst->handle, CAN_FLAG_FOV1);
    }

    // Refresh bus status. Read straight from CAN_ESR; can_read_proto_status()
    // does the same thing for error.c. Cache it locally so other helpers
    // (can_recover_bus_off, can_is_passive) don't need to re-poll.
    can_read_proto_status(channel, &inst->cur_status);

    // Tx timeout: if a frame has been hanging in a mailbox without an ACK
    // for too long, abort it so we don't get stuck retransmitting forever.
    if (inst->tx_pending > 0 && tick_now >= inst->last_tx_tick + CAN_TX_TIMEOUT)
    {
        inst->tx_pending = 0;
        HAL_CAN_AbortTxRequest(&inst->handle, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
        for (int i=0; i<3; i++)
            tx_marker_valid[channel][i] = false;
        buf_clear_can_buffer(channel);
        error_assert(channel, APP_CanTxTimeout, false);
    }

    if (!inst->bitrate_printed_once && inst->is_open)
    {
        inst->bitrate_printed_once = true;
        can_print_info(channel);
    }
}

// Bus-off recovery

void can_recover_bus_off(int channel)
{
    can_class* inst = &can_inst[channel];
    if (!inst->is_open)
        return;

    if (inst->cur_status.BusOff)
    {
        if (!inst->recover_bus_off)
        {
            inst->recover_bus_off = true;
            control_send_debug_mesg(channel, ">> Bus Off detected (bxCAN auto-recovers when bus quietens)");
            // bxCAN with AutoBusOff=ENABLE clears the bus-off condition
            // automatically after 128 occurrences of 11 recessive bits, no
            // software action required.
        }
    }
    else if (inst->recover_bus_off)
    {
        inst->recover_bus_off = false;
        control_send_debug_mesg(channel, "<< Successfully recovered from Bus Off");
        error_clear(channel);
    }
}


void can_timer_100ms()
{
    const uint32_t STUFFING_FACTOR = 1125;

    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        can_class* inst = &can_inst[C];
        if (!inst->is_open || inst->busload_interval == 0)
            continue;

        if (inst->busload_counter >= inst->busload_interval)
        {
            uint32_t rate_us_ppm = inst->bit_count_total * inst->nom_bit_len_ns / 100000U;
            uint32_t busload_ppm = rate_us_ppm * STUFFING_FACTOR / inst->busload_interval;
            uint8_t  new_busload = MIN(99, busload_ppm / 10000U);

            if (new_busload > 0 || inst->old_busload_pct > 0)
                control_report_busload(C, new_busload);

            inst->old_busload_pct = new_busload;
            inst->bit_count_total = 0;
            inst->busload_counter = 0;
        }
        inst->busload_counter ++;
    }
}

// Bitrate setters

// Sample point 87.5% targeted. CAN clock = PCLK1 = 48 MHz.
// Total time quanta per bit = 16 (1 sync + 14 prop/seg1 + 1 seg2)... but bxCAN
// only allows TS1 max 16 and TS2 max 8, so for 1 Mbit we need 48/Brp/(1+Seg1+Seg2)
// = 1e6 -> 48 quanta. Brp=3, Seg1=13, Seg2=2 = 16 TQ * 3 = 48. SP = (1+13)/16 = 87.5%.
// For other bitrates we keep Seg1=13, Seg2=2 and adjust Brp.
eFeedback can_set_baudrate(int channel, can_nom_bitrate bitrate)
{
    can_class* inst = &can_inst[channel];
    if (inst->is_open)
        return FBK_AdapterMustBeClosed;

    inst->bitrate_nominal.Seg1 = 13;
    inst->bitrate_nominal.Seg2 = 2;

    // CAN clock at 48 MHz. Total bits per TQ-group = 16 -> 48 MHz / 16 = 3 Mbit
    // baseline; divide by Brp to land on each requested baudrate.
    switch (bitrate)
    {
        case CAN_BITRATE_10K:   inst->bitrate_nominal.Brp = 300; break;
        case CAN_BITRATE_20K:   inst->bitrate_nominal.Brp = 150; break;
        case CAN_BITRATE_50K:   inst->bitrate_nominal.Brp =  60; break;
        case CAN_BITRATE_83K:
            // 83.333 kbit at 48 MHz: 48 MHz / 36 / 16 = 83.333 kHz, SP 87.5%
            inst->bitrate_nominal.Brp  = 36;
            inst->bitrate_nominal.Seg1 = 13;
            inst->bitrate_nominal.Seg2 = 2;
            break;
        case CAN_BITRATE_100K:  inst->bitrate_nominal.Brp =  30; break;
        case CAN_BITRATE_125K:  inst->bitrate_nominal.Brp =  24; break;
        case CAN_BITRATE_250K:  inst->bitrate_nominal.Brp =  12; break;
        case CAN_BITRATE_500K:  inst->bitrate_nominal.Brp =   6; break;
        case CAN_BITRATE_800K:
            // 800 kbit at 48 MHz: 48 MHz / 4 / 15 = 800 kHz, SP 86.7%
            inst->bitrate_nominal.Brp  = 4;
            inst->bitrate_nominal.Seg1 = 12;
            inst->bitrate_nominal.Seg2 = 2;
            break;
        case CAN_BITRATE_1000K: inst->bitrate_nominal.Brp =   3; break;
        default:
            return FBK_InvalidParameter;
    }

    bitlimits* limits = utils_get_bit_limits();
    inst->bitrate_nominal.Sjw = MIN(inst->bitrate_nominal.Seg2, limits->nom_sjw_max);

    if (!IS_FDCAN_NOMINAL_PRESCALER(inst->bitrate_nominal.Brp)  ||
        !IS_FDCAN_NOMINAL_TSEG1    (inst->bitrate_nominal.Seg1) ||
        !IS_FDCAN_NOMINAL_TSEG2    (inst->bitrate_nominal.Seg2))
    {
        inst->bitrate_nominal.Brp = 0;
        return FBK_ParamOutOfRange; // covers 83 kbit on bxCAN: Seg2=10 > max 8
    }
    return FBK_Success;
}

// CAN FD data baudrate not supported on bxCAN.
eFeedback can_set_data_baudrate(int channel, can_data_bitrate bitrate)
{
    (void)channel; (void)bitrate;
    return FBK_UnsupportedFeature;
}

eFeedback can_set_nom_bit_timing(int channel, uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw)
{
    can_class* inst = &can_inst[channel];
    if (inst->is_open)
        return FBK_AdapterMustBeClosed;

    if (!IS_FDCAN_NOMINAL_PRESCALER(BRP)  ||
        !IS_FDCAN_NOMINAL_TSEG1    (Seg1) ||
        !IS_FDCAN_NOMINAL_TSEG2    (Seg2) ||
        !IS_FDCAN_NOMINAL_SJW      (Sjw))
            return FBK_ParamOutOfRange;

    inst->bitrate_nominal.Brp  = BRP;
    inst->bitrate_nominal.Seg1 = Seg1;
    inst->bitrate_nominal.Seg2 = Seg2;
    inst->bitrate_nominal.Sjw  = Sjw;
    return FBK_Success;
}

// CAN FD data bit timing not supported on bxCAN.
eFeedback can_set_data_bit_timing(int channel, uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw)
{
    (void)channel; (void)BRP; (void)Seg1; (void)Seg2; (void)Sjw;
    return FBK_UnsupportedFeature;
}

static void can_print_info(int channel)
{
    if ((GLB_UserFlags[channel] & USR_DebugReport) == 0)
        return;

    can_class* inst = &can_inst[channel];

    char buf[200];
    if (inst->open_mode != FDCAN_MODE_NORMAL)
    {
        char* mode = "Invalid";
        switch (inst->open_mode)
        {
            case FDCAN_MODE_BUS_MONITORING:    mode = "Monitoring";        break;
            case FDCAN_MODE_INTERNAL_LOOPBACK: mode = "Internal Loopback"; break;
            case FDCAN_MODE_EXTERNAL_LOOPBACK: mode = "External Loopback"; break;
        }
        sprintf(buf, "Operation Mode: %s", mode);
        control_send_debug_mesg(channel, buf);
    }

    utils_format_bitrate(buf, "Nominal", &inst->bitrate_nominal);
    control_send_debug_mesg(channel, buf);
}

// Filters (bxCAN: 14 filter banks, all 32-bit ID-mask, all routed to FIFO 0
// for user-visible matches; non-matching frames go to FIFO 1 via the
// catch-all filter installed when no user filters are defined.)

eFeedback can_set_mask_filter(int channel, bool extended, uint32_t filter, uint32_t mask)
{
    can_class* inst = &can_inst[channel];

    int tot_filters = inst->std_filter_count + inst->ext_filter_count;
    if (tot_filters >= MAX_FILTERS)
        return FBK_InvalidParameter;

    uint32_t maximum = extended ? 0x1FFFFFFFU : 0x7FFU;
    if (filter > maximum || mask > maximum)
        return FBK_ParamOutOfRange;

    if (inst->is_open)
    {
        if (tot_filters != 1)
            return FBK_AdapterMustBeClosed;
        if (extended != (inst->ext_filter_count == 1))
            return FBK_AdapterMustBeClosed;
        inst->ext_filter_count = 0;
        inst->std_filter_count = 0;
        tot_filters = 0;
    }

    FDCAN_FilterTypeDef* last_filter = &inst->filters[tot_filters];
    last_filter->IdType       = extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    last_filter->FilterIndex  = extended ? inst->ext_filter_count : inst->std_filter_count;
    last_filter->FilterType   = FDCAN_FILTER_MASK;
    last_filter->FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    last_filter->FilterID1    = filter;
    last_filter->FilterID2    = mask;

    if (extended) inst->ext_filter_count ++;
    else          inst->std_filter_count ++;

    if (inst->is_open && !can_apply_filters(inst))
        return FBK_ErrorFromHAL;

    return FBK_Success;
}

eFeedback can_clear_filters(int channel)
{
    can_class* inst = &can_inst[channel];
    if (inst->is_open)
        return FBK_AdapterMustBeClosed;

    inst->ext_filter_count = 0;
    inst->std_filter_count = 0;
    return FBK_Success;
}

// Encode a (filter, mask, IDE, RTR) into the bxCAN 32-bit IDR layout used by
// 32-bit-scale filters: STDID[10:0]<<21 | EXTID[17:0]<<3 | IDE<<2 | RTR<<1.
static void encode_filter_32bit(bool extended, uint32_t filter, uint32_t mask,
                                uint32_t* id_high, uint32_t* id_low,
                                uint32_t* mask_high, uint32_t* mask_low)
{
    uint32_t id_word, mask_word;
    if (extended)
    {
        id_word   = (filter << 3) | CAN_ID_EXT;
        mask_word = (mask   << 3) | CAN_ID_EXT;   // require IDE = 1
    }
    else
    {
        id_word   = (filter << 21);
        mask_word = (mask   << 21) | CAN_ID_EXT;  // require IDE = 0 (so EXT frames are rejected)
    }
    *id_high   = (id_word   >> 16) & 0xFFFFU;
    *id_low    = (id_word        ) & 0xFFFFU;
    *mask_high = (mask_word >> 16) & 0xFFFFU;
    *mask_low  = (mask_word      ) & 0xFFFFU;
}

static bool can_apply_filters(can_class* inst)
{
    int tot_filters = inst->std_filter_count + inst->ext_filter_count;
    bool has_filters = (tot_filters > 0);

    // Always install at least one filter that catches the rest of the
    // traffic into RX FIFO 1, so non-matching frames still flash the LED.
    // (Mirrors the FDCAN driver's "non_matching" config.)
    CAN_FilterTypeDef hal_filter;
    int bank_index = 0;

    // First: install the user filters into FIFO 0.
    for (int i=0; i<tot_filters; i++)
    {
        FDCAN_FilterTypeDef* f = &inst->filters[i];
        bool extended = (f->IdType == FDCAN_EXTENDED_ID);

        encode_filter_32bit(extended, f->FilterID1, f->FilterID2,
                            &hal_filter.FilterIdHigh,
                            &hal_filter.FilterIdLow,
                            &hal_filter.FilterMaskIdHigh,
                            &hal_filter.FilterMaskIdLow);

        hal_filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
        hal_filter.FilterBank           = bank_index++;
        hal_filter.FilterMode           = CAN_FILTERMODE_IDMASK;
        hal_filter.FilterScale          = CAN_FILTERSCALE_32BIT;
        hal_filter.FilterActivation     = CAN_FILTER_ENABLE;
        hal_filter.SlaveStartFilterBank = 14; // F072 has no slave instance
        if (HAL_CAN_ConfigFilter(&inst->handle, &hal_filter) != HAL_OK)
            return false;
    }

    // Second: install a catch-all that routes everything else into the
    // appropriate FIFO. With no user filters the catch-all goes to FIFO 0
    // (so the host sees every frame). With user filters present the
    // catch-all goes to FIFO 1 (LED-only).
    hal_filter.FilterIdHigh         = 0x0000U;
    hal_filter.FilterIdLow          = 0x0000U;
    hal_filter.FilterMaskIdHigh     = 0x0000U;
    hal_filter.FilterMaskIdLow      = 0x0000U; // mask=0 -> match every frame
    hal_filter.FilterFIFOAssignment = has_filters ? CAN_FILTER_FIFO1 : CAN_FILTER_FIFO0;
    hal_filter.FilterBank           = bank_index;
    hal_filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    hal_filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    hal_filter.FilterActivation     = CAN_FILTER_ENABLE;
    hal_filter.SlaveStartFilterBank = 14;
    if (HAL_CAN_ConfigFilter(&inst->handle, &hal_filter) != HAL_OK)
        return false;

    return true;
}

// Termination resistor / busload report enable / status

bool can_set_termination(int channel, bool enable)
{
    if (SET_TermPins[channel] <= 0)
        return false;
    HAL_GPIO_WritePin(SET_TermPorts[channel], SET_TermPins[channel], enable ? TERMINATOR_ON : TERMINATOR_OFF);
    can_inst[channel].termination_on = enable;
    return true;
}

bool can_get_termination(int channel, bool* enabled)
{
    if (SET_TermPins[channel] <= 0)
        return false;
    *enabled = can_inst[channel].termination_on;
    return true;
}

eFeedback can_enable_busload(int channel, uint32_t interval)
{
    if (interval > 100)
        return FBK_ParamOutOfRange;
    can_inst[channel].busload_interval = interval;
    return FBK_Success;
}

bool can_is_any_open()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
        if (can_inst[C].is_open) return true;
    return false;
}

bool can_is_open    (int channel) { return can_inst[channel].is_open; }
bool can_is_passive (int channel) { return can_inst[channel].cur_status.ErrorPassive; }
bool can_using_FD   (int channel) { (void)channel; return false; }
bool can_using_BRS  (int channel) { (void)channel; return false; }

eFeedback can_is_tx_allowed(int channel)
{
    can_class* inst = &can_inst[channel];
    if (!inst->is_open)                       return FBK_AdapterMustBeOpen;
    if (inst->open_mode == FDCAN_MODE_BUS_MONITORING) return FBK_NoTxInSilentMode;
    if (inst->cur_status.BusOff)              return FBK_BusIsOff;
    return FBK_Success;
}

bool can_is_tx_fifo_free(int channel)
{
    return HAL_CAN_GetTxMailboxesFreeLevel(&can_inst[channel].handle) > 0U;
}

FDCAN_HandleTypeDef* can_get_handle(int channel)
{
    return &can_inst[channel].handle;
}

// Status / error counter readout (called from error.c)

void can_read_proto_status(int channel, FDCAN_ProtocolStatusTypeDef* out)
{
    uint32_t esr = can_inst[channel].handle.Instance->ESR;
    out->LastErrorCode     = (esr & CAN_ESR_LEC) >> CAN_ESR_LEC_Pos;
    out->DataLastErrorCode = FDCAN_PROTOCOL_ERROR_NONE;
    out->Activity          = 0;
    out->Warning           = (esr & CAN_ESR_EWGF) ? 1U : 0U;
    out->ErrorPassive      = (esr & CAN_ESR_EPVF) ? 1U : 0U;
    out->BusOff            = (esr & CAN_ESR_BOFF) ? 1U : 0U;
    out->RxESIflag         = 0;
    out->RxBRSflag         = 0;
    out->RxFDFflag         = 0;
    out->ProtocolException = 0;
    out->TDCvalue          = 0;
}

void can_read_error_counters(int channel, FDCAN_ErrorCountersTypeDef* out)
{
    uint32_t esr = can_inst[channel].handle.Instance->ESR;
    out->TxErrorCnt          = (esr & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos;
    out->RxErrorCnt          = (esr & CAN_ESR_REC) >> CAN_ESR_REC_Pos;
    out->RxErrorPassiveStatus = 0;
    out->ErrorLogging         = 0;
}

// IRQ entry point (forwarded from CEC_CAN_IRQHandler in interrupts.c)

void can_irq_handler(void)
{
    // We never call HAL_CAN_ActivateNotification(), so no interrupts are
    // expected. If something does enable a CAN IRQ in the future, route it
    // through the HAL handler.
    for (int C=0; C<CHANNEL_COUNT; C++)
        HAL_CAN_IRQHandler(&can_inst[C].handle);
}

// Helpers

// Translate the FDCAN_MODE_* the upper layer passes to can_open() into the
// bxCAN CAN_MODE_* register encoding. Sets *ok = false for modes bxCAN
// cannot represent.
static uint32_t can_translate_mode(uint32_t fdcan_mode, bool* ok)
{
    *ok = true;
    switch (fdcan_mode)
    {
        case FDCAN_MODE_NORMAL:            return CAN_MODE_NORMAL;
        case FDCAN_MODE_BUS_MONITORING:    return CAN_MODE_SILENT;
        case FDCAN_MODE_INTERNAL_LOOPBACK: return CAN_MODE_SILENT_LOOPBACK;
        case FDCAN_MODE_EXTERNAL_LOOPBACK: return CAN_MODE_LOOPBACK;
        default:
            *ok = false;
            return CAN_MODE_NORMAL;
    }
}

// Bus-load bit count. Same model as the FDCAN version but the FD branches
// are dead code on bxCAN (FDFormat is always FDCAN_CLASSIC_CAN).
static uint32_t can_calc_bit_count_in_frame(can_class* inst, FDCAN_RxHeaderTypeDef* header)
{
    if (inst->busload_interval == 0)
        return 0;

    const uint32_t CAN_BIT_NBR_WOD_CBFF = 47;
    const uint32_t CAN_BIT_NBR_WOD_CEFF = 67;

    uint32_t byte_count = (header->DataLength <= 8U) ? header->DataLength : 8U;

    if (header->RxFrameType == FDCAN_REMOTE_FRAME)
        return (header->IdType == FDCAN_EXTENDED_ID) ? CAN_BIT_NBR_WOD_CEFF : CAN_BIT_NBR_WOD_CBFF;

    uint32_t bit_count = (header->IdType == FDCAN_EXTENDED_ID) ? CAN_BIT_NBR_WOD_CEFF : CAN_BIT_NBR_WOD_CBFF;
    bit_count += byte_count * 8U;
    return bit_count;
}

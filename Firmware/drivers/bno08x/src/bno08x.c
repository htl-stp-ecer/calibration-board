#include "bno08x.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "sh2.h"
#include "sh2_hal.h"
#include "sh2_err.h"

extern SPI_HandleTypeDef hspi1;

/* Globals für nachträgliche GDB-Inspektion (kein UART am Board).  Halten den
 * letzten Init-Status und ein paar Rohdaten fest, damit ein Reviewer das
 * Verhalten ohne Tracer rekonstruieren kann.  Werden im normalen Betrieb
 * ignoriert. */
volatile int      g_bno_stage           = 0;
volatile int      g_sh2_open_rc         = 0xCAFE;
volatile int      g_sh2_prodids_rc      = 0xCAFE;
volatile uint8_t  g_int_after_reset     = 0xFF;
volatile uint32_t g_int_ms_to_assert    = 0;
volatile uint32_t g_read_calls          = 0;
volatile uint32_t g_reads_with_data     = 0;
volatile uint8_t  g_first_read_buf[16];
volatile uint8_t  g_first_read_skipped  = 0;
volatile uint32_t g_exti4_falling_count = 0;
volatile uint32_t g_exti4_first_tick = 0;
volatile uint32_t g_write_calls         = 0;
volatile uint32_t g_writes_ok           = 0;
volatile uint32_t g_writes_ack_int      = 0;  /* INT-LOW kam vor 100 ms Timeout */
volatile uint32_t g_reset_events        = 0;
volatile uint8_t  g_last_read_chan      = 0xFF;
volatile uint16_t g_last_read_len       = 0;
/* Hal_read Exit-Pfade */
volatile uint32_t g_read_skip_int_high  = 0;  /* INT high, nicht 64. Call */
volatile uint32_t g_read_hdr_ff         = 0;  /* alle 64 Bytes 0xFF */
volatile uint32_t g_read_hdr_zero       = 0;  /* hdr[0]=0 → kein Paket */
volatile uint32_t g_read_pkt_too_big    = 0;
volatile uint32_t g_read_spi_fail       = 0;
/* Hal_write Exit-Pfade */
volatile uint32_t g_write_int_gated     = 0;
volatile uint32_t g_write_spi_fail      = 0;
/* Letzter unverarbeiteter hdr beim hdr_zero / hdr_ff exit */
volatile uint8_t  g_last_garbage_hdr[4] = {0};
/* Echte (non-zero-len) Packet-Header + erste Payload-Bytes für Forensik */
volatile uint8_t  g_real_pkt_hdr[4]      = {0};
volatile uint8_t  g_real_pkt_payload[32] = {0};
volatile uint16_t g_real_pkt_len         = 0;

#define BNO_INT_TIMEOUT_MS  300u
#define BNO_BOOT_DELAY_MS   500u

static inline void cs_low(void)    { HAL_GPIO_WritePin(BNO_CS1_GPIO_Port,  BNO_CS1_Pin,  GPIO_PIN_RESET); }
static inline void cs_high(void)   { HAL_GPIO_WritePin(BNO_CS1_GPIO_Port,  BNO_CS1_Pin,  GPIO_PIN_SET); }
static inline void wake_low(void)  { HAL_GPIO_WritePin(BNO_WAKE_GPIO_Port, BNO_WAKE_Pin, GPIO_PIN_RESET); }
static inline void wake_high(void) { HAL_GPIO_WritePin(BNO_WAKE_GPIO_Port, BNO_WAKE_Pin, GPIO_PIN_SET); }
static inline void nrst_low(void)  { HAL_GPIO_WritePin(BNO_NRST_GPIO_Port, BNO_NRST_Pin, GPIO_PIN_RESET); }
static inline void nrst_high(void) { HAL_GPIO_WritePin(BNO_NRST_GPIO_Port, BNO_NRST_Pin, GPIO_PIN_SET); }
static inline bool int_asserted(void)
{
    return HAL_GPIO_ReadPin(BNO_INT_GPIO_Port, BNO_INT_Pin) == GPIO_PIN_RESET;
}

static bool wait_int_low(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) <= timeout_ms) {
        if (int_asserted()) return true;
    }
    return false;
}

static int hal_open(sh2_Hal_t *self)
{
    (void)self;
    g_bno_stage = 10;

    /* CubeMX hat PC4 als EXTI_RISING konfiguriert und keinen NVIC-Handler
     * dafür angelegt.  Wir nehmen die Leitung als reine Input-Polling-Quelle
     * mit interner Pullup-Last (passend zum open-drain INT des BNO). */
    GPIO_InitTypeDef g = {0};
    g.Pin   = BNO_INT_Pin;
    g.Mode  = GPIO_MODE_IT_FALLING;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BNO_INT_GPIO_Port, &g);
    /* CubeMX hat den EXTI4-NVIC nicht enabled — händisch nachziehen. */
    HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);

    /* SPI1 kommt aus CubeMX in Mode 0 @24 MHz; BNO08x verlangt Mode 3
     * und max 3 MHz (§4.2 BNO080 datasheet). APB2 = 96 MHz, /32 → 3 MHz. */
    HAL_SPI_DeInit(&hspi1);
    hspi1.Init.CLKPolarity       = SPI_POLARITY_HIGH;
    hspi1.Init.CLKPhase          = SPI_PHASE_2EDGE;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    hspi1.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        g_bno_stage = -11;
        return -1;
    }
    g_bno_stage = 11;

    cs_high();
    wake_high();
    /* PB2 wird im CubeMX-GPIO-Init auf LOW gezogen — das ist BNO_NBOOT
     * (active low) und hält den Chip sonst im Bootloader-Mode fest. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);

    nrst_low();
    HAL_Delay(20);
    nrst_high();
    g_bno_stage = 12;

    /* Warten bis INT (active-low) gezogen wird ODER Timeout.  Der BNO080
     * sendet die SHTP-Advertisement ~90 ms nach Reset.  Wenn die INT-Leitung
     * elektrisch nicht durchkommt, läuft hier der Timeout ab und wir gehen
     * trotzdem weiter (best effort). */
    uint32_t t0 = HAL_GetTick();
    bool got_int = wait_int_low(BNO_INT_TIMEOUT_MS);
    g_int_ms_to_assert = got_int ? (HAL_GetTick() - t0) : 0;
    g_int_after_reset = (uint8_t)int_asserted();

    /* Etwas Schlaf vor dem ersten Read — gibt der SH2-Firmware Zeit, die
     * Advertise-Bytes ins SPI-FIFO zu pumpen. */
    HAL_Delay(BNO_BOOT_DELAY_MS);

    g_bno_stage = got_int ? 13 : -13;
    /* Wir geben immer 0 zurück, auch wenn INT nie low ging — sh2_open
     * versucht es dann ohnehin via Polling-Read.  hal_read meldet 0 bei
     * Leerlauf, und SHTP läuft eventuell trotzdem an. */
    return 0;
}

static void hal_close(sh2_Hal_t *self)
{
    (void)self;
    nrst_low();
    cs_high();
    wake_high();
}

static uint8_t s_tx_dummy[SH2_HAL_MAX_TRANSFER_IN];
static uint8_t s_rx_dump[SH2_HAL_MAX_TRANSFER_OUT];

static int hal_read(sh2_Hal_t *self, uint8_t *pBuf, unsigned len, uint32_t *t_us)
{
    (void)self;
    g_read_calls++;
    *t_us = HAL_GetTick() * 1000u;

    /* Bevorzugt: nur dann lesen wenn INT asserted ist. Wenn INT tot ist
     * (HW-Defekt-Verdacht), nach 50 Calls trotzdem polled-read versuchen,
     * damit der SH2-Stack zumindest eine Chance hat. */
    bool int_now = int_asserted();
    if (!int_now && (g_read_calls % 64) != 0) {
        g_read_skip_int_high++;
        return 0;
    }

    cs_low();

    /* Erste Header-Bytes lesen, dabei führende 0xFF (Chip-Idle / Sleep)
     * tolerieren. */
    uint8_t zero = 0;
    uint8_t hdr[4] = {0};
    if (HAL_SPI_TransmitReceive(&hspi1, &zero, &hdr[0], 1, 50) != HAL_OK) {
        g_read_spi_fail++; cs_high(); return 0;
    }
    unsigned skipped = 0;
    while (hdr[0] == 0xFF && skipped < 64) {
        if (HAL_SPI_TransmitReceive(&hspi1, &zero, &hdr[0], 1, 50) != HAL_OK) {
            g_read_spi_fail++; cs_high(); return 0;
        }
        skipped++;
    }
    if (hdr[0] == 0xFF) {
        g_read_hdr_ff++;
        for (unsigned i = 0; i < 4; i++) g_last_garbage_hdr[i] = hdr[i];
        cs_high();
        return 0;
    }
    if (HAL_SPI_TransmitReceive(&hspi1, &zero, &hdr[1], 3, 50) != HAL_OK) {
        g_read_spi_fail++; cs_high(); return 0;
    }

    if (!g_first_read_buf[0] && !g_first_read_buf[1]) {
        for (unsigned i = 0; i < 4; i++) g_first_read_buf[i] = hdr[i];
        g_first_read_skipped = (uint8_t)skipped;
    }

    unsigned pkt_len = (unsigned)hdr[0] | (((unsigned)hdr[1] & 0x7Fu) << 8);
    if (pkt_len == 0) {
        g_read_hdr_zero++;
        for (unsigned i = 0; i < 4; i++) g_last_garbage_hdr[i] = hdr[i];
        cs_high();
        return 0;
    }
    if (pkt_len > len) {
        g_read_pkt_too_big++;
        cs_high();
        return 0;
    }

    memcpy(pBuf, hdr, 4);
    if (pkt_len > 4) {
        unsigned remaining = pkt_len - 4;
        if (remaining > sizeof(s_tx_dummy)) remaining = sizeof(s_tx_dummy);
        memset(s_tx_dummy, 0, remaining);
        if (HAL_SPI_TransmitReceive(&hspi1, s_tx_dummy, pBuf + 4,
                                    (uint16_t)remaining, 200) != HAL_OK) {
            cs_high();
            return 0;
        }
    }
    cs_high();
    g_reads_with_data++;
    g_last_read_chan = (uint8_t)(hdr[2] & 0xFFu);
    g_last_read_len  = (uint16_t)pkt_len;
    /* Letztes echtes Paket archivieren (Header + erste 32 Payload-Bytes) */
    for (unsigned i = 0; i < 4; i++) g_real_pkt_hdr[i] = hdr[i];
    g_real_pkt_len = (uint16_t)pkt_len;
    unsigned copy = (pkt_len > 4) ? (pkt_len - 4) : 0;
    if (copy > sizeof(g_real_pkt_payload)) copy = sizeof(g_real_pkt_payload);
    for (unsigned i = 0; i < copy; i++) g_real_pkt_payload[i] = pBuf[4 + i];
    return (int)pkt_len;
}

static int hal_write(sh2_Hal_t *self, uint8_t *pBuf, unsigned len)
{
    (void)self;
    if (len == 0 || len > sizeof(s_rx_dump)) return 0;

    g_write_calls++;

    /* Wenn INT asserted ist, hat der Chip noch was zu senden — erst lesen. */
    if (int_asserted()) { g_write_int_gated++; return 0; }

    /* WAKE/PS0 als Wake-Request, kurzes Timeout-Warten auf INT. */
    wake_low();
    bool ready = wait_int_low(100);
    wake_high();
    if (ready) g_writes_ack_int++;
    /* Ohne INT-Bestätigung trotzdem weiter (HW-Workaround). */

    cs_low();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1, pBuf, s_rx_dump,
                                                   (uint16_t)len, 200);
    cs_high();
    if (st == HAL_OK) {
        g_writes_ok++;
        return (int)len;
    }
    g_write_spi_fail++;
    return 0;
}

static uint32_t hal_getTimeUs(sh2_Hal_t *self)
{
    (void)self;
    return HAL_GetTick() * 1000u;
}

static sh2_Hal_t s_hal = {
    .open      = hal_open,
    .close     = hal_close,
    .read      = hal_read,
    .write     = hal_write,
    .getTimeUs = hal_getTimeUs,
};

static volatile bool s_reset_seen = false;

/* EXTI4_IRQHandler liegt in Core/Src/stm32f7xx_it.c (CubeMX-generiert);
 * der ruft HAL_GPIO_EXTI_IRQHandler → diesen Callback hier. */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == BNO_INT_Pin) {
        if (g_exti4_falling_count == 0) {
            g_exti4_first_tick = HAL_GetTick();
        }
        g_exti4_falling_count++;
    }
}

static void event_cb(void *cookie, sh2_AsyncEvent_t *ev)
{
    (void)cookie;
    if (ev->eventId == SH2_RESET) { s_reset_seen = true; g_reset_events++; }
}

bno08x_status_t bno08x_init(void)
{
    g_bno_stage = 1;
    int rc = sh2_open(&s_hal, event_cb, NULL);
    g_sh2_open_rc = rc;
    if (rc != SH2_OK) {
        return BNO08X_ERR_IO;
    }
    g_bno_stage = s_reset_seen ? 21 : 20;

    sh2_ProductIds_t ids;
    memset(&ids, 0, sizeof(ids));
    rc = sh2_getProdIds(&ids);
    g_sh2_prodids_rc = rc;
    if (rc != SH2_OK || ids.numEntries == 0) {
        return BNO08X_ERR_PROTO;
    }
    g_bno_stage = 30;
    return BNO08X_OK;
}

bno08x_status_t bno08x_read_quat(bno08x_quat_t *out)
{
    (void)out;
    return BNO08X_ERR_IO;
}

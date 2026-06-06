#include "framing.h"

#include <string.h>

#include "stm32f7xx_hal.h"
#include "usbd_cdc_if.h"

/* CRC-8/SMBUS: poly=0x07, init=0x00, ref-in=ref-out=false, xor-out=0x00 */
static uint8_t crc8_smbus(const uint8_t *data, uint32_t len)
{
    uint8_t c = 0x00;
    for (uint32_t i = 0; i < len; i++) {
        c ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            c = (c & 0x80u) ? (uint8_t)((c << 1) ^ 0x07u) : (uint8_t)(c << 1);
        }
    }
    return c;
}

bool frame_send(uint8_t type, const uint8_t *payload, uint8_t len)
{
    /* max sinnvolles Frame: header(7) + max_payload(64) + crc(1) = 72 B.
     * Wir bauen das Frame komplett im Stack zusammen und schreiben es in
     * einem Rutsch in die Ring — so kann der Pump immer ein vollständiges
     * Frame in einen 64-B-Bulk-Packet schieben (auch wenn er den Frame
     * dann auf 2 Packets aufteilt — das macht der Host beim CRC-Check
     * automatisch wieder rund). */
    uint8_t buf[FRAME_OVERHEAD + 64];
    if (len > sizeof(buf) - FRAME_OVERHEAD) return false;

    uint32_t t_ms = HAL_GetTick();
    buf[0] = FRAME_SYNC;
    buf[1] = type;
    buf[2] = len;
    buf[3] = (uint8_t)(t_ms >>  0);
    buf[4] = (uint8_t)(t_ms >>  8);
    buf[5] = (uint8_t)(t_ms >> 16);
    buf[6] = (uint8_t)(t_ms >> 24);
    memcpy(&buf[7], payload, len);
    buf[7 + len] = crc8_smbus(&buf[1], (uint32_t)(2 + 4 + len));  /* type..payload */

    /* atomisches Schreiben: nur akzeptieren wenn das ganze Frame passt */
    extern uint32_t usb_cdc_write_atomic(const uint8_t *buf, uint32_t len);
    uint32_t total = FRAME_OVERHEAD + len;
    return usb_cdc_write_atomic(buf, total) == total;
}

/* ── RX-Parser ──────────────────────────────────────────────────────
 * Stream-State-Machine, byteweise gefüttert.  Resilient: bei kaputter
 * CRC oder unbekanntem Typ wird verworfen und auf nächsten SYNC
 * resynct. */
#define RX_BUF_MAX 80u   /* genug für FRAME_OVERHEAD + 64 B Payload */

static frame_cmd_handler_t s_cmd_handler = NULL;
static uint8_t  s_rx_buf[RX_BUF_MAX];
static uint32_t s_rx_len  = 0;

volatile uint32_t g_frame_rx_bytes     = 0;
volatile uint32_t g_frame_rx_ok        = 0;
volatile uint32_t g_frame_rx_crc_err   = 0;
volatile uint32_t g_frame_rx_resync    = 0;

void frame_set_cmd_handler(frame_cmd_handler_t h)
{
    s_cmd_handler = h;
}

/* Erwartete Payload-Länge für einen RX-Frame-Typ.  0 = unbekannt → resync. */
static uint8_t rx_expected_payload(uint8_t type)
{
    switch (type) {
        case FRAME_TYPE_CMD_SET_PAA_CAL:        return FRAME_PAYLOAD_CMD_SET_PAA_CAL;
        case FRAME_TYPE_CMD_SAVE_GYRO_BIAS:     return FRAME_PAYLOAD_CMD_NONE;
        case FRAME_TYPE_CMD_RESET_GYRO_BIAS:    return FRAME_PAYLOAD_CMD_NONE;
        default: return 0;
    }
}

static void rx_drop_front(uint32_t n)
{
    if (n >= s_rx_len) { s_rx_len = 0; return; }
    for (uint32_t i = 0; i < s_rx_len - n; i++) s_rx_buf[i] = s_rx_buf[i + n];
    s_rx_len -= n;
}

static void rx_try_parse(void)
{
    while (1) {
        /* auf SYNC syncen */
        uint32_t sync_idx = 0;
        while (sync_idx < s_rx_len && s_rx_buf[sync_idx] != FRAME_SYNC) sync_idx++;
        if (sync_idx > 0) {
            g_frame_rx_resync++;
            rx_drop_front(sync_idx);
        }
        if (s_rx_len < FRAME_HDR_BYTES) return;

        uint8_t type = s_rx_buf[1];
        uint8_t len  = s_rx_buf[2];
        uint8_t want = rx_expected_payload(type);
        if (want == 0 || len != want) {
            g_frame_rx_resync++;
            rx_drop_front(1);
            continue;
        }

        uint32_t total = FRAME_OVERHEAD + len;
        if (s_rx_len < total) return;

        uint8_t crc_have = s_rx_buf[FRAME_HDR_BYTES + len];
        uint8_t crc_want = crc8_smbus(&s_rx_buf[1], 2u + 4u + len);
        if (crc_have != crc_want) {
            g_frame_rx_crc_err++;
            rx_drop_front(1);
            continue;
        }

        g_frame_rx_ok++;
        if (s_cmd_handler) {
            s_cmd_handler(type, &s_rx_buf[FRAME_HDR_BYTES], len);
        }
        rx_drop_front(total);
    }
}

void frame_feed_rx(const uint8_t *data, uint32_t len)
{
    g_frame_rx_bytes += len;
    while (len > 0) {
        uint32_t free_n = RX_BUF_MAX - s_rx_len;
        if (free_n == 0) {
            /* RX-Buffer voll trotz Parsing — Notbremse: ganzen Buffer
             * wegwerfen und auf nächsten SYNC neu syncen. */
            s_rx_len = 0;
            g_frame_rx_resync++;
            free_n = RX_BUF_MAX;
        }
        uint32_t take = (len < free_n) ? len : free_n;
        for (uint32_t i = 0; i < take; i++) s_rx_buf[s_rx_len + i] = data[i];
        s_rx_len += take;
        data += take;
        len  -= take;
        rx_try_parse();
    }
}

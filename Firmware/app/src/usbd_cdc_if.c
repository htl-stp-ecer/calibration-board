/* CDC class interface: provides the host-side virtual COM port and a
 * thread-safe-ish single-producer TX ring used by the app to push IMU and
 * optical-flow samples. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "usbd_core.h"
#include "usbd_conf.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

#define APP_RX_BUF_SIZE   CDC_DATA_FS_OUT_PACKET_SIZE
#define APP_TX_RING_SIZE  16384U  /* power of 2 — Bursts bei voller ICM-Rate */

static uint8_t s_rx_buf[APP_RX_BUF_SIZE];

static uint8_t  s_tx_ring[APP_TX_RING_SIZE];
static volatile uint32_t s_tx_head;   /* next write index (producer) */
static volatile uint32_t s_tx_tail;   /* next read  index (consumer) */

static volatile uint8_t  s_tx_busy;   /* 1 while a USB IN transfer is in flight */
static uint8_t  s_tx_chunk[CDC_DATA_FS_MAX_PACKET_SIZE];

static volatile uint8_t  s_ready;     /* host configured (SetConfiguration done) */

/* ------------------------------------------------------------------- */
/*                         CDC class callbacks                         */
/* ------------------------------------------------------------------- */
static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, s_tx_chunk, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, s_rx_buf);
    s_tx_busy = 0;
    s_ready = 1;
    return USBD_OK;
}

static int8_t CDC_DeInit_FS(void)
{
    s_ready = 0;
    s_tx_busy = 0;
    return USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)cmd; (void)pbuf; (void)length;
    /* Line coding / control line state ignored — TX-only telemetry channel. */
    return USBD_OK;
}

static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
    /* Host → Device: in den Frame-Parser füttern.  Aufruf erfolgt aus
     * USB-Interrupt-Kontext — die handler in framing.c müssen kurz sein
     * (kein Flash-Write hier; das wird vom Main-Loop-Modul übernommen,
     * das via Flag aufgewacht wird). */
    extern void frame_feed_rx(const uint8_t *data, uint32_t len);
    if (Len && *Len > 0) {
        frame_feed_rx(Buf, *Len);
    }
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, s_rx_buf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
    (void)Buf; (void)Len; (void)epnum;
    /* Chain-TX: sofort den nächsten Packet nachschieben statt auf den
     * nächsten Main-Loop-Pump zu warten.  Damit läuft die USB-TX mit der
     * Host-Drain-Rate (back-to-back Packets) statt 1 Packet/Loop-Iteration
     * — das war der eigentliche Durchsatz-Deckel (~117 Packets/s). */
    s_tx_busy = 0;
    usb_cdc_tx_pump();
    return USBD_OK;
}

USBD_CDC_ItfTypeDef USBD_CDC_fops = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS,
    CDC_TransmitCplt_FS,
};

/* ------------------------------------------------------------------- */
/*                           Public TX path                            */
/* ------------------------------------------------------------------- */
bool usb_cdc_is_ready(void)
{
    return s_ready && hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED;
}

static inline uint32_t ring_used(void)
{
    return (s_tx_head - s_tx_tail) & (APP_TX_RING_SIZE - 1U);
}

static inline uint32_t ring_free(void)
{
    return APP_TX_RING_SIZE - 1U - ring_used();
}

uint32_t usb_cdc_write(const uint8_t *buf, uint32_t len)
{
    if (!usb_cdc_is_ready()) return 0;

    uint32_t avail = ring_free();
    if (len > avail) len = avail;

    for (uint32_t i = 0; i < len; i++) {
        s_tx_ring[(s_tx_head + i) & (APP_TX_RING_SIZE - 1U)] = buf[i];
    }
    s_tx_head = (s_tx_head + len) & (APP_TX_RING_SIZE - 1U);

    usb_cdc_tx_pump();
    return len;
}

/* Atomisches Schreiben: schreibt alles-oder-nichts. Wichtig für binäre
 * Frames — ein Teil-Frame zerschießt den Host-Decoder bis zum nächsten
 * Sync.  Bei voller Ring wird das Frame komplett verworfen und 0
 * zurückgegeben; der Caller weiß damit dass ein Drop passiert ist. */
uint32_t usb_cdc_write_atomic(const uint8_t *buf, uint32_t len)
{
    if (!usb_cdc_is_ready()) return 0;
    if (len > ring_free()) return 0;

    for (uint32_t i = 0; i < len; i++) {
        s_tx_ring[(s_tx_head + i) & (APP_TX_RING_SIZE - 1U)] = buf[i];
    }
    s_tx_head = (s_tx_head + len) & (APP_TX_RING_SIZE - 1U);

    usb_cdc_tx_pump();
    return len;
}

int usb_cdc_printf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((uint32_t)n > sizeof(buf)) n = sizeof(buf);
    return (int)usb_cdc_write((const uint8_t *)buf, (uint32_t)n);
}

/* Schiebt einen Bulk-Packet raus, wenn kein Transfer läuft und Daten da
 * sind.  Wird aus drei Kontexten gerufen: Main-Loop (usb_module.loop),
 * den write()-Pfaden UND (chain-TX) dem TransmitCplt-USB-ISR.  Damit die
 * sich nicht gegenseitig den s_tx_busy/s_tx_tail-Stand zerschießen, läuft
 * der Check+Arm in einer kurzen Critical Section (~wenige µs für 64 B). */
void usb_cdc_tx_pump(void)
{
    if (!usb_cdc_is_ready()) return;

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (s_tx_busy) { __set_PRIMASK(primask); return; }

    uint32_t avail = ring_used();
    if (avail == 0) { __set_PRIMASK(primask); return; }

    /* Drain up to one FS bulk packet (64 B).  Wrap-aware copy. */
    uint32_t chunk = avail > CDC_DATA_FS_MAX_PACKET_SIZE
                     ? CDC_DATA_FS_MAX_PACKET_SIZE : avail;
    for (uint32_t i = 0; i < chunk; i++) {
        s_tx_chunk[i] = s_tx_ring[(s_tx_tail + i) & (APP_TX_RING_SIZE - 1U)];
    }
    s_tx_busy = 1;
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, s_tx_chunk, chunk);
    if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) == USBD_OK) {
        s_tx_tail = (s_tx_tail + chunk) & (APP_TX_RING_SIZE - 1U);
    } else {
        s_tx_busy = 0;  /* try again next pump */
    }

    __set_PRIMASK(primask);
}

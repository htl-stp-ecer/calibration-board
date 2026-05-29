#ifndef USBD_CDC_IF_H
#define USBD_CDC_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "usbd_cdc.h"

extern USBD_CDC_ItfTypeDef USBD_CDC_fops;

/* True once the host has selected the CDC config (i.e. enumeration done). */
bool usb_cdc_is_ready(void);

/* Non-blocking: copies into the TX ring; returns number of bytes accepted
 * (may be less than len if the ring is full, or 0 if the host hasn't
 * enumerated yet).  Safe to call from main loop only — not ISR. */
uint32_t usb_cdc_write(const uint8_t *buf, uint32_t len);

/* Convenience: printf-style line over the VCP. */
int usb_cdc_printf(const char *fmt, ...);

/* Pump pending TX from the ring to the USB stack.  Call periodically from
 * the main loop. */
void usb_cdc_tx_pump(void);

#ifdef __cplusplus
}
#endif
#endif

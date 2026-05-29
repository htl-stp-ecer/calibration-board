#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_def.h"

#define DEVICE_FS  0U

extern USBD_HandleTypeDef hUsbDeviceFS;

/* Wires the CDC class into the USBD stack and starts the device.  Must be
 * called after MX_USB_OTG_FS_PCD_Init().  Also enables OTG_FS_IRQn. */
void MX_USB_DEVICE_Init(void);

#ifdef __cplusplus
}
#endif
#endif

#include <stdio.h>

#include "stm32f7xx_hal.h"
#include "module.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

/* USB-Pump-Modul.  Bringt das CDC-Device hoch und drainiert in jeder
 * Iteration die TX-Ring.  Kein Text-Output mehr (Banner/Heartbeat) —
 * sobald ein Frame-Decoder am Hostend hängt, würden ASCII-Bytes den
 * Sync zerschießen.  Diagnose-Text läuft über printf → UART/SWO. */

static void usb_setup(void)
{
    MX_USB_DEVICE_Init();
    printf("[usb] CDC device started, VID:PID=0483:5740 — binary frame stream\r\n");
}

static void usb_loop(void)
{
    usb_cdc_tx_pump();
}

const module_t usb_module = {
    .name    = "usb",
    .setup   = usb_setup,
    .loop    = usb_loop,
    .enabled = true,
};

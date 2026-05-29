#include <stdio.h>

#include "stm32f7xx_hal.h"
#include "module.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

static bool     s_announced;
static uint32_t s_last_hb_tick;
static uint32_t s_hb_counter;

static void usb_setup(void)
{
    MX_USB_DEVICE_Init();
    printf("[usb] CDC device started, VID:PID=0483:5740 — wait for /dev/ttyACM*\r\n");
}

static void usb_loop(void)
{
    /* Once the host enumerates, push a one-time banner so the user knows
     * which side of the cable is talking. */
    if (!s_announced && usb_cdc_is_ready()) {
        s_announced = true;
        usb_cdc_printf("[usb] raccoon calibration board online\r\n");
    }
    if (s_announced && !usb_cdc_is_ready()) {
        s_announced = false;  /* host disconnected — re-announce on reconnect */
    }

    /* 1 Hz heartbeat — makes the channel visible even when no sensor is
     * producing samples (BNO disabled per HW issue 2, PAA may fail init). */
    uint32_t now = HAL_GetTick();
    if (usb_cdc_is_ready() && (now - s_last_hb_tick) >= 1000u) {
        s_last_hb_tick = now;
        usb_cdc_printf("hb,%lu\r\n", (unsigned long)s_hb_counter++);
    }

    /* Drain whatever the sensor modules queued this iteration. */
    usb_cdc_tx_pump();
}

const module_t usb_module = {
    .name    = "usb",
    .setup   = usb_setup,
    .loop    = usb_loop,
    .enabled = true,
};

#include "usb_device.h"
#include "stm32f7xx_hal.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"

USBD_HandleTypeDef hUsbDeviceFS;

void MX_USB_DEVICE_Init(void)
{
    if (USBD_Init(&hUsbDeviceFS, &CDC_Desc, DEVICE_FS) != USBD_OK)        return;
    if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC) != USBD_OK)          return;
    if (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_CDC_fops) != USBD_OK) return;

    /* NVIC isn't configured for OTG_FS_IRQn in the .ioc; do it here.
     * Priority 6 sits below EXTI4 (BNO INT, prio 5) so the IMU edge can
     * still preempt the USB ISR. */
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);

    USBD_Start(&hUsbDeviceFS);
}

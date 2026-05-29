#ifndef USBD_CONF_H
#define USBD_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32f7xx.h"
#include "stm32f7xx_hal.h"

#define USBD_MAX_NUM_INTERFACES               1U
#define USBD_MAX_NUM_CONFIGURATION            1U
#define USBD_MAX_STR_DESC_SIZ                 512U
#define USBD_SELF_POWERED                     1U
#define USBD_DEBUG_LEVEL                      0U

#define USBD_CDC_INTERVAL                     1000U

/* Static allocation: avoid heap exhaustion (HeapSize=0x200 in linker script). */
#define USBD_malloc          (void *)USBD_static_malloc
#define USBD_free            USBD_static_free
#define USBD_memset          memset
#define USBD_memcpy          memcpy
#define USBD_Delay           HAL_Delay

#define USBD_UsrLog(...)     do {} while (0)
#define USBD_ErrLog(...)     do {} while (0)
#define USBD_DbgLog(...)     do {} while (0)

void *USBD_static_malloc(uint32_t size);
void  USBD_static_free(void *p);

#ifdef __cplusplus
}
#endif
#endif

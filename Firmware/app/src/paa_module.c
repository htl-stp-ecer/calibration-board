#include <stdio.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "module.h"
#include "paa5100.h"

static paa5100_status_t s_init_status = PAA5100_ERR_IO;
static uint32_t         s_last_print_tick;
static uint32_t         s_tick;

volatile int g_paa_init_status = 0xDEADBEEF;

static void paa_setup(void)
{
    printf("[paa] init...\r\n");
    s_init_status = paa5100_init();
    g_paa_init_status = (int)s_init_status;
    printf("[paa] init -> %d (id=0x%02x rev=0x%02x)\r\n",
           (int)s_init_status,
           paa5100_get_product_id(),
           paa5100_get_revision_id());
}

static void paa_loop(void)
{
    if (s_init_status != PAA5100_OK) return;

    uint32_t now = HAL_GetTick();
    if ((now - s_last_print_tick) < 100u) return;  /* 10 Hz Print-Rate */
    s_last_print_tick = now;

    paa5100_motion_t m;
    if (paa5100_read_motion(&m) == PAA5100_OK) {
        printf("[paa t=%lu] dx=%6d dy=%6d squal=%3u motion=0x%02x\r\n",
               (unsigned long)s_tick++, m.dx, m.dy, m.squal, m.motion);
    }
}

const module_t paa_module = {
    .name    = "paa",
    .setup   = paa_setup,
    .loop    = paa_loop,
    .enabled = false,
};

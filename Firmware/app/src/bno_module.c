#include <stdio.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "module.h"
#include "bno08x.h"

static bno08x_status_t s_init_status = BNO08X_ERR_IO;
static bno08x_quat_t   s_last_quat;
static uint32_t        s_last_print_tick;

volatile int g_bno_init_status = 0xDEADBEEF;

static void bno_setup(void)
{
    printf("[bno] init...\r\n");
    s_init_status = bno08x_init();
    g_bno_init_status = (int)s_init_status;
    printf("[bno] init -> %d\r\n", (int)s_init_status);
}

static void bno_loop(void)
{
    if (s_init_status != BNO08X_OK) return;

    /* Pro Sekunde max einen Read + Print. */
    uint32_t now = HAL_GetTick();
    if ((now - s_last_print_tick) < 1000u) return;
    s_last_print_tick = now;

    if (bno08x_read_quat(&s_last_quat) == BNO08X_OK) {
        printf("[bno] q=(%.3f, %.3f, %.3f, %.3f) acc=%.3f rad\r\n",
               (double)s_last_quat.w, (double)s_last_quat.x,
               (double)s_last_quat.y, (double)s_last_quat.z,
               (double)s_last_quat.accuracy_rad);
    }
}

/* enabled=false bis Issue 2 (CS-Pfad PA4↔BNO Pin 18) hardwareseitig geklärt. */
const module_t bno_module = {
    .name    = "bno",
    .setup   = bno_setup,
    .loop    = bno_loop,
    .enabled = true,
};

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "module.h"
#include "framing.h"

/* 1 Hz Status-Frame über USB-CDC.  Erlaubt dem Host präzise Aussagen
 * über Sensor-Zustand (PAA verbunden?  ICM init OK?  Drop-Rate?) ohne
 * dass er sich aus Frame-Raten was zusammenraten muss.  Die Globals
 * werden in den jeweiligen Sensor-Modulen gepflegt. */

extern volatile int      g_icm_module_init_status;
extern volatile uint32_t g_icm_sample_count;
extern volatile uint32_t g_icm_dropped_frames;

extern volatile int      g_paa_init_status;
extern volatile uint32_t g_paa_init_attempts;
extern volatile uint32_t g_paa_reconnect_count;
extern volatile uint32_t g_paa_disconnect_count;
extern volatile uint32_t g_paa_dropped_frames;

/* PAA-Treiber hält product_id zur Identifikation; 0 = noch nie gesehen. */
extern volatile uint8_t  g_paa_product_id;

static uint32_t s_last_emit;

static void status_setup(void)
{
    s_last_emit = HAL_GetTick();
}

static void status_loop(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - s_last_emit) < 1000u) return;
    s_last_emit = now;

    uint8_t payload[FRAME_PAYLOAD_STATUS];
    int8_t   icm_st  = (int8_t)g_icm_module_init_status;
    int8_t   paa_st  = (int8_t)g_paa_init_status;
    uint8_t  paa_seen = g_paa_product_id != 0u ? 1u : 0u;
    uint32_t icm_n   = g_icm_sample_count;
    uint32_t icm_drp = g_icm_dropped_frames;
    uint32_t paa_n   = g_paa_init_attempts;
    uint16_t paa_rc  = (uint16_t)g_paa_reconnect_count;
    uint16_t paa_dc  = (uint16_t)g_paa_disconnect_count;

    memcpy(&payload[0],  &icm_st,   1);
    memcpy(&payload[1],  &paa_st,   1);
    memcpy(&payload[2],  &paa_seen, 1);
    payload[3] = 0;
    memcpy(&payload[4],  &icm_n,    4);
    memcpy(&payload[8],  &icm_drp,  4);
    memcpy(&payload[12], &paa_n,    4);
    memcpy(&payload[16], &paa_rc,   2);
    memcpy(&payload[18], &paa_dc,   2);

    (void)frame_send(FRAME_TYPE_STATUS, payload, FRAME_PAYLOAD_STATUS);
}

const module_t status_module = {
    .name    = "status",
    .setup   = status_setup,
    .loop    = status_loop,
    .enabled = true,
};

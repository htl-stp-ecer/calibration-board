#include <stdio.h>
#include <string.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "module.h"
#include "paa5100.h"
#include "framing.h"

/* PAA-Modul: optischer Flow.  Pollt so schnell wie SPI2 das hergibt
 * (Treiber-intern ~375 kHz, deutlich langsamer als ICM) und schickt jedes
 * Sample als binäres Frame über USB-CDC.  PAA selber liefert nur ein
 * neues Sample wenn motion-bit gesetzt ist — wir senden trotzdem alle,
 * damit der Host konstantes Heartbeat-/Squal-Tracking hat. */

/* PAA-Sensor ist hot-pluggable: er darf beim Boot fehlen, später dazukommen
 * oder im Betrieb weggehen.  Strategie:
 *   - init beim Setup einmal versuchen
 *   - bei nicht-OK: alle PAA_RETRY_INTERVAL_MS einen erneuten init() probieren
 *   - bei OK: lesen; wenn 3 reads in Folge IO-Fehler → als disconnected
 *     markieren und in den Retry-Loop zurück
 *   - kein Blocken des ICM-Streams (Retry ist nur ein einzelner SPI-Burst) */
#define PAA_RETRY_INTERVAL_MS  2000U
#define PAA_IO_FAIL_THRESHOLD  3U

static paa5100_status_t s_init_status = PAA5100_ERR_IO;
static uint32_t         s_next_retry_tick;
static uint32_t         s_consec_io_fail;

volatile int      g_paa_init_status      = 0xDEADBEEF;
volatile uint32_t g_paa_dropped_frames   = 0;
volatile uint32_t g_paa_init_attempts    = 0;
volatile uint32_t g_paa_reconnect_count  = 0;
volatile uint32_t g_paa_disconnect_count = 0;

static void try_init(void)
{
    g_paa_init_attempts++;
    s_init_status = paa5100_init();
    g_paa_init_status = (int)s_init_status;
    if (s_init_status == PAA5100_OK) {
        s_consec_io_fail = 0;
        if (g_paa_init_attempts > 1) {
            g_paa_reconnect_count++;
            printf("[paa] reconnected (attempt %lu, id=0x%02x rev=0x%02x)\r\n",
                   (unsigned long)g_paa_init_attempts,
                   paa5100_get_product_id(),
                   paa5100_get_revision_id());
        } else {
            printf("[paa] init OK (id=0x%02x rev=0x%02x)\r\n",
                   paa5100_get_product_id(),
                   paa5100_get_revision_id());
        }
    }
}

static void paa_setup(void)
{
    printf("[paa] init attempt 1...\r\n");
    try_init();
    if (s_init_status != PAA5100_OK) {
        printf("[paa] not present (status=%d) — will retry every %lu ms\r\n",
               (int)s_init_status, (unsigned long)PAA_RETRY_INTERVAL_MS);
        s_next_retry_tick = HAL_GetTick() + PAA_RETRY_INTERVAL_MS;
    }
}

static void paa_loop(void)
{
    if (s_init_status != PAA5100_OK) {
        /* Hot-plug-Retry: gelegentlich erneut versuchen */
        if ((int32_t)(HAL_GetTick() - s_next_retry_tick) >= 0) {
            try_init();
            s_next_retry_tick = HAL_GetTick() + PAA_RETRY_INTERVAL_MS;
        }
        return;
    }

    paa5100_motion_t m;
    if (paa5100_read_motion(&m) != PAA5100_OK) {
        if (++s_consec_io_fail >= PAA_IO_FAIL_THRESHOLD) {
            /* Sensor scheint weg.  Init-Status auf Fehler, Retry-Loop
             * übernimmt ab nächster Iteration. */
            s_init_status     = PAA5100_ERR_IO;
            g_paa_init_status = (int)s_init_status;
            g_paa_disconnect_count++;
            s_consec_io_fail  = 0;
            s_next_retry_tick = HAL_GetTick() + PAA_RETRY_INTERVAL_MS;
            printf("[paa] disconnected (3 consecutive IO fails) — retrying\r\n");
        }
        return;
    }
    s_consec_io_fail = 0;

    /* Payload Layout: dx,dy (int16 LE), squal, shutter_hi, shutter_lo, motion */
    uint8_t payload[FRAME_PAYLOAD_PAA];
    memcpy(&payload[0], &m.dx, 2);
    memcpy(&payload[2], &m.dy, 2);
    payload[4] = m.squal;
    payload[5] = m.shutter_hi;
    payload[6] = m.shutter_lo;
    payload[7] = m.motion;
    payload[8] = 0;  /* reserved/padding */
    payload[9] = 0;

    if (!frame_send(FRAME_TYPE_PAA, payload, FRAME_PAYLOAD_PAA)) {
        g_paa_dropped_frames++;
    }
}

const module_t paa_module = {
    .name    = "paa",
    .setup   = paa_setup,
    .loop    = paa_loop,
    .enabled = true,
};

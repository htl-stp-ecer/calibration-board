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

/* Event-getrieben: nicht mit voller Loop-Rate (~1 kHz) Nullen pollen.
 * Alle PAA_POLL_PERIOD_MS einmal lesen (dx/dy akkumulieren on-chip, also
 * geht nichts verloren) und ein Frame NUR senden wenn echte Bewegung
 * anliegt — dann „kriegt der Host immer was Neues wenn der PAA was Neues
 * hat".  Ein langsamer Heartbeat hält trotzdem die Liveness/Squal-Anzeige
 * am Laufen, auch wenn der Roboter steht. */
#define PAA_POLL_PERIOD_MS   2u    /* 500 Hz Poll — responsive, 60% weniger Reads als 1254 Hz */
#define PAA_HEARTBEAT_MS     200u  /* 5 Hz: ein Frame auch ohne Bewegung (Liveness + Squal) */

static paa5100_status_t s_init_status = PAA5100_ERR_IO;
static uint32_t         s_next_retry_tick;
static uint32_t         s_next_sample_tick;
static uint32_t         s_next_heartbeat;
static uint32_t         s_consec_io_fail;

/* Frei laufender signed Counts-Akkumulator (Board-Integration).  Der
 * Host akkumuliert nicht mehr; der Kalibrier-Wizard nimmt Differenzen.
 * Bei jedem (Re-)Init auf 0 gesetzt, damit ein Reconnect sauber startet. */
static int32_t s_acc_x;
static int32_t s_acc_y;

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
        s_acc_x = 0;
        s_acc_y = 0;
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

    /* Poll-Rate-Limit: alle PAA_POLL_PERIOD_MS einmal lesen statt jede
     * Loop-Iteration.  Spart die ~0.27 ms Burst-Read-Zeit der Iterationen
     * dazwischen — kein Verlust, weil der PAA dx/dy on-chip akkumuliert. */
    if ((int32_t)(HAL_GetTick() - s_next_sample_tick) < 0) return;
    s_next_sample_tick = HAL_GetTick() + PAA_POLL_PERIOD_MS;

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

    /* Counts IMMER akkumulieren (on-chip-Delta nicht verlieren); bei
     * Stillstand ist dx=dy=0, also No-op. */
    s_acc_x += m.dx;
    s_acc_y += m.dy;

    /* Event-getrieben senden: nur bei echter Bewegung — sonst nur ein
     * langsamer Heartbeat (Liveness + Squal).  So kriegt der Host genau
     * dann ein neues PAA-Frame, wenn der Sensor was Neues hat; bei
     * Stillstand kein Nullen-Spam. */
    const bool paa_moved     = (m.dx != 0) || (m.dy != 0);
    const bool paa_heartbeat = (int32_t)(HAL_GetTick() - s_next_heartbeat) >= 0;
    if (!paa_moved && !paa_heartbeat) return;
    if (paa_heartbeat) s_next_heartbeat = HAL_GetTick() + PAA_HEARTBEAT_MS;

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

    /* Aktuellen Akkumulator-Stand mitsenden (Wizard liest nur die
     * Start-/End-Differenz).  Akkumuliert wurde oben bereits. */
    uint8_t acc[FRAME_PAYLOAD_PAA_ACC];
    memcpy(&acc[0], &s_acc_x, 4);
    memcpy(&acc[4], &s_acc_y, 4);
    if (!frame_send(FRAME_TYPE_PAA_ACC, acc, FRAME_PAYLOAD_PAA_ACC)) {
        g_paa_dropped_frames++;
    }
}

const module_t paa_module = {
    .name    = "paa",
    .setup   = paa_setup,
    .loop    = paa_loop,
    .enabled = true,
};

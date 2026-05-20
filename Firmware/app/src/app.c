#include "app.h"

#include "main.h"
#include "stm32f7xx_hal.h"

#include "bno08x.h"
#include "paa5100.h"

volatile int g_bno_init_status = 0xDEADBEEF;
volatile int g_paa_init_status = 0xDEADBEEF;

static void blink_pattern(uint32_t on_ms, uint32_t off_ms, uint32_t loops)
{
    for (uint32_t i = 0; i < loops; i++) {
        HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET);
        HAL_Delay(on_ms);
        HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);
        HAL_Delay(off_ms);
    }
}

void app_main(void)
{
    /* BNO temporarily disabled to isolate PAA bringup — see hardware-issues.md
     * for HW-defect status of the INT-line. */
    bno08x_status_t bno_st = BNO08X_ERR_IO;
    g_bno_init_status = (int)bno_st;

    paa5100_status_t paa_st = paa5100_init();
    g_paa_init_status = (int)paa_st;

    /* LED-Schema: ein Status-Telegramm pro 4 s
     *   N kurze Pulse → BNO-Status  (1=OK, 2=Fehler)
     *   Pause
     *   M kurze Pulse → PAA-Status  (1=OK, 2=Fehler)
     *   lange Pause
     * Wenn beides OK → schneller Heartbeat.
     */
    paa5100_motion_t m;

    while (1) {
        if (bno_st == BNO08X_OK && paa_st == PAA5100_OK) {
            blink_pattern(100, 100, 1);
            paa5100_read_motion(&m);
        } else {
            /* BNO-Status */
            blink_pattern(50, 200, (bno_st == BNO08X_OK) ? 1 : 2);
            HAL_Delay(500);
            /* PAA-Status */
            blink_pattern(50, 200, (paa_st == PAA5100_OK) ? 1 : 2);
            HAL_Delay(1500);
            if (paa_st == PAA5100_OK) paa5100_read_motion(&m);
        }
    }
}

#include "app.h"

#include <stdio.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "bno08x.h"
#include "paa5100.h"
#include "swo.h"

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
    /* SWO/ITM printf on PB3. 2 MHz is the safe ceiling for ST-LINK V2. */
    swo_init(HAL_RCC_GetHCLKFreq(), 1000000U);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[boot] calibration-board up, HCLK=%lu Hz\r\n",
           (unsigned long)HAL_RCC_GetHCLKFreq());

    printf("[init] bno08x_init starting...\r\n");
    bno08x_status_t bno_st = bno08x_init();
    g_bno_init_status = (int)bno_st;
    printf("[init] bno08x_init -> %d\r\n", (int)bno_st);

    printf("[init] paa5100_init starting...\r\n");
    paa5100_status_t paa_st = paa5100_init();
    g_paa_init_status = (int)paa_st;
    printf("[init] paa5100_init -> %d (id=0x%02x rev=0x%02x)\r\n",
           (int)paa_st, paa5100_get_product_id(), paa5100_get_revision_id());

    /* LED-Schema: ein Status-Telegramm pro 4 s
     *   N kurze Pulse → BNO-Status  (1=OK, 2=Fehler)
     *   Pause
     *   M kurze Pulse → PAA-Status  (1=OK, 2=Fehler)
     *   lange Pause
     * Wenn beides OK → schneller Heartbeat.
     */
    paa5100_motion_t m;
    uint32_t tick = 0;

    while (1) {
        if (paa_st == PAA5100_OK) {
            paa5100_read_motion(&m);
            printf("[t=%lu] paa dx=%6d dy=%6d squal=%3u motion=0x%02x\r\n",
                   (unsigned long)tick++, m.dx, m.dy, m.squal, m.motion);
        } else {
            printf("[t=%lu] paa offline (init rc=%d)\r\n",
                   (unsigned long)tick++, (int)paa_st);
        }

        if (bno_st == BNO08X_OK && paa_st == PAA5100_OK) {
            blink_pattern(100, 100, 1);
        } else {
            blink_pattern(50, 200, (bno_st == BNO08X_OK) ? 1 : 2);
            HAL_Delay(300);
            blink_pattern(50, 200, (paa_st == PAA5100_OK) ? 1 : 2);
            HAL_Delay(500);
        }
    }
}

#include "main.h"
#include "stm32f7xx_hal.h"

#include "module.h"
#include "bno08x.h"

/* App-Test für BNO:
 *   USER_LED 1 Hz blinkt  → init failed
 *   USER_LED konstant an  → init OK, im Loop wird mit der BNO geredet
 * Sonst keine Logik. */

static bool       s_initialized;
static bno08x_quat_t s_quat;
static uint32_t   s_blink_tick;
static bool       s_blink_state;

static void bno_setup(void)
{
    HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);
}

static void bno_loop(void)
{
    if (!s_initialized) {
        if (bno08x_init() == BNO08X_OK) {
            s_initialized = true;
            HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET);
        } else {
            uint32_t now = HAL_GetTick();
            if ((now - s_blink_tick) >= 1000u) {
                s_blink_tick  = now;
                s_blink_state = !s_blink_state;
                HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin,
                                  s_blink_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
            }
        }
        return;
    }

    (void)bno08x_read_quat(&s_quat);
}

const module_t bno_module = {
    .name    = "bno",
    .setup   = bno_setup,
    .loop    = bno_loop,
    .enabled = false,  /* BNO08x defekt — übergangsweise aus */
};

#include "app.h"

#include <stdio.h>
#include <stddef.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "module.h"
#include "swo.h"

/* Modul-Registry — neue Sensoren / Funktionen hier registrieren.  Jedes
 * Modul liefert ein eigenes `setup()` (einmalig beim Boot) und `loop()`
 * (pro Iteration). `enabled` schaltet ein Modul übergangsweise weg, ohne
 * den Eintrag zu löschen. */
extern const module_t bno_module;
extern const module_t paa_module;

static const module_t *const MODULES[] = {
    &bno_module,
    &paa_module,
};
#define NUM_MODULES (sizeof(MODULES) / sizeof(MODULES[0]))

static void run_setup(void) {
    for (size_t i = 0; i < NUM_MODULES; i++) {
        const module_t *m = MODULES[i];
        if (!m->enabled) {
            printf("[setup] %s SKIPPED (disabled)\r\n", m->name);
            continue;
        }
        if (m->setup) m->setup();
    }
}

static void run_loop_iteration(void) {
    for (size_t i = 0; i < NUM_MODULES; i++) {
        const module_t *m = MODULES[i];
        if (!m->enabled || !m->loop) continue;
        m->loop();
    }
}

void app_main(void) {
    /* SWO/ITM printf on PB3.  ST-LINK V2 (Clone) routet das in der Praxis
     * nicht durch; ST-LINK V3 schon.  UART4 ist die zuverlässige Quelle. */
    swo_init(HAL_RCC_GetHCLKFreq(), 1000000U);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[boot] calibration-board up, HCLK=%lu Hz\r\n",
           (unsigned long) HAL_RCC_GetHCLKFreq());

    run_setup();

    while (1) {
        HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);
        run_loop_iteration();
    }
}

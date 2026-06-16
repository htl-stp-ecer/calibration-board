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
extern const module_t paa_cal_module;
extern const module_t icm_module;
extern const module_t status_module;
extern const module_t usb_module;

/* usb_module FIRST: setup() starts the CDC stack, so it's available when
 * the sensor modules later try to push samples. */
static const module_t *const MODULES[] = {
    &usb_module,
    &bno_module,
    &paa_module,
    &paa_cal_module,
    &icm_module,
    &status_module,
};
#define NUM_MODULES (sizeof(MODULES) / sizeof(MODULES[0]))

/* Soft-Start: Module gestaffelt hochfahren statt alle gleichzeitig.  Jeder
 * Sensor/Peripherie-Block zieht beim Bring-up Strom; gleichzeitig = ein
 * Step, der eine grenzwertige Versorgung in den Brownout zieht.  Die kurze
 * Pause lässt den Strom jedes Blocks setteln, bevor der nächste dazukommt
 * → flacherer Anstieg.  (Der Kondensator-Inrush beim Einstecken liegt VOR
 * dieser Phase und ist reine Hardware — das glättet sie NICHT.) */
#define SETUP_STAGGER_MS  25u

static void run_setup(void)
{
    for (size_t i = 0; i < NUM_MODULES; i++) {
        const module_t *m = MODULES[i];
        if (!m->enabled) {
            printf("[setup] %s SKIPPED (disabled)\r\n", m->name);
            continue;
        }
        if (m->setup) m->setup();
        HAL_Delay(SETUP_STAGGER_MS);
    }
}

static void run_loop_iteration(void)
{
    for (size_t i = 0; i < NUM_MODULES; i++) {
        const module_t *m = MODULES[i];
        if (!m->enabled || !m->loop) continue;
        m->loop();
    }
}

void app_main(void)
{
    /* SWO/ITM printf on PB3.  ST-LINK V2 (Clone) routet das in der Praxis
     * nicht durch; ST-LINK V3 schon.  UART4 ist die zuverlässige Quelle. */
    swo_init(HAL_RCC_GetHCLKFreq(), 1000000U);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[boot] calibration-board up, HCLK=%lu Hz\r\n",
           (unsigned long)HAL_RCC_GetHCLKFreq());

    run_setup();

    while (1) {
        run_loop_iteration();
        /* Kein __WFI: PAA wird jede Iteration gelesen (~1 kHz, Wunsch:
         * maximale Rate) → der Loop hat ohnehin kaum Leerlauf, WFI würde
         * also kaum Strom sparen, machte aber SWD-HOTPLUG-Debugging
         * unzuverlässig (schlafender Core).  Power-Saving kam über den
         * 192->96-MHz-Takt — der scheiterte aber an der PAA-SPI; daher
         * vorerst bei 192 MHz. */
    }
}

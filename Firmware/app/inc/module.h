#pragma once

#include <stdbool.h>

/* Arduino-style module interface. Jedes Modul wird einmal `setup()`
 * aufgerufen und dann pro Main-Loop-Iteration `loop()`. `enabled` kann auf
 * false gesetzt werden um ein Modul übergangsweise zu deaktivieren (z. B.
 * solange ein Hardware-Issue offen ist) — setup/loop werden dann komplett
 * übersprungen. */
typedef struct {
    const char *name;
    void (*setup)(void);  /* darf NULL sein */
    void (*loop)(void);   /* darf NULL sein */
    bool enabled;
} module_t;

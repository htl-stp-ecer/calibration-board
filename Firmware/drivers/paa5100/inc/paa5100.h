#pragma once

#include <stdint.h>

typedef enum {
    PAA5100_OK = 0,
    PAA5100_ERR_IO = -1,
    PAA5100_ERR_ID = -2,
    PAA5100_ERR_TIMEOUT = -3,
} paa5100_status_t;

typedef struct {
    int16_t dx;
    int16_t dy;
    uint8_t squal;       /* surface quality (0..169) */
    uint8_t shutter_hi;  /* high byte of shutter */
    uint8_t shutter_lo;  /* low byte of shutter */
    uint8_t motion;      /* bit 7 = data ready */
} paa5100_motion_t;

paa5100_status_t paa5100_init(void);
paa5100_status_t paa5100_read_motion(paa5100_motion_t *out);
uint8_t          paa5100_get_product_id(void);
uint8_t          paa5100_get_revision_id(void);

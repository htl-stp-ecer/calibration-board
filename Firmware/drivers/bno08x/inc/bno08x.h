#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BNO08X_OK = 0,
    BNO08X_ERR_IO = -1,
    BNO08X_ERR_TIMEOUT = -2,
    BNO08X_ERR_PROTO = -3,
} bno08x_status_t;

typedef struct {
    float w, x, y, z;
    float accuracy_rad;
} bno08x_quat_t;

bno08x_status_t bno08x_init(void);
bno08x_status_t bno08x_read_quat(bno08x_quat_t *out);

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ICM42688P_WHO_AM_I_EXPECTED 0x47u

typedef enum {
    ICM42688P_OK            =  0,
    ICM42688P_ERR_IO        = -1,
    ICM42688P_ERR_WHO_AM_I  = -2,
    ICM42688P_ERR_SPI_CFG   = -3,
} icm42688p_status_t;

typedef struct {
    int16_t ax, ay, az;   /* Accel raw counts */
    int16_t gx, gy, gz;   /* Gyro  raw counts */
    int16_t temp;         /* Temperature raw */
} icm42688p_sample_t;

icm42688p_status_t icm42688p_init(void);
icm42688p_status_t icm42688p_read_sample(icm42688p_sample_t *out);
uint8_t            icm42688p_get_who_am_i(void);

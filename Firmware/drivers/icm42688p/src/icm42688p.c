/* TDK InvenSense ICM-42688-P 6-axis IMU driver.
 * Bus:   SPI3 (PC10 SCK / PC11 MISO / PC12 MOSI)
 * CS:    PD2  (SPI3_CS1, active low)
 * INT:   PA15 (ICM_INT, EXTI15) — nicht von diesem Treiber benutzt
 *
 * CubeMX initialisiert SPI3 mit DataSize=4BIT — wir re-initialisieren auf
 * 8-bit, Mode 0, ~1.7 MHz (APB1=54 MHz / prescaler 32).  ICM verträgt bis
 * 24 MHz, für die Bring-up Phase reicht langsam aus. */

#include "icm42688p.h"

#include "main.h"
#include "stm32f7xx_hal.h"

extern SPI_HandleTypeDef hspi3;

/* Register map (User Bank 0, default) */
#define REG_DEVICE_CONFIG        0x11
#define REG_PWR_MGMT0            0x4E
#define REG_GYRO_CONFIG0         0x4F
#define REG_ACCEL_CONFIG0        0x50
#define REG_GYRO_CONFIG1         0x51
#define REG_GYRO_ACCEL_CONFIG0   0x52
#define REG_ACCEL_CONFIG1        0x53
#define REG_TEMP_DATA1           0x1D
#define REG_WHO_AM_I             0x75

/* ── Konfiguration für Roboter-Orientierungs-Fusion (Madgwick/Mahony/EKF) ──
 *
 * ODR = 1 kHz   → reicht für Fusion bis ~500 Hz Loop-Rate, Headroom für
 *                 Integrationsstabilität bei hohen Drehraten.
 * FS:
 *   Gyro  ±2000 dps  →  0.061 dps/LSB. Sicher gegen schnelle Spins
 *                       (Botball-Robot ~1-2 U/s, viel Reserve).
 *   Accel ±4 g       →  0.122 mg/LSB. Robot sieht praktisch nie >4 g —
 *                       4× bessere Auflösung als Default ±16 g, was die
 *                       Tilt-Compensation des Fusionsfilters massiv
 *                       verbessert.
 * UI-Filter Bandbreite = ODR/10 = 100 Hz  →  schluckt Motor-/Getriebe-
 *                       Vibrationen oberhalb ~100 Hz, lässt aber die
 *                       reale Roboter-Dynamik (<50 Hz) intakt.
 * Filter-Ordnung 3rd order (Default)  →  steiler Roll-Off, geringe
 *                       Phase im Pass-Band.
 *
 * Codierung der Register (Datasheet §14):
 *   GYRO_CONFIG0  bits[7:5]=FS, bits[3:0]=ODR
 *     FS:  000=±2000 dps
 *     ODR: 0110=1 kHz
 *     → 0x06
 *   ACCEL_CONFIG0 bits[7:5]=FS, bits[3:0]=ODR
 *     FS:  010=±4 g
 *     ODR: 0110=1 kHz
 *     → 0x46
 *   GYRO_ACCEL_CONFIG0 bits[7:4]=ACCEL_UI_FILT_BW, bits[3:0]=GYRO_UI_FILT_BW
 *     4 = ODR/10  →  bei 1 kHz ODR = 100 Hz BW
 *     → 0x44
 */
#define ICM_GYRO_CONFIG0_VAL         0x06  /* ±2000 dps,  1 kHz */
#define ICM_ACCEL_CONFIG0_VAL        0x46  /* ±4 g,       1 kHz */
#define ICM_GYRO_ACCEL_CONFIG0_VAL   0x44  /* UI BW = 100 Hz für beide */

/* LSB-Faktoren zur Umrechnung der g_icm_*-Globals in physikalische Einheiten.
 * Bei ±2000 dps:   32768 / 2000   = 16.384  LSB/dps
 * Bei ±4 g:        32768 / 4      = 8192    LSB/g
 * Temperatur:      raw/132.48 + 25 °C  (Datasheet §14.9) */
#define ICM_GYRO_LSB_PER_DPS    16.384f
#define ICM_ACCEL_LSB_PER_G     8192.0f

#define SPI_READ_BIT        0x80

/* Debug-Globals — über SWD lesbar (STM32_Programmer_CLI -r32 <addr> 4).
 * Adressen via `arm-none-eabi-nm build/Debug/Firmware.elf | grep g_icm`. */
volatile int     g_icm_init_status = 0xDEADBEEF;
volatile int     g_icm_stage       = 0;
volatile uint8_t g_icm_who_am_i    = 0;
volatile int16_t g_icm_ax, g_icm_ay, g_icm_az;
volatile int16_t g_icm_gx, g_icm_gy, g_icm_gz;
volatile int16_t g_icm_temp;
volatile uint32_t g_icm_sample_count = 0;

static inline void cs_low(void)  { HAL_GPIO_WritePin(SPI3_CS1_GPIO_Port, SPI3_CS1_Pin, GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(SPI3_CS1_GPIO_Port, SPI3_CS1_Pin, GPIO_PIN_SET);   }

static HAL_StatusTypeDef spi_xfer(uint8_t *tx, uint8_t *rx, uint16_t len)
{
    cs_low();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi3, tx, rx, len, 100);
    cs_high();
    return st;
}

static HAL_StatusTypeDef reg_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg & 0x7F, val };
    uint8_t rx[2];
    return spi_xfer(tx, rx, 2);
}

static HAL_StatusTypeDef reg_read(uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = { (uint8_t)(reg | SPI_READ_BIT), 0x00 };
    uint8_t rx[2] = { 0 };
    HAL_StatusTypeDef st = spi_xfer(tx, rx, 2);
    *val = rx[1];
    return st;
}

static HAL_StatusTypeDef reg_read_burst(uint8_t reg, uint8_t *buf, uint16_t n)
{
    /* Ein Transfer: 1 Byte Adresse + n Bytes Daten.  Für CubeMX-HAL
     * brauchen wir TX und RX gleich lang; TX bleibt nach Byte 0 0x00. */
    uint8_t tx[16] = { (uint8_t)(reg | SPI_READ_BIT) };
    uint8_t rx[16] = { 0 };
    if (n + 1u > sizeof(tx)) return HAL_ERROR;
    HAL_StatusTypeDef st = spi_xfer(tx, rx, (uint16_t)(n + 1u));
    for (uint16_t i = 0; i < n; i++) buf[i] = rx[i + 1u];
    return st;
}

static icm42688p_status_t spi_reinit(void)
{
    HAL_SPI_DeInit(&hspi3);
    hspi3.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi3.Init.CLKPolarity       = SPI_POLARITY_LOW;   /* Mode 0 */
    hspi3.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi3.Init.NSS               = SPI_NSS_SOFT;
    hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;  /* APB1=54 MHz/4 = 13.5 MHz (ICM max 24 MHz) */
    hspi3.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi3.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi3.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&hspi3) != HAL_OK) return ICM42688P_ERR_SPI_CFG;
    return ICM42688P_OK;
}

icm42688p_status_t icm42688p_init(void)
{
    g_icm_stage = 1;
    cs_high();

    if (spi_reinit() != ICM42688P_OK) {
        g_icm_stage = -1;
        g_icm_init_status = ICM42688P_ERR_SPI_CFG;
        return ICM42688P_ERR_SPI_CFG;
    }
    g_icm_stage = 2;

    /* Soft-Reset: REG_DEVICE_CONFIG bit0 = soft_reset_config */
    if (reg_write(REG_DEVICE_CONFIG, 0x01) != HAL_OK) {
        g_icm_stage = -2;
        g_icm_init_status = ICM42688P_ERR_IO;
        return ICM42688P_ERR_IO;
    }
    HAL_Delay(2);  /* Datasheet: 1 ms reset time */
    g_icm_stage = 3;

    /* WHO_AM_I lesen */
    uint8_t whoami = 0;
    if (reg_read(REG_WHO_AM_I, &whoami) != HAL_OK) {
        g_icm_stage = -3;
        g_icm_init_status = ICM42688P_ERR_IO;
        return ICM42688P_ERR_IO;
    }
    g_icm_who_am_i = whoami;
    g_icm_stage = 4;

    if (whoami != ICM42688P_WHO_AM_I_EXPECTED) {
        g_icm_init_status = ICM42688P_ERR_WHO_AM_I;
        return ICM42688P_ERR_WHO_AM_I;
    }

    /* PWR_MGMT0: Gyro Low-Noise (0b11), Accel Low-Noise (0b11), TEMP on */
    if (reg_write(REG_PWR_MGMT0, 0x0F) != HAL_OK) {
        g_icm_stage = -5;
        g_icm_init_status = ICM42688P_ERR_IO;
        return ICM42688P_ERR_IO;
    }
    /* Datasheet §12.9: Gyro startup time bis stabiles Rauschen = 45 ms.
     * Accel startup = 200 µs. Gemeinsamer Wait = 45 ms.  Innerhalb
     * dieser Zeit dürfen auch ODR/FS schon geändert werden, aber wir
     * warten sauber damit der erste Sample nicht in der Anlaufflanke
     * sitzt. */
    HAL_Delay(45);
    g_icm_stage = 5;

    /* FS/ODR + UI-Filter für Orientation-Fusion setzen. */
    if (reg_write(REG_GYRO_CONFIG0,       ICM_GYRO_CONFIG0_VAL)       != HAL_OK ||
        reg_write(REG_ACCEL_CONFIG0,      ICM_ACCEL_CONFIG0_VAL)      != HAL_OK ||
        reg_write(REG_GYRO_ACCEL_CONFIG0, ICM_GYRO_ACCEL_CONFIG0_VAL) != HAL_OK) {
        g_icm_stage = -6;
        g_icm_init_status = ICM42688P_ERR_IO;
        return ICM42688P_ERR_IO;
    }
    /* Filter-Settling nach ODR/FS-Wechsel: ~10 Samples = 10 ms. */
    HAL_Delay(10);
    g_icm_stage = 6;

    g_icm_init_status = ICM42688P_OK;
    g_icm_stage = 10;
    return ICM42688P_OK;
}

icm42688p_status_t icm42688p_read_sample(icm42688p_sample_t *out)
{
    /* TEMP_DATA1 .. GYRO_DATA_Z0 = 0x1D..0x2A = 14 Bytes, big-endian */
    uint8_t buf[14];
    if (reg_read_burst(REG_TEMP_DATA1, buf, sizeof(buf)) != HAL_OK) {
        return ICM42688P_ERR_IO;
    }

    int16_t temp = (int16_t)((buf[0]  << 8) | buf[1]);
    int16_t ax   = (int16_t)((buf[2]  << 8) | buf[3]);
    int16_t ay   = (int16_t)((buf[4]  << 8) | buf[5]);
    int16_t az   = (int16_t)((buf[6]  << 8) | buf[7]);
    int16_t gx   = (int16_t)((buf[8]  << 8) | buf[9]);
    int16_t gy   = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t gz   = (int16_t)((buf[12] << 8) | buf[13]);

    out->temp = temp;
    out->ax = ax; out->ay = ay; out->az = az;
    out->gx = gx; out->gy = gy; out->gz = gz;

    g_icm_temp = temp;
    g_icm_ax = ax; g_icm_ay = ay; g_icm_az = az;
    g_icm_gx = gx; g_icm_gy = gy; g_icm_gz = gz;
    g_icm_sample_count++;
    return ICM42688P_OK;
}

uint8_t icm42688p_get_who_am_i(void)
{
    return g_icm_who_am_i;
}

#include "paa5100.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "main.h"
#include "stm32f7xx_hal.h"

extern SPI_HandleTypeDef hspi2;

/* PAA5100JE registers (from Pimoroni driver, PixArt datasheet). */
#define REG_ID              0x00
#define REG_REV_ID          0x01
#define REG_DATA_READY      0x02
#define REG_MOTION_BURST    0x16
#define REG_POWER_UP_RESET  0x3A
#define REG_ORIENTATION     0x5B
#define REG_RESOLUTION      0x4E

/* Diagnose-Globals (kein UART am Board — Inspektion via GDB) */
volatile int      g_paa_stage          = 0;
volatile uint8_t  g_paa_product_id     = 0;
volatile uint8_t  g_paa_revision       = 0;
volatile uint8_t  g_paa_inv_id         = 0;       /* 0x5F: bitwise-NOT product id, should be 0xB6 */
volatile uint32_t g_paa_motion_reads   = 0;
volatile int16_t  g_paa_dx_accum       = 0;
volatile int16_t  g_paa_dy_accum       = 0;
volatile uint8_t  g_paa_last_squal     = 0;

static uint8_t s_product_id = 0;
static uint8_t s_revision   = 0;

/* ---------- Pin / SPI plumbing ------------------------------------------ */

static inline void cs_low(void)  { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET); }

/* PCB führt SPI2_MOSI auf PC1 (AF5), nicht PC3 wie von CubeMX defaultmäßig
 * konfiguriert.  Wir bauen die GPIO-Belegung händisch um:
 *   - PB10 = SCK  (AF5 SPI2)  – schon korrekt von CubeMX
 *   - PC2  = MISO (AF5 SPI2)  – schon korrekt
 *   - PC1  = MOSI (AF5 SPI2)  – nachziehen
 *   - PC3  = MOSI was CubeMX gesetzt hat → auf Analog/Float setzen damit
 *            zwei MOSI-Treiber nicht kollidieren
 *   - PA3  = CS (output, push-pull)
 *   - PA2  = INT (input mit Pullup) – CubeMX hatte das fälschlich als Output
 */
static void reconfigure_pins(void)
{
    GPIO_InitTypeDef g = {0};

    /* PC1 → SPI2 MOSI (AF5) */
    g.Pin       = GPIO_PIN_1;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOC, &g);

    /* PC3 → Analog (deaktivieren, war fälschlich SPI2_MOSI) */
    g.Pin       = GPIO_PIN_3;
    g.Mode      = GPIO_MODE_ANALOG;
    g.Pull      = GPIO_NOPULL;
    g.Alternate = 0;
    HAL_GPIO_Init(GPIOC, &g);

    /* PA2 → Input mit Pullup (FLOW_INT — war fälschlich Output) */
    g.Pin   = GPIO_PIN_2;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    /* PA3 (CS) bleibt Output PP — von CubeMX richtig konfiguriert; idle high. */
    cs_high();
}

/* CubeMX init SPI2 mit Mode 0 und Prescaler 4 (24 MHz).  PAA5100 verlangt
 * Mode 3 und max 2 MHz. */
static int reinit_spi2(void)
{
    HAL_SPI_DeInit(&hspi2);
    /* CubeMX hat DataSize default auf 4-bit gestellt — Korrigieren auf 8. */
    hspi2.Init.DataSize          = SPI_DATASIZE_8BIT;
    /* PAA5100 sollte Mode 3 sein, aber Pimoroni-Lib setzt explicit nichts
     * (default mode 0).  Probieren wir Mode 0 als Diagnose. */
    hspi2.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase          = SPI_PHASE_1EDGE;
    /* APB1 = HCLK/4 = 24 MHz @ 96 MHz SYSCLK.  /16 = 1.5 MHz — knapp unter
     * dem 2-MHz-Max des PAA5100.  Motion-Burst (13 Byte) ~70 µs.  (Bei
     * Init-Problemen → /32 = 750 kHz, lief verifiziert stabil.) */
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi2.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
    return (HAL_SPI_Init(&hspi2) == HAL_OK) ? 0 : -1;
}

/* Exakte µs-Verzögerung über den DWT-Cycle-Counter — taktunabhängig, weil
 * sie SystemCoreClock zur Laufzeit nutzt (passt sich also automatisch an
 * 96 MHz, 192 MHz, … an).  Ersetzt die fragile NOP-Schleifen-Kalibrierung,
 * die bei 96 MHz zu kurze PAA-Timings lieferte → init schlug fehl.  DWT
 * wird beim ersten Aufruf einmalig scharfgeschaltet (Cortex-M7: CYCCNT
 * läuft auch ohne angeschlossenen Debugger, sobald TRCENA gesetzt ist). */
static inline void delay_us(uint32_t us)
{
    static uint8_t s_dwt_ready = 0u;
    if (!s_dwt_ready) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0u;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
        s_dwt_ready = 1u;
    }
    const uint32_t start = DWT->CYCCNT;
    const uint32_t ticks = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < ticks) { __NOP(); }
}

/* CS-Aware single-byte write/read.  PixArt-Pattern: bit 7 = R/W.
 * Timing nach Bitcraze-Treiber für PMW3901/PAA5100:
 *   - CS-low → address: 50 µs setup
 *   - address → data:   50 µs (write) bzw 500 µs (read)
 *   - data   → CS-high: 50 µs hold
 *   - CS high inter-frame:           200 µs */
static void paa_write(uint8_t reg, uint8_t val)
{
    uint8_t a = (uint8_t)(reg | 0x80u);
    cs_low();
    delay_us(50);
    HAL_SPI_Transmit(&hspi2, &a, 1, 50);
    delay_us(50);
    HAL_SPI_Transmit(&hspi2, &val, 1, 50);
    delay_us(50);
    cs_high();
    delay_us(200);
}

static uint8_t paa_read(uint8_t reg)
{
    uint8_t a = reg & 0x7Fu;
    uint8_t dummy = 0xFF;
    uint8_t rx = 0;
    cs_low();
    delay_us(50);
    HAL_SPI_Transmit(&hspi2, &a, 1, 50);
    delay_us(500);  /* read-specific gap per PixArt datasheet */
    HAL_SPI_TransmitReceive(&hspi2, &dummy, &rx, 1, 50);
    delay_us(50);
    cs_high();
    delay_us(200);
    return rx;
}

/* Motion-Burst — Hot Path, daher auf Datenblatt-Timing getrimmt:
 * tSRAD (address→data) = 35 µs typ. → 50 µs mit Marge (war 500 µs),
 * Inter-Frame tSRR ≈ 20 µs → 50 µs (war 200 µs).  Mit DWT-genauem delay_us
 * ist das sicher.  Read fällt damit von ~1.36 ms auf ~0.34 ms. */
static void paa_burst_read(uint8_t reg, uint8_t *buf, unsigned len)
{
    uint8_t addr = reg & 0x7Fu;
    cs_low();
    delay_us(50);
    HAL_SPI_Transmit(&hspi2, &addr, 1, 50);
    delay_us(50);   /* tSRAD (Datenblatt 35 µs) */
    uint8_t tx_zero[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (len > sizeof(tx_zero)) len = sizeof(tx_zero);
    HAL_SPI_TransmitReceive(&hspi2, tx_zero, buf, (uint16_t)len, 100);
    delay_us(50);
    cs_high();
    delay_us(50);   /* Inter-Frame tSRR (Datenblatt ~20 µs) */
}

/* ---------- "Secret sauce" — PAA5100 calibration sequence --------------- */

/* Encoded as flat reg/val pairs.  0xFF as register means "delay (val) ms". */
#define DELAY_REG 0xFE

static void bulk_write(const uint8_t *seq, unsigned n)
{
    for (unsigned i = 0; i + 1 < n; i += 2) {
        if (seq[i] == DELAY_REG) {
            HAL_Delay(seq[i + 1]);
        } else {
            paa_write(seq[i], seq[i + 1]);
        }
    }
}

static const uint8_t SS_PART1[] = {
    0x7F, 0x00,  0x55, 0x01,  0x50, 0x07,
    0x7F, 0x0E,  0x43, 0x10,
};
static const uint8_t SS_PART2[] = {
    0x7F, 0x00,  0x51, 0x7B,  0x50, 0x00,  0x55, 0x00,
    0x7F, 0x0E,
};
static const uint8_t SS_PART3_PREP[] = {
    0x7F, 0x00,  0x61, 0xAD,  0x51, 0x70,  0x7F, 0x0E,
};
static const uint8_t SS_FINAL[] = {
    0x7F, 0x00,  0x61, 0xAD,

    0x7F, 0x03,  0x40, 0x00,

    0x7F, 0x05,  0x41, 0xB3,  0x43, 0xF1,  0x45, 0x14,
    0x5F, 0x34,  0x7B, 0x08,  0x5E, 0x34,  0x5B, 0x11,
    0x6D, 0x11,  0x45, 0x17,  0x70, 0xE5,  0x71, 0xE5,

    0x7F, 0x06,  0x44, 0x1B,  0x40, 0xBF,  0x4E, 0x3F,

    0x7F, 0x08,  0x66, 0x44,  0x65, 0x20,  0x6A, 0x3A,
    0x61, 0x05,  0x62, 0x05,

    0x7F, 0x09,  0x4F, 0xAF,  0x5F, 0x40,  0x48, 0x80,
    0x49, 0x80,  0x57, 0x77,  0x60, 0x78,  0x61, 0x78,
    0x62, 0x08,  0x63, 0x50,

    0x7F, 0x0A,  0x45, 0x60,

    0x7F, 0x00,  0x4D, 0x11,  0x55, 0x80,  0x74, 0x21,
    0x75, 0x1F,  0x4A, 0x78,  0x4B, 0x78,  0x44, 0x08,
    0x45, 0x50,  0x64, 0xFF,  0x65, 0x1F,

    0x7F, 0x14,  0x65, 0x67,  0x66, 0x08,  0x63, 0x70,
    0x6F, 0x1C,

    0x7F, 0x15,  0x48, 0x48,

    0x7F, 0x07,  0x41, 0x0D,  0x43, 0x14,  0x4B, 0x0E,
    0x45, 0x0F,  0x44, 0x42,  0x4C, 0x80,

    0x7F, 0x10,  0x5B, 0x02,

    0x7F, 0x07,  0x40, 0x41,

    DELAY_REG, 0x0A,

    0x7F, 0x00,  0x32, 0x00,

    0x7F, 0x07,  0x40, 0x40,

    0x7F, 0x06,  0x68, 0xF0,  0x69, 0x00,

    0x7F, 0x0D,  0x48, 0xC0,  0x6F, 0xD5,

    0x7F, 0x00,  0x5B, 0xA0,  0x4E, 0xA8,  0x5A, 0x90,
    0x40, 0x80,  0x73, 0x1F,

    DELAY_REG, 0x0A,

    0x73, 0x00,
};

static void secret_sauce(void)
{
    bulk_write(SS_PART1, sizeof(SS_PART1));

    if (paa_read(0x67) & 0x80) {
        paa_write(0x48, 0x04);
    } else {
        paa_write(0x48, 0x02);
    }

    bulk_write(SS_PART2, sizeof(SS_PART2));

    if (paa_read(0x73) == 0x00) {
        uint8_t c1 = paa_read(0x70);
        uint8_t c2 = paa_read(0x71);
        if (c1 <= 28) c1 += 14;
        else          c1 += 11;
        if (c1 > 0x3F) c1 = 0x3F;
        c2 = (uint8_t)(((uint16_t)c2 * 45u) / 100u);

        bulk_write(SS_PART3_PREP, sizeof(SS_PART3_PREP));
        paa_write(0x70, c1);
        paa_write(0x71, c2);
    }

    bulk_write(SS_FINAL, sizeof(SS_FINAL));
}

/* ---------- Public API ---------------------------------------------------- */

paa5100_status_t paa5100_init(void)
{
    g_paa_stage = 1;
    if (reinit_spi2() != 0) {
        g_paa_stage = -2;
        return PAA5100_ERR_IO;
    }
    g_paa_stage = 2;
    /* GPIO-Fixup MUSS nach reinit_spi2 — HAL_SPI_Init ruft MspInit, das
     * PC3 als AF zurücksetzt. */
    reconfigure_pins();
    g_paa_stage = 3;

    /* Drive CS low → high pulse to sync sensor SPI state */
    cs_low();
    HAL_Delay(50);
    cs_high();
    HAL_Delay(5);

    /* Power-up reset */
    paa_write(REG_POWER_UP_RESET, 0x5A);
    HAL_Delay(20);

    /* Read DATA_READY + 4 registers (per Pimoroni — wakes/syncs sensor) */
    for (uint8_t off = 0; off < 5; off++) {
        (void)paa_read(REG_DATA_READY + off);
    }
    g_paa_stage = 4;

    secret_sauce();
    g_paa_stage = 5;

    s_product_id = paa_read(REG_ID);
    s_revision   = paa_read(REG_REV_ID);
    g_paa_product_id = s_product_id;
    g_paa_revision   = s_revision;
    g_paa_inv_id     = paa_read(0x5F);

    if (s_product_id != 0x49) {
        g_paa_stage = -6;
        return PAA5100_ERR_ID;
    }
    g_paa_stage = 10;
    return PAA5100_OK;
}

paa5100_status_t paa5100_read_motion(paa5100_motion_t *out)
{
    if (!out) return PAA5100_ERR_IO;

    uint8_t buf[12] = {0};
    paa_burst_read(REG_MOTION_BURST, buf, 12);

    out->motion     = buf[0];
    /* buf[1] = observation, buf[2..3] = dx (little-endian, signed) ... */
    out->dx         = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    out->dy         = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
    out->squal      = buf[6];
    /* buf[7..9] raw_sum/max/min */
    out->shutter_hi = buf[10];
    out->shutter_lo = buf[11];

    g_paa_motion_reads++;
    g_paa_dx_accum   = (int16_t)(g_paa_dx_accum + out->dx);
    g_paa_dy_accum   = (int16_t)(g_paa_dy_accum + out->dy);
    g_paa_last_squal = out->squal;

    return PAA5100_OK;
}

uint8_t paa5100_get_product_id(void)  { return s_product_id; }
uint8_t paa5100_get_revision_id(void) { return s_revision;   }

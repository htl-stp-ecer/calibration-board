#pragma once

#include <stdint.h>

/* Initialise SWO/ITM trace on PB3 for printf() output.
 *
 *   hclk_hz   — CPU/HCLK frequency in Hz (TPIU is clocked from HCLK on M7)
 *   swo_baud  — desired SWO bitrate in Hz; ST-LINK V2 tops out around 2 MHz
 *
 * Safe to call even without a debugger attached: ITM_SendChar() short-circuits
 * if ITM is disabled, so printf() simply becomes a no-op.
 */
void swo_init(uint32_t hclk_hz, uint32_t swo_baud);

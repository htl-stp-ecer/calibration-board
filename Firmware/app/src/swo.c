#include "swo.h"

#include "stm32f7xx_hal.h"

void swo_init(uint32_t hclk_hz, uint32_t swo_baud)
{
    /* Route the TPIU trace output to PB3. STM32CubeMX configures the IOC with
     * Trace_Asynchronous_SW but does NOT emit this code in HAL_MspInit, so the
     * pin stays disconnected from the trace block without this step. */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {
        .Pin       = GPIO_PIN_3,
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = GPIO_AF0_TRACE,
    };
    HAL_GPIO_Init(GPIOB, &gpio);
    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN;

    /* Unlock trace, enable DWT/ITM access. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* TPIU: NRZ (UART-like) async output, no formatter. */
    TPI->SPPR = 2U;
    TPI->ACPR = (hclk_hz / swo_baud) - 1U;
    TPI->FFCR = 0x00000100U;

    /* ITM: unlock, enable, ATB ID = 1, stimulus port 0 unprivileged + enabled. */
    ITM->LAR = 0xC5ACCE55U;
    ITM->TCR = (1U << ITM_TCR_TraceBusID_Pos) | ITM_TCR_SWOENA_Msk |
               ITM_TCR_DWTENA_Msk | ITM_TCR_SYNCENA_Msk | ITM_TCR_ITMENA_Msk;
    ITM->TPR = 0x1U;
    ITM->TER = 0x1U;
}

extern UART_HandleTypeDef huart4;

int __io_putchar(int ch)
{
    /* Doppel-Sink: SWO/ITM (no-op falls Debugger nicht trace'd) + UART4 auf
     * J703 für angeschlossenen USB-UART-Adapter. */
    ITM_SendChar((uint32_t)ch);
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart4, &c, 1, 10);
    return ch;
}

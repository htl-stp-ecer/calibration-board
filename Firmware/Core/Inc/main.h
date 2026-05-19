/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define FLOW_SENS_INT_Pin GPIO_PIN_2
#define FLOW_SENS_INT_GPIO_Port GPIOA
#define FLOW_SENS_CS_Pin GPIO_PIN_3
#define FLOW_SENS_CS_GPIO_Port GPIOA
#define BNO_CS1_Pin GPIO_PIN_4
#define BNO_CS1_GPIO_Port GPIOA
#define BNO_SCLK_Pin GPIO_PIN_5
#define BNO_SCLK_GPIO_Port GPIOA
#define BNO_MISO_Pin GPIO_PIN_6
#define BNO_MISO_GPIO_Port GPIOA
#define BNO_MOSI_Pin GPIO_PIN_7
#define BNO_MOSI_GPIO_Port GPIOA
#define BNO_INT_Pin GPIO_PIN_4
#define BNO_INT_GPIO_Port GPIOC
#define BNO_NRST_Pin GPIO_PIN_0
#define BNO_NRST_GPIO_Port GPIOB
#define BNO_WAKE_Pin GPIO_PIN_1
#define BNO_WAKE_GPIO_Port GPIOB
#define add_PB11_Pin GPIO_PIN_11
#define add_PB11_GPIO_Port GPIOB
#define add_PB12_Pin GPIO_PIN_12
#define add_PB12_GPIO_Port GPIOB
#define add_PB13_Pin GPIO_PIN_13
#define add_PB13_GPIO_Port GPIOB
#define add_PB14_Pin GPIO_PIN_14
#define add_PB14_GPIO_Port GPIOB
#define add_PB15_Pin GPIO_PIN_15
#define add_PB15_GPIO_Port GPIOB
#define add_PC6_Pin GPIO_PIN_6
#define add_PC6_GPIO_Port GPIOC
#define add_PC7_Pin GPIO_PIN_7
#define add_PC7_GPIO_Port GPIOC
#define add_PC8_Pin GPIO_PIN_8
#define add_PC8_GPIO_Port GPIOC
#define USER_LED_Pin GPIO_PIN_9
#define USER_LED_GPIO_Port GPIOC
#define ICM_INT_Pin GPIO_PIN_15
#define ICM_INT_GPIO_Port GPIOA
#define SPI3_CS1_Pin GPIO_PIN_2
#define SPI3_CS1_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

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
#include "stm32f4xx_hal.h"

#include "stm32f4xx_nucleo.h"

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define SM_DCLK_Pin GPIO_PIN_4
#define SM_DCLK_GPIO_Port GPIOC
#define SM_LE_Pin GPIO_PIN_5
#define SM_LE_GPIO_Port GPIOC
#define SM_R1_Pin GPIO_PIN_6
#define SM_R1_GPIO_Port GPIOC
#define SM_G1_Pin GPIO_PIN_7
#define SM_G1_GPIO_Port GPIOC
#define SM_B1_Pin GPIO_PIN_8
#define SM_B1_GPIO_Port GPIOC
#define SM_R2_Pin GPIO_PIN_9
#define SM_R2_GPIO_Port GPIOC
#define SM_GCLK_Pin GPIO_PIN_8
#define SM_GCLK_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define SM_G2_Pin GPIO_PIN_10
#define SM_G2_GPIO_Port GPIOC
#define SM_B2_Pin GPIO_PIN_11
#define SM_B2_GPIO_Port GPIOC
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define SM_ADDR_A_Pin GPIO_PIN_4
#define SM_ADDR_A_GPIO_Port GPIOB
#define SM_ADDR_B_Pin GPIO_PIN_5
#define SM_ADDR_B_GPIO_Port GPIOB
#define SM_ADDR_C_Pin GPIO_PIN_6
#define SM_ADDR_C_GPIO_Port GPIOB
#define SM_ADDR_D_Pin GPIO_PIN_7
#define SM_ADDR_D_GPIO_Port GPIOB
#define SM_ADDR_E_Pin GPIO_PIN_8
#define SM_ADDR_E_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

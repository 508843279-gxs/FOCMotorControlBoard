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

#define LED_OT_Pin GPIO_PIN_13
#define LED_OT_GPIO_Port GPIOC
#define LED_OC_Pin GPIO_PIN_14
#define LED_OC_GPIO_Port GPIOC
#define LED_OV_Pin GPIO_PIN_15
#define LED_OV_GPIO_Port GPIOC
#define LED_UV_Pin GPIO_PIN_0
#define LED_UV_GPIO_Port GPIOH
#define KEY_RUN_Pin GPIO_PIN_1
#define KEY_RUN_GPIO_Port GPIOA
#define KEY_DIR_Pin GPIO_PIN_2
#define KEY_DIR_GPIO_Port GPIOA
#define KEY_SPEED_UP_Pin GPIO_PIN_13
#define KEY_SPEED_UP_GPIO_Port GPIOB
#define KEY_SPEED_DOWN_Pin GPIO_PIN_14
#define KEY_SPEED_DOWN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define control_mod   1     //控制模式（0：串口控制，1：按键控制）


extern uint16_t RPM1;
extern uint16_t RPM2;
extern uint16_t RPM3;
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

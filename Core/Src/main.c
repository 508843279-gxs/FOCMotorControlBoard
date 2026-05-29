/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "crc.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* 第一阶段硬件自检函数：只检查基础硬件，不进入电机控制流程。 */
static void SelfTest_Start(void);
static void SelfTest_Task(void);
static void SelfTest_StartPwm(void);
static void SelfTest_StartAdc(void);
static void SelfTest_AllLedsOff(void);
static void SelfTest_SetOnlyLed(uint8_t led_index);
static void SelfTest_ScanKeys(void);
static void SelfTest_CheckKey(const char *name, GPIO_TypeDef *port, uint16_t pin, GPIO_PinState *last_state);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 自检任务周期。所有任务都在主循环中轮询，不使用阻塞延时。 */
#define SELFTEST_LED_INTERVAL_MS     250U
#define SELFTEST_KEY_SCAN_MS         20U
#define SELFTEST_REPORT_INTERVAL_MS  1000U

/* 当前按 LED 低电平点亮处理；如果实际亮灭相反，交换这两个宏即可。 */
#define SELFTEST_LED_ON              GPIO_PIN_RESET
#define SELFTEST_LED_OFF             GPIO_PIN_SET

/* 这些变量由 Int/Src/Int_bsp.c 中的中断回调更新。 */
extern volatile uint32_t adc_injected_count;
extern uint8_t rxBuff[1000];

/* 基于 HAL_GetTick() 的简单软件调度状态。 */
static uint32_t selftest_next_led_ms;
static uint32_t selftest_next_key_ms;
static uint32_t selftest_next_report_ms;
static uint32_t selftest_last_adc_count;
static uint8_t selftest_led_index;

/* 保存按键上一次状态，只在按下/松开边沿变化时打印。 */
static GPIO_PinState key_run_last = GPIO_PIN_SET;
static GPIO_PinState key_dir_last = GPIO_PIN_SET;
static GPIO_PinState key_speed_up_last = GPIO_PIN_SET;
static GPIO_PinState key_speed_down_last = GPIO_PIN_SET;
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_TIM1_Init();
  MX_CRC_Init();
  MX_TIM5_Init();
  /* USER CODE BEGIN 2 */
  SelfTest_Start();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    SelfTest_Task();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 84;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* CubeMX 外设初始化完成后，启动第一阶段所有硬件自检。 */
static void SelfTest_Start(void)
{
  printf("\r\n[SELFTEST] FOCMotorControlBoard hardware self-test\r\n");
  printf("[SELFTEST] UART OK, starting LED/PWM/ADC checks...\r\n");
  printf("[SELFTEST] PWM: PA8/PA9/PA10 and PA7/PB0/PB1, ADC trigger: PA11/TIM1_CH4\r\n");
  printf("[SELFTEST] Keys: PA1 RUN, PA2 DIR, PB13 UP, PB14 DOWN\r\n");

  /* 串口接收空闲中断：电脑串口助手发送任意字节即可验证 RX 中断链路。 */
  if (HAL_UARTEx_ReceiveToIdle_IT(&huart1, rxBuff, 1000U) != HAL_OK)
  {
    printf("[SELFTEST] UART RX start failed\r\n");
  }

  /* 先关闭所有 LED，再开始轮流单独点亮，方便确认每一路 LED 是否正常。 */
  SelfTest_AllLedsOff();
  SelfTest_StartPwm();
  SelfTest_StartAdc();

  selftest_next_led_ms = HAL_GetTick();
  selftest_next_key_ms = HAL_GetTick();
  selftest_next_report_ms = HAL_GetTick() + SELFTEST_REPORT_INTERVAL_MS;
}

/* 输出固定占空比 PWM，方便用示波器逐个检查 PWM 引脚。 */
static void SelfTest_StartPwm(void)
{
  uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;

  /* CH1/2/3 用于三相 PWM 波形检查；CH4 靠近周期末端，用于观察 ADC 触发相关波形。 */
  __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, period / 4U);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, period / 2U);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, (period * 3U) / 4U);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, period - 2U);

  /* 启动主 PWM 和互补 PWM 输出；如果启动失败，通过串口打印错误。 */
  if (HAL_TIM_Base_Start(&htim1) != HAL_OK) { printf("[SELFTEST] TIM1 base start failed\r\n"); }
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) { printf("[SELFTEST] TIM1 CH1 start failed\r\n"); }
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK) { printf("[SELFTEST] TIM1 CH2 start failed\r\n"); }
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3) != HAL_OK) { printf("[SELFTEST] TIM1 CH3 start failed\r\n"); }
  if (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) { printf("[SELFTEST] TIM1 CH1N start failed\r\n"); }
  if (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2) != HAL_OK) { printf("[SELFTEST] TIM1 CH2N start failed\r\n"); }
  if (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3) != HAL_OK) { printf("[SELFTEST] TIM1 CH3N start failed\r\n"); }
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4) != HAL_OK) { printf("[SELFTEST] TIM1 CH4 start failed\r\n"); }

  /* TIM5 先保持运行，后续可用于耗时测量；当前阶段不参与电机控制。 */
  if (HAL_TIM_Base_Start(&htim5) != HAL_OK) { printf("[SELFTEST] TIM5 base start failed\r\n"); }
  printf("[SELFTEST] TIM1 PWM started, ARR=%lu\r\n", (unsigned long)(period - 1U));
}

/* 启动 ADC 注入转换中断；第一阶段回调里只做计数。 */
static void SelfTest_StartAdc(void)
{
  /* 启动前先清掉可能残留的注入转换完成标志，避免误判。 */
  __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOC);

  if (HAL_ADCEx_InjectedStart_IT(&hadc1) != HAL_OK)
  {
    printf("[SELFTEST] ADC injected start failed\r\n");
  }
  else
  {
    printf("[SELFTEST] ADC injected interrupt started\r\n");
  }
}

/* 主循环轮询低频自检任务，避免阻塞 ADC/PWM 的正常运行。 */
static void SelfTest_Task(void)
{
  uint32_t now = HAL_GetTick();

  /* LED 轮流点亮，便于单独确认每个输出脚和指示灯。 */
  if ((int32_t)(now - selftest_next_led_ms) >= 0)
  {
    SelfTest_SetOnlyLed(selftest_led_index);
    selftest_led_index = (uint8_t)((selftest_led_index + 1U) & 0x03U);
    selftest_next_led_ms = now + SELFTEST_LED_INTERVAL_MS;
  }

  /* 按键低频扫描，每个按键独立做边沿检测。 */
  if ((int32_t)(now - selftest_next_key_ms) >= 0)
  {
    SelfTest_ScanKeys();
    selftest_next_key_ms = now + SELFTEST_KEY_SCAN_MS;
  }

  /* 每秒打印一次 ADC 回调增量；delta 应稳定且不为 0。 */
  if ((int32_t)(now - selftest_next_report_ms) >= 0)
  {
    uint32_t count = adc_injected_count;
    printf("[SELFTEST] adc_count=%lu delta=%lu\r\n",
           (unsigned long)count,
           (unsigned long)(count - selftest_last_adc_count));
    selftest_last_adc_count = count;
    selftest_next_report_ms = now + SELFTEST_REPORT_INTERVAL_MS;
  }
}

/* 强制关闭所有指示灯。 */
static void SelfTest_AllLedsOff(void)
{
  HAL_GPIO_WritePin(LED_UV_GPIO_Port, LED_UV_Pin, SELFTEST_LED_OFF);
  HAL_GPIO_WritePin(LED_OV_GPIO_Port, LED_OV_Pin, SELFTEST_LED_OFF);
  HAL_GPIO_WritePin(LED_OC_GPIO_Port, LED_OC_Pin, SELFTEST_LED_OFF);
  HAL_GPIO_WritePin(LED_OT_GPIO_Port, LED_OT_Pin, SELFTEST_LED_OFF);
}

/* 只点亮指定的一个 LED，用于检查引脚顺序和亮灭极性。 */
static void SelfTest_SetOnlyLed(uint8_t led_index)
{
  SelfTest_AllLedsOff();

  switch (led_index)
  {
  case 0:
    HAL_GPIO_WritePin(LED_UV_GPIO_Port, LED_UV_Pin, SELFTEST_LED_ON);
    break;
  case 1:
    HAL_GPIO_WritePin(LED_OV_GPIO_Port, LED_OV_Pin, SELFTEST_LED_ON);
    break;
  case 2:
    HAL_GPIO_WritePin(LED_OC_GPIO_Port, LED_OC_Pin, SELFTEST_LED_ON);
    break;
  case 3:
    HAL_GPIO_WritePin(LED_OT_GPIO_Port, LED_OT_Pin, SELFTEST_LED_ON);
    break;
  default:
    break;
  }
}

/* 检查所有按键；当前配置为上拉输入、低电平有效。 */
static void SelfTest_ScanKeys(void)
{
  SelfTest_CheckKey("KEY_RUN", KEY_RUN_GPIO_Port, KEY_RUN_Pin, &key_run_last);
  SelfTest_CheckKey("KEY_DIR", KEY_DIR_GPIO_Port, KEY_DIR_Pin, &key_dir_last);
  SelfTest_CheckKey("KEY_SPEED_UP", KEY_SPEED_UP_GPIO_Port, KEY_SPEED_UP_Pin, &key_speed_up_last);
  SelfTest_CheckKey("KEY_SPEED_DOWN", KEY_SPEED_DOWN_GPIO_Port, KEY_SPEED_DOWN_Pin, &key_speed_down_last);
}

/* 只在按键状态变化时打印，避免串口被重复信息刷屏。 */
static void SelfTest_CheckKey(const char *name, GPIO_TypeDef *port, uint16_t pin, GPIO_PinState *last_state)
{
  GPIO_PinState state = HAL_GPIO_ReadPin(port, pin);

  if (state != *last_state)
  {
    *last_state = state;
    printf("[SELFTEST] %s %s\r\n", name, state == GPIO_PIN_RESET ? "PRESSED" : "RELEASED");
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

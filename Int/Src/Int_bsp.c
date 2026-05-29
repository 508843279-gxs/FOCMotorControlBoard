#include "adc.h"
#include "bsp.h"
#include "usart.h"
#include <stdio.h>

/* 第二阶段 BSP 总入口和中断回调。
 * 中断里只做采样、计数、轻量保护；主循环里跑按键、命令和状态机。
 */

#define BSP_REPORT_INTERVAL_MS 1000U

volatile uint32_t adc_injected_count;
volatile uint32_t uart_rx_event_count;

uint8_t rxBuff[1000];

static uint32_t bsp_next_report_ms;
static uint32_t bsp_last_adc_count;

void BspInit(void)
{
    printf("\r\n[BSP2] BSP layer bring-up\r\n");
    printf("[BSP2] Modules: adc command pwm protection motor\r\n");
    printf("[BSP2] UART CMD: RUN STOP DIR UP DOWN STATUS HELP\r\n");

    ParaInit();
    StatusLed_AllOff();
    BspPwm_StartAdcTrigger();

    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOC);
    if (HAL_ADCEx_InjectedStart_IT(&hadc1) != HAL_OK)
    {
        printf("[BSP2] ADC injected start failed\r\n");
    }

    if (HAL_UARTEx_ReceiveToIdle_IT(&huart1, rxBuff, sizeof(rxBuff)) != HAL_OK)
    {
        printf("[BSP2] UART RX start failed\r\n");
    }

    bsp_next_report_ms = HAL_GetTick() + BSP_REPORT_INTERVAL_MS;
}

void BspTask(void)
{
    uint32_t now = HAL_GetTick();

    KeyControl_Update();
    MotorControl_UpdateCommand();
    MotorControl_StateMachineStep();

    if ((int32_t)(now - bsp_next_report_ms) >= 0)
    {
        uint32_t count = adc_injected_count;
        printf("[BSP2] adc=%lu delta=%lu vbus=%d ia=%d ib=%d temp=%d state=%d err=%d\r\n",
               (unsigned long)count,
               (unsigned long)(count - bsp_last_adc_count),
               (int)mc_info.vbus,
               (int)mc_info.isens_a,
               (int)mc_info.isens_b,
               (int)mc_info.temperature,
               mc_info.mc_state,
               mc_info.mc_err);
        bsp_last_adc_count = count;
        bsp_next_report_ms = now + BSP_REPORT_INTERVAL_MS;
    }
}

void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        adc_injected_count++;
        BspAdcSample_Update();
        MotorProtection_CheckVbus();
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        uart_rx_event_count++;
        MotorControl_HandleUartCommand(rxBuff, Size);
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, rxBuff, sizeof(rxBuff));
    }
}

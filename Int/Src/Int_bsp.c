#include "adc.h"
#include "bsp.h"
#include "FOC_CURRENT.h"
#include "usart.h"
#include <stdio.h>

/* 第四阶段 BSP/FOC 总入口和中断回调。
 * ADC 回调里完成采样、温度滤波、保护和 FOC 快速控制；
 * 主循环里跑按键、串口命令和状态机。
 */

#define BSP_REPORT_INTERVAL_MS 1000U

/* ADC 注入回调计数。每秒打印 delta，用来判断 ADC 触发是否稳定。 */
volatile uint32_t adc_injected_count;
/* UART 空闲接收事件计数。后续如果串口不响应，可以先看这个是否增加。 */
volatile uint32_t uart_rx_event_count;

/* 串口接收缓冲区。HAL_UARTEx_ReceiveToIdle_IT() 会把收到的数据放到这里。 */
uint8_t rxBuff[1000];

/* 每秒状态打印的软件定时变量。 */
static uint32_t bsp_next_report_ms;
static uint32_t bsp_last_adc_count;

void BspInit(void)
{
    /* 第四阶段启动提示：能看到这几行，说明串口 printf 已经通。 */
    printf("\r\n[SAFE4] safe trial bring-up\r\n");
    printf("[SAFE4] Modules: adc command pwm protection motor temperature foc_current\r\n");
    printf("[SAFE4] UART CMD: RUN STOP DIR UP DOWN STATUS HELP\r\n");

    /* 初始化状态机、命令缓冲和故障灯，确保上电处于安全停止状态。 */
    ParaInit();
    StatusLed_AllOff();
    /* TIM1 CH4 先启动，用来持续触发 ADC 注入转换。 */
    BspPwm_StartAdcTrigger();

    /* 启动 ADC 注入转换中断。真正的采样读取在 ADC 回调中完成。 */
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_JEOC);
    if (HAL_ADCEx_InjectedStart_IT(&hadc1) != HAL_OK)
    {
        printf("[SAFE4] ADC injected start failed\r\n");
    }

    /* 启动 UART 空闲接收。串口命令在 HAL_UARTEx_RxEventCallback() 中解析。 */
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart1, rxBuff, sizeof(rxBuff)) != HAL_OK)
    {
        printf("[SAFE4] UART RX start failed\r\n");
    }

    bsp_next_report_ms = HAL_GetTick() + BSP_REPORT_INTERVAL_MS;
}

void BspTask(void)
{
    uint32_t now = HAL_GetTick();

    /* 主循环只跑低频/非实时任务，避免在中断里做复杂逻辑。 */
    KeyControl_Update();
    MotorControl_UpdateCommand();
    MotorControl_StateMachineStep();

    /* 每秒打印一次关键状态，调试时优先看 delta/state/err。 */
    if ((int32_t)(now - bsp_next_report_ms) >= 0)
    {
        uint32_t count = adc_injected_count;
        printf("[SAFE4] adc=%lu delta=%lu ref=%d obs=%d vbus=%d ia=%d ib=%d temp=%d state=%d err=%d\r\n",
               (unsigned long)count,
               (unsigned long)(count - bsp_last_adc_count),
               (int)ObserverParam.RefRPM,
               (int)ObserverParam.ObserverRPM,
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
        /* ADC 回调里只做轻量工作：计数、采样换算、温度滤波和快速保护。 */
        adc_injected_count++;
        BspAdcSample_Update();
        BspTemperature_FilterTick();
        MotorProtection_CheckVbus();
        MotorControl_FocControlStep();
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        /* 串口收到一帧数据后，交给命令模块解析。 */
        uart_rx_event_count++;
        MotorControl_HandleUartCommand(rxBuff, Size);
        /* ReceiveToIdle 是一次性接收，处理完必须重新开启下一次接收。 */
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, rxBuff, sizeof(rxBuff));
    }
}

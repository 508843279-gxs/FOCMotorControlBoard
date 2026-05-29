/**
 * @file Int_bsp.c
 * @brief 第一阶段硬件自检回调。
 *
 * 本阶段中断里只做最小动作：ADC 回调只计数，UART RX 回调只打印收到的长度。
 */
#include "main.h"
#include "adc.h"
#include "usart.h"
#include <stdio.h>

/* main.c 会读取这些计数值；中断回调只更新轻量状态。 */
volatile uint32_t adc_injected_count;
volatile uint32_t uart_rx_event_count;

/* 串口接收空闲中断共用缓冲区。 */
uint8_t rxBuff[1000];

/* ADC 注入转换完成回调：只计数，用于证明 TIM1 触发 ADC 的链路正常。 */
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        adc_injected_count++;
    }
}

/* UART 接收空闲事件：打印收到的数据长度，并重新开启下一次接收。 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        uart_rx_event_count++;
        printf("[SELFTEST] UART RX size=%u\r\n", Size);
        /* 重新挂起接收，这样下一次串口输入也能被观察到。 */
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, rxBuff, sizeof(rxBuff));
    }
}

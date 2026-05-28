/**
 * @file Int_bsp.c
 * @brief BSP 中断入口层。
 *
 * 这个文件只保留 HAL 回调和模块调度顺序，不再直接堆放采样、命令、
 * 状态机和保护细节。真正业务逻辑拆到了 bsp_*.c 中，调试时可以按
 * 回调顺序逐个跳转：
 *   1. BspAdcSample_Update()       更新电流、电压、温度累计值
 *   2. KeyControl_Update()         按键控制模式下生成 comm[] 命令
 *   3. MotorControl_UpdateCommand()把 comm[] 翻译成 mc_info
 *   4. MotorControl_StateMachineStep()执行电机状态机和 FOC
 *   5. MotorProtection_CheckVbus() 做母线电压保护
 *   6. BspTemperature_FilterTick() 更新温度平均值
 */
#include "bsp.h"
#include "adc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "stdio.h"

/**
 * @brief ADC 注入转换完成回调。
 * @param hadc 触发回调的 ADC 句柄。
 *
 * TIM1 触发 ADC1 注入通道采样后，HAL 会进入这里。本回调是电机控制
 * 的主控制周期，所以这里的执行时间会直接影响 FOC 周期稳定性。
 * PA15 在入口拉高、出口拉低，可用示波器测量本次控制周期耗时。
 */
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    /* 工程当前只处理 ADC1，其他 ADC 回调直接忽略，避免误调度。 */
    if (hadc->Instance != hadc1.Instance)
    {
        return;
    }

    /* 调试脉冲起点：用于观察一次 ADC 回调的执行宽度。 */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
    /* TIM5 作为回调内部耗时/辅助计时器，每个控制周期重新清零。 */
    __HAL_TIM_SET_COUNTER(&htim5, 0);

    /* 读取 ADC 注入通道，并更新 mc_info 中的电流、电压、温度累计值。 */
    BspAdcSample_Update();

#if control_mod == 1
    /* control_mod=1 时由按键/串口统一控制速度档位和启停命令。 */
    KeyControl_Update();
#endif

    /* 将 comm[] 命令转换成状态机使用的 mc_info.cmd/refRPM/refIq。 */
    MotorControl_UpdateCommand();
    /* 执行 MC_RDY/MC_START/MC_RUN/MC_STOP/MC_ERR 状态机。 */
    MotorControl_StateMachineStep();
    /* 母线电压保护放在状态机后，确保采样值每个周期都被检查。 */
    MotorProtection_CheckVbus();
    /* 每 10000 个控制周期更新一次 SafeTemp。 */
    BspTemperature_FilterTick();

    /* 调试脉冲终点。 */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
}

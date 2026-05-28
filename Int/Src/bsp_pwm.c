#include "bsp.h"
#include "tim.h"

/**
 * @file bsp_pwm.c
 * @brief TIM1 三相 PWM 输出控制。
 *
 * 电机驱动使用 TIM1 的 CH1/CH2/CH3 及其互补输出 CH1N/CH2N/CH3N。
 * 状态机只调用 StartPWM()/StopPWM()，不直接碰 HAL_TIM_*，这样后续
 * 若要加入预充电、死区校验或刹车恢复逻辑，可以集中在这里改。
 */

/**
 * @brief 停止三相 PWM 和互补 PWM 输出。
 *
 * 通常在 STOP_CMD、故障状态或等待启动状态下调用，保证上下桥臂不再输出。
 */
void StopPWM(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_3);
}

/**
 * @brief 启动三相 PWM 和互补 PWM 输出。
 *
 * 一般在 MC_START 过渡完成后调用。调用前状态机会先执行 EN_GATE_SET
 * 和短延时，确保门极驱动已准备好。
 */
void StartPWM(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
}

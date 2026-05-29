#include "bsp.h"
#include "tim.h"

/* 第二阶段 PWM 模块：
 * TIM1 基准和 CH4 用于持续触发 ADC，三相 CH1/2/3 由 StartPWM()/StopPWM() 控制。
 */

void BspPwm_SetComparePercent(uint8_t a_percent, uint8_t b_percent, uint8_t c_percent)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;

    if (a_percent > 100U) { a_percent = 100U; }
    if (b_percent > 100U) { b_percent = 100U; }
    if (c_percent > 100U) { c_percent = 100U; }

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (period * a_percent) / 100U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, (period * b_percent) / 100U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, (period * c_percent) / 100U);
}

void BspPwm_StartAdcTrigger(void)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;

    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, period - 2U);
    BspPwm_SetComparePercent(25U, 50U, 75U);

    HAL_TIM_Base_Start(&htim1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
    HAL_TIM_Base_Start(&htim5);
}

void StartPWM(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
}

void StopPWM(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_3);
}

#include "bsp.h"
#include "tim.h"

/* 第二阶段 PWM 模块：
 * TIM1 基准和 CH4 用于持续触发 ADC，三相 CH1/2/3 由 StartPWM()/StopPWM() 控制。
 */

void BspPwm_SetComparePercent(uint8_t a_percent, uint8_t b_percent, uint8_t c_percent)
{
    /* period = ARR + 1，后面用百分比换算 CCR 比较值。 */
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;

    /* 防止传入超过 100 的占空比，避免 CCR 超过周期。 */
    if (a_percent > 100U) { a_percent = 100U; }
    if (b_percent > 100U) { b_percent = 100U; }
    if (c_percent > 100U) { c_percent = 100U; }

    /* 第二阶段还没有接 FOC 输出，所以先用固定百分比方便示波器观察。 */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (period * a_percent) / 100U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, (period * b_percent) / 100U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, (period * c_percent) / 100U);
}

void BspPwm_StartAdcTrigger(void)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;

    /* 清除高级定时器 Break 标志，避免 PWM 输出被保护标志锁住。 */
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK);
    /* CH4 放在周期末端附近，用作 ADC 注入转换触发点。 */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, period - 2U);
    /* 预先写入三相固定占空比；真正输出要等 StartPWM() 启动 CH1/2/3。 */
    BspPwm_SetComparePercent(25U, 50U, 75U);

    /* 启动 TIM1 基准和 CH4，让 ADC 触发链路先跑起来。 */
    HAL_TIM_Base_Start(&htim1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
    /* TIM5 当前只保持运行，后续可用于耗时测量或调试计时。 */
    HAL_TIM_Base_Start(&htim5);
}

void StartPWM(void)
{
    /* 启动三相主 PWM。 */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    /* 启动三相互补 PWM，用于半桥上下管驱动。 */
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
}

void StopPWM(void)
{
    /* 停止主输出和互补输出。故障、STOP 命令都会走到这里。 */
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_3);
}

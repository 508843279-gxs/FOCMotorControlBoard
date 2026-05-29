#include "bsp.h"

/* 第二阶段保护模块：
 * 先搭保护框架和状态灯，不做复杂锁存复位策略。
 */

#define STATUS_LED_ON             GPIO_PIN_RESET
#define STATUS_LED_OFF            GPIO_PIN_SET

#define VBUS_UNDERVOLTAGE_V       4.0f
#define VBUS_OVERVOLTAGE_V        20.0f
#define PHASE_OVERCURRENT_A       20.0f
#define MOTOR_OVERTEMP_C          80.0f

void StatusLed_AllOff(void)
{
    HAL_GPIO_WritePin(LED_UV_GPIO_Port, LED_UV_Pin, STATUS_LED_OFF);
    HAL_GPIO_WritePin(LED_OV_GPIO_Port, LED_OV_Pin, STATUS_LED_OFF);
    HAL_GPIO_WritePin(LED_OC_GPIO_Port, LED_OC_Pin, STATUS_LED_OFF);
    HAL_GPIO_WritePin(LED_OT_GPIO_Port, LED_OT_Pin, STATUS_LED_OFF);
}

void StatusLed_Set(enMcErr err, uint8_t on)
{
    GPIO_PinState state = on ? STATUS_LED_ON : STATUS_LED_OFF;

    switch (err)
    {
    case LV_ERR:
        HAL_GPIO_WritePin(LED_UV_GPIO_Port, LED_UV_Pin, state);
        break;
    case OV_ERR:
        HAL_GPIO_WritePin(LED_OV_GPIO_Port, LED_OV_Pin, state);
        break;
    case OC_ERR:
        HAL_GPIO_WritePin(LED_OC_GPIO_Port, LED_OC_Pin, state);
        break;
    case OT_ERR:
        HAL_GPIO_WritePin(LED_OT_GPIO_Port, LED_OT_Pin, state);
        break;
    default:
        break;
    }
}

void MotorProtection_CheckVbus(void)
{
    if (mc_info.vbus < VBUS_UNDERVOLTAGE_V)
    {
        mc_info.mc_err = LV_ERR;
        mc_info.mc_state = MC_ERR;
        StatusLed_Set(LV_ERR, 1U);
    }
    else if (mc_info.vbus > VBUS_OVERVOLTAGE_V)
    {
        if (++mc_info.over_vol_count > 5U)
        {
            mc_info.mc_err = OV_ERR;
            mc_info.mc_state = MC_ERR;
            mc_info.err_state_vbus = mc_info.vbus;
            StatusLed_Set(OV_ERR, 1U);
        }
    }
    else
    {
        mc_info.over_vol_count = 0U;
    }
}

void MotorProtection_CheckRunFaults(void)
{
    if (mc_info.isens_a > PHASE_OVERCURRENT_A || mc_info.isens_a < -PHASE_OVERCURRENT_A ||
        mc_info.isens_b > PHASE_OVERCURRENT_A || mc_info.isens_b < -PHASE_OVERCURRENT_A ||
        mc_info.isens_c > PHASE_OVERCURRENT_A || mc_info.isens_c < -PHASE_OVERCURRENT_A)
    {
        if (++mc_info.over_cur_count > 4U)
        {
            mc_info.mc_err = OC_ERR;
            mc_info.mc_state = MC_ERR;
            StatusLed_Set(OC_ERR, 1U);
        }
    }
    else
    {
        mc_info.over_cur_count = 0U;
    }

    if (mc_info.temperature >= MOTOR_OVERTEMP_C && mc_info.temperature < 200.0f)
    {
        if (++mc_info.over_temp_count > 4U)
        {
            mc_info.mc_err = OT_ERR;
            mc_info.mc_state = MC_ERR;
            StatusLed_Set(OT_ERR, 1U);
        }
    }
    else
    {
        mc_info.over_temp_count = 0U;
    }
}

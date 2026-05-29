#include "bsp.h"

/**
 * @file bsp_status_led.c
 * @brief 故障状态灯控制。
 *
 * 硬件连接方式为 LED 阳极接 3.3V，经限流电阻后由 MCU 引脚下拉点亮。
 * 因此 GPIO_RESET 表示点亮，GPIO_SET 表示熄灭。把这个反相逻辑集中在
 * 本模块，可以避免保护逻辑里到处散落裸 GPIO 操作。
 */

/* 低电平点亮，高电平熄灭。 */
#define STATUS_LED_ON GPIO_PIN_RESET
#define STATUS_LED_OFF GPIO_PIN_SET

/**
 * @brief 熄灭全部故障指示灯。
 *
 * 一般在上电初始化、故障复位、重新进入可启动状态时调用。
 */
void StatusLed_AllOff(void)
{
    HAL_GPIO_WritePin(LED_UV_GPIO_Port, LED_UV_Pin, STATUS_LED_OFF);
    HAL_GPIO_WritePin(LED_OV_GPIO_Port, LED_OV_Pin, STATUS_LED_OFF);
    HAL_GPIO_WritePin(LED_OC_GPIO_Port, LED_OC_Pin, STATUS_LED_OFF);
    HAL_GPIO_WritePin(LED_OT_GPIO_Port, LED_OT_Pin, STATUS_LED_OFF);
}

/**
 * @brief 根据故障类型点亮或熄灭对应状态灯。
 * @param err 故障类型，支持欠压、过压、过流、过温。
 * @param on  1 表示点亮，0 表示熄灭。
 */
void StatusLed_Set(enMcErr err, uint8_t on)
{
    /* 将逻辑上的 on/off 转换为当前硬件的 GPIO 电平。 */
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
        /* NONE_ERR 或未知故障不操作任何单灯。 */
        break;
    }
}

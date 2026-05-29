#include "bsp.h"
#include <stdio.h>

/* 第二阶段命令模块：
 * 按键和串口都只改 comm[]，状态机统一从 comm[] 读取命令。
 */

#define COMMAND_KEY_SCAN_MS        20U
#define COMMAND_RAMP_STEP_RPM      50.0f

uint16_t RPM1 = 2000U;
uint16_t RPM2 = 4000U;
uint16_t RPM3 = 6000U;

static uint32_t next_key_scan_ms;
static uint8_t motor_enable;
static int8_t motor_dir = 1;
static uint8_t speed_level;
static float ramp_rpm;

static GPIO_PinState key_run_last = GPIO_PIN_SET;
static GPIO_PinState key_dir_last = GPIO_PIN_SET;
static GPIO_PinState key_up_last = GPIO_PIN_SET;
static GPIO_PinState key_down_last = GPIO_PIN_SET;

static float Command_LevelToRpm(void)
{
    switch (speed_level)
    {
    case 1U:
        return (float)RPM1;
    case 2U:
        return (float)RPM2;
    case 3U:
        return (float)RPM3;
    default:
        return 0.0f;
    }
}

static float Command_Ramp(float current, float target)
{
    if (current < target - COMMAND_RAMP_STEP_RPM)
    {
        return current + COMMAND_RAMP_STEP_RPM;
    }
    if (current > target + COMMAND_RAMP_STEP_RPM)
    {
        return current - COMMAND_RAMP_STEP_RPM;
    }
    return target;
}

static uint8_t Command_KeyPressed(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState *last_state)
{
    GPIO_PinState state = HAL_GPIO_ReadPin(port, pin);
    uint8_t pressed = 0U;

    if (state != *last_state)
    {
        *last_state = state;
        if (state == GPIO_PIN_RESET)
        {
            pressed = 1U;
        }
    }

    return pressed;
}

static void Command_PrintStatus(const char *tag)
{
    printf("[BSP2] %s run=%u level=%u dir=%d rpm=%d state=%d err=%d\r\n",
           tag,
           motor_enable,
           speed_level,
           motor_dir,
           (int)ramp_rpm,
           mc_info.mc_state,
           mc_info.mc_err);
}

static void Command_ClearFault(void)
{
    mc_info.mc_err = NONE_ERR;
    mc_info.mc_state = MC_STOP;
    StatusLed_AllOff();
    printf("[BSP2] FAULT_RESET\r\n");
}

void KeyControl_Update(void)
{
    uint32_t now = HAL_GetTick();
    float target_rpm;

    if ((int32_t)(now - next_key_scan_ms) < 0)
    {
        return;
    }
    next_key_scan_ms = now + COMMAND_KEY_SCAN_MS;

    if (Command_KeyPressed(KEY_RUN_GPIO_Port, KEY_RUN_Pin, &key_run_last))
    {
        if (mc_info.mc_state == MC_ERR || mc_info.mc_err != NONE_ERR)
        {
            motor_enable = 0U;
            Command_ClearFault();
        }
        else
        {
            motor_enable = !motor_enable;
            if (motor_enable && speed_level == 0U)
            {
                speed_level = 1U;
            }
            Command_PrintStatus("KEY_RUN");
        }
    }

    if (Command_KeyPressed(KEY_DIR_GPIO_Port, KEY_DIR_Pin, &key_dir_last))
    {
        motor_dir = (int8_t)-motor_dir;
        Command_PrintStatus("KEY_DIR");
    }

    if (Command_KeyPressed(KEY_SPEED_UP_GPIO_Port, KEY_SPEED_UP_Pin, &key_up_last))
    {
        if (speed_level < 3U)
        {
            speed_level++;
        }
        Command_PrintStatus("KEY_UP");
    }

    if (Command_KeyPressed(KEY_SPEED_DOWN_GPIO_Port, KEY_SPEED_DOWN_Pin, &key_down_last))
    {
        if (speed_level > 0U)
        {
            speed_level--;
        }
        Command_PrintStatus("KEY_DOWN");
    }

    target_rpm = motor_enable ? ((float)motor_dir * Command_LevelToRpm()) : 0.0f;
    ramp_rpm = Command_Ramp(ramp_rpm, target_rpm);

    comm[1] = ramp_rpm;
    comm[2] = (motor_enable && speed_level > 0U) ? 1.0f : 0.0f;
}

static uint8_t Command_Equals(const char *cmd, const char *target)
{
    while (*cmd != '\0' && *target != '\0')
    {
        if (*cmd++ != *target++)
        {
            return 0U;
        }
    }
    return *cmd == '\0' && *target == '\0';
}

void MotorControl_HandleUartCommand(uint8_t *data, uint16_t size)
{
    char cmd[24];
    uint16_t i;
    uint16_t len = 0U;

    for (i = 0U; i < size && len < sizeof(cmd) - 1U; i++)
    {
        char c = (char)data[i];
        if (c == '\r' || c == '\n')
        {
            break;
        }
        if (c >= 'a' && c <= 'z')
        {
            c = (char)(c - 'a' + 'A');
        }
        if (c != ' ' && c != '\t')
        {
            cmd[len++] = c;
        }
    }
    cmd[len] = '\0';

    if (len == 0U)
    {
        return;
    }

    if (Command_Equals(cmd, "RUN") || Command_Equals(cmd, "START"))
    {
        if (mc_info.mc_state == MC_ERR || mc_info.mc_err != NONE_ERR)
        {
            Command_ClearFault();
        }
        motor_enable = 1U;
        if (speed_level == 0U)
        {
            speed_level = 1U;
        }
        Command_PrintStatus("UART_RUN");
    }
    else if (Command_Equals(cmd, "STOP"))
    {
        motor_enable = 0U;
        Command_PrintStatus("UART_STOP");
    }
    else if (Command_Equals(cmd, "DIR"))
    {
        motor_dir = (int8_t)-motor_dir;
        Command_PrintStatus("UART_DIR");
    }
    else if (Command_Equals(cmd, "UP"))
    {
        if (speed_level < 3U)
        {
            speed_level++;
        }
        Command_PrintStatus("UART_UP");
    }
    else if (Command_Equals(cmd, "DOWN"))
    {
        if (speed_level > 0U)
        {
            speed_level--;
        }
        Command_PrintStatus("UART_DOWN");
    }
    else if (Command_Equals(cmd, "STATUS"))
    {
        Command_PrintStatus("UART_STATUS");
    }
    else if (Command_Equals(cmd, "HELP"))
    {
        printf("[BSP2] CMD: RUN STOP DIR UP DOWN STATUS HELP\r\n");
    }
    else
    {
        printf("[BSP2] ERR UNKNOWN CMD: %s\r\n", cmd);
    }
}

#include "bsp.h"
#include <stdio.h>

/* 第二阶段命令模块：
 * 按键和串口都只改 comm[]，状态机统一从 comm[] 读取命令。
 */

#define COMMAND_KEY_SCAN_MS        20U
#define COMMAND_RAMP_STEP_RPM      50.0f

/* 三个速度档位。后续如果要改最高转速，优先改这里或 main.h 中的配置入口。 */
uint16_t RPM1 = 2000U;
uint16_t RPM2 = 4000U;
uint16_t RPM3 = 6000U;

/* 按键扫描软件定时，避免主循环太快导致按键重复触发。 */
static uint32_t next_key_scan_ms;
/* 命令层运行使能：1 表示允许状态机启动，0 表示停止。 */
static uint8_t motor_enable;
/* 当前方向：1 正向，-1 反向。 */
static int8_t motor_dir = 1;
/* 当前速度档位：0 停止，1/2/3 对应 RPM1/RPM2/RPM3。 */
static uint8_t speed_level;
/* 斜坡后的当前转速命令，最终写入 comm[1]。 */
static float ramp_rpm;

/* 保存按键上一次电平，用于检测按下边沿。 */
static GPIO_PinState key_run_last = GPIO_PIN_SET;
static GPIO_PinState key_dir_last = GPIO_PIN_SET;
static GPIO_PinState key_up_last = GPIO_PIN_SET;
static GPIO_PinState key_down_last = GPIO_PIN_SET;

static float Command_LevelToRpm(void)
{
    /* 把档位转换成无方向符号的目标转速。 */
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
    /* 简单线性斜坡：每次调用最多变化 COMMAND_RAMP_STEP_RPM。 */
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

    /* 当前按键为上拉输入、低电平有效，只在“松开->按下”的瞬间返回 1。 */
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
    /* 命令层统一打印入口，调试按键/串口时主要看这一行。 */
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
    /* 清故障只清软件状态；如果硬件故障条件仍在，保护模块会再次置故障。 */
    mc_info.mc_err = NONE_ERR;
    mc_info.mc_state = MC_STOP;
    StatusLed_AllOff();
    printf("[BSP2] FAULT_RESET\r\n");
}

void KeyControl_Update(void)
{
    uint32_t now = HAL_GetTick();
    float target_rpm;

    /* 20ms 扫描一次按键，降低按键抖动和重复打印。 */
    if ((int32_t)(now - next_key_scan_ms) < 0)
    {
        return;
    }
    next_key_scan_ms = now + COMMAND_KEY_SCAN_MS;

    /* RUN 键：正常时启停切换；故障时作为清故障键。 */
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

    /* DIR 键：只改变方向符号，斜坡转速会自动向新目标靠近。 */
    if (Command_KeyPressed(KEY_DIR_GPIO_Port, KEY_DIR_Pin, &key_dir_last))
    {
        motor_dir = (int8_t)-motor_dir;
        Command_PrintStatus("KEY_DIR");
    }

    /* 加档键：最高 3 档，不循环。 */
    if (Command_KeyPressed(KEY_SPEED_UP_GPIO_Port, KEY_SPEED_UP_Pin, &key_up_last))
    {
        if (speed_level < 3U)
        {
            speed_level++;
        }
        Command_PrintStatus("KEY_UP");
    }

    /* 减档键：最低 0 档，0 档会输出停止命令。 */
    if (Command_KeyPressed(KEY_SPEED_DOWN_GPIO_Port, KEY_SPEED_DOWN_Pin, &key_down_last))
    {
        if (speed_level > 0U)
        {
            speed_level--;
        }
        Command_PrintStatus("KEY_DOWN");
    }

    /* 根据运行使能、方向和档位计算最终目标 RPM。 */
    target_rpm = motor_enable ? ((float)motor_dir * Command_LevelToRpm()) : 0.0f;
    /* 转速命令不突变，先经过斜坡。 */
    ramp_rpm = Command_Ramp(ramp_rpm, target_rpm);

    /* 把命令层结果写入 comm[]，状态机只读 comm[]，不关心命令来自按键还是串口。 */
    comm[1] = ramp_rpm;
    comm[2] = (motor_enable && speed_level > 0U) ? 1.0f : 0.0f;
}

static uint8_t Command_Equals(const char *cmd, const char *target)
{
    /* 小型字符串比较函数，避免引入额外字符串处理依赖。 */
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

    /* 将串口收到的一帧数据整理成大写命令，忽略空格和 TAB。 */
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

    /* 空命令直接忽略，例如串口助手只发了换行。 */
    if (len == 0U)
    {
        return;
    }

    /* RUN/START：置运行使能；如果当前有故障，先清软件故障状态。 */
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
    /* STOP：清运行使能，状态机会在下一次 BspTask() 中停 PWM。 */
    else if (Command_Equals(cmd, "STOP"))
    {
        motor_enable = 0U;
        Command_PrintStatus("UART_STOP");
    }
    /* DIR/UP/DOWN 和按键一样，只改命令层状态。 */
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

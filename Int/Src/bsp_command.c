#include "bsp.h"
#include "stdio.h"

/**
 * @file bsp_command.c
 * @brief 按键与串口命令解析。
 *
 * 本模块把不同控制入口统一转换成 comm[]：
 *   - 按键：RUN、DIR、SPEED_UP、SPEED_DOWN；
 *   - 串口：RUN/START、STOP、DIR、UP、DOWN、LVL 0..3、STATUS、HELP。
 *
 * 后面的状态机只读取 comm[]，这样按键和串口不会各自直接操作电机状态机，
 * 控制入口会更清晰，也便于后续增加新的通信协议。
 */

/* 按键消抖需要连续稳定的控制周期数量。 */
#define KEY_DEBOUNCE_TICKS 200U
/* 速度档位范围：0 档停止，1~3 档对应 RPM1/RPM2/RPM3。 */
#define KEY_LEVEL_MIN 0U
#define KEY_LEVEL_MAX 3U
/* 每个控制周期转速指令变化量，数值越大加减速越快。 */
#define KEY_RPM_RAMP_STEP 0.2f
/* 判断转速已经接近 0 的死区，主要用于运行中换向。 */
#define KEY_RPM_ZERO_BAND 1.0f

/* 按键名称，仅用于调试打印。 */
typedef enum
{
    KEY_NAME_RUN,
    KEY_NAME_DIR,
    KEY_NAME_SPEED_UP,
    KEY_NAME_SPEED_DOWN
} KeyName_t;

/* 单个按键的软件消抖状态。 */
typedef struct
{
    GPIO_TypeDef *port;        /* 按键所在 GPIO 端口 */
    uint16_t pin;              /* 按键所在 GPIO 引脚 */
    KeyName_t name;            /* 调试打印用名称 */
    uint8_t stable_pressed;    /* 消抖后的稳定状态，1 表示按下 */
    uint8_t last_stable_pressed;/* 上一次稳定状态，用于检测按下沿 */
    uint16_t debounce_count;   /* 连续不一致计数，达到阈值才确认变化 */
} KeyDebounce_t;

/* 运行使能，1 表示允许状态机启动电机。 */
static uint8_t key_motor_enable = 0;
/* 当前方向：1 正转，-1 反转。 */
static int8_t key_motor_dir = 1;
/* 运行中请求换向时，先把新方向暂存在这里。 */
static int8_t key_pending_dir = 1;
/* 当前速度档位，0 停止，1~3 对应 main.c 中的 RPM1/RPM2/RPM3。 */
static uint8_t key_speed_level = 0;
/* 换向流程标志：1 表示正在先降速到 0，再切换方向。 */
static uint8_t key_dir_change_active = 0;
/* 斜坡后的当前目标转速，最终写入 comm[1]。 */
static float key_current_rpm = 0.0f;

/**
 * @brief 将按键枚举转换成可读字符串。
 * @param name 按键枚举。
 * @return 字符串常量，用于串口调试输出。
 */
static const char *KeyNameString(KeyName_t name)
{
    switch (name)
    {
    case KEY_NAME_RUN:
        return "KEY_RUN";
    case KEY_NAME_DIR:
        return "KEY_DIR";
    case KEY_NAME_SPEED_UP:
        return "KEY_SPEED_UP";
    case KEY_NAME_SPEED_DOWN:
        return "KEY_SPEED_DOWN";
    default:
        return "KEY_UNKNOWN";
    }
}

/**
 * @brief 扫描一个按键并检测有效按下沿。
 * @param key 按键消抖状态结构体。
 * @return 1 表示本周期检测到新的按下沿，0 表示没有。
 *
 * 当前硬件按键为低电平有效，所以 GPIO_PIN_RESET 被认为是按下。
 * 函数只在“松开 -> 按下”的瞬间返回 1，长按不会重复触发。
 */
static uint8_t KeyPressedEdge(KeyDebounce_t *key)
{
    /* 读取原始电平，并转换成逻辑按下状态。 */
    uint8_t raw_pressed = HAL_GPIO_ReadPin(key->port, key->pin) == GPIO_PIN_RESET;

    /* 原始状态与稳定状态不同，说明可能发生了按键变化或抖动。 */
    if (raw_pressed != key->stable_pressed)
    {
        /* 连续保持不同达到阈值，才接受本次变化。 */
        if (++key->debounce_count >= KEY_DEBOUNCE_TICKS)
        {
            key->last_stable_pressed = key->stable_pressed;
            key->stable_pressed = raw_pressed;
            key->debounce_count = 0;

            /* 只响应按下沿，松开沿不触发控制动作。 */
            if (key->stable_pressed && !key->last_stable_pressed)
            {
                printf("%s pressed\r\n", KeyNameString(key->name));
                return 1;
            }

            return 0;
        }
    }
    else
    {
        /* 状态稳定时清零计数器，等待下一次变化。 */
        key->debounce_count = 0;
    }

    return 0;
}

/**
 * @brief 根据速度档位获取无方向符号的目标 RPM。
 * @return 0/RPM1/RPM2/RPM3。
 */
static float KeyLevelRPM(void)
{
    switch (key_speed_level)
    {
    case 1:
        return RPM1;
    case 2:
        return RPM2;
    case 3:
        return RPM3;
    default:
        return 0.0f;
    }
}

/**
 * @brief 根据当前方向和档位获取带符号目标 RPM。
 * @return 正值为正转，负值为反转，0 为停止。
 */
static float KeyTargetRPM(void)
{
    return key_motor_dir * KeyLevelRPM();
}

/**
 * @brief 对转速命令做线性斜坡。
 * @param current 当前斜坡值。
 * @param target 目标值。
 * @return 下一控制周期应使用的转速命令。
 */
static float KeyRampRPM(float current, float target)
{
    if (current < target - KEY_RPM_RAMP_STEP)
    {
        return current + KEY_RPM_RAMP_STEP;
    }

    if (current > target + KEY_RPM_RAMP_STEP)
    {
        return current - KEY_RPM_RAMP_STEP;
    }

    return target;
}

static uint8_t KeyRpmNearZero(float rpm)
{
    return (rpm > -KEY_RPM_ZERO_BAND && rpm < KEY_RPM_ZERO_BAND);
}

/**
 * @brief 打印控制层状态，辅助确认按键/串口命令是否生效。
 * @param reason 触发打印的事件名称。
 */
static void KeyDebug_PrintState(const char *reason)
{
    printf("%s: run=%u level=%u dir=%d pending=%d ramp=%d target=%d state=%d err=%d vbus=%d\r\n",
           reason,
           key_motor_enable,
           key_speed_level,
           key_motor_dir,
           key_pending_dir,
           (int)key_current_rpm,
           (int)KeyTargetRPM(),
           mc_info.mc_state,
           mc_info.mc_err,
           (int)mc_info.vbus);
}

/**
 * @brief 按键控制主任务。
 *
 * 每个 ADC 控制周期调用一次，完成：
 *   1. 扫描四个按键并做消抖；
 *   2. RUN 键控制启停，故障状态下作为故障复位；
 *   3. DIR 键控制方向，运行中换向会先斜坡降速到 0；
 *   4. SPEED_UP/DOWN 调整 0~3 档；
 *   5. 将最终启停和转速写入 comm[2]/comm[1]。
 */
void KeyControl_Update(void)
{
    /* static 保留每个按键的消抖历史。 */
    static KeyDebounce_t key_run = {KEY_RUN_GPIO_Port, KEY_RUN_Pin, KEY_NAME_RUN, 0, 0, 0};
    static KeyDebounce_t key_dir = {KEY_DIR_GPIO_Port, KEY_DIR_Pin, KEY_NAME_DIR, 0, 0, 0};
    static KeyDebounce_t key_speed_up = {KEY_SPEED_UP_GPIO_Port, KEY_SPEED_UP_Pin, KEY_NAME_SPEED_UP, 0, 0, 0};
    static KeyDebounce_t key_speed_down = {KEY_SPEED_DOWN_GPIO_Port, KEY_SPEED_DOWN_Pin, KEY_NAME_SPEED_DOWN, 0, 0, 0};
    uint8_t run_pressed = KeyPressedEdge(&key_run);
    float target_rpm;

    /* 故障状态下强制停止，RUN 键用于清故障。 */
    if (mc_info.mc_state == MC_ERR || mc_info.mc_err != NONE_ERR)
    {
        key_motor_enable = 0;
        key_dir_change_active = 0;
        key_current_rpm = 0.0f;

        if (run_pressed)
        {
            mc_info.mc_state = MC_STOP;
            mc_info.mc_err = NONE_ERR;
            StatusLed_AllOff();
            KeyDebug_PrintState("FAULT_RESET");
        }
    }
    /* 正常状态下 RUN 键在运行和停止之间切换。 */
    else if (run_pressed)
    {
        key_motor_enable = !key_motor_enable;

        if (key_motor_enable && key_speed_level == KEY_LEVEL_MIN)
        {
            key_speed_level = 1;
        }

        if (!key_motor_enable)
        {
            key_dir_change_active = 0;
        }

        KeyDebug_PrintState("RUN_TOGGLE");
    }

    /* 方向键：运行中先缓停再反向，停机时直接反向。 */
    if (KeyPressedEdge(&key_dir))
    {
        if (key_motor_enable && key_speed_level > KEY_LEVEL_MIN)
        {
            key_pending_dir = -key_motor_dir;
            key_dir_change_active = 1;
            KeyDebug_PrintState("DIR_RAMP_START");
        }
        else
        {
            key_motor_dir = -key_motor_dir;
            key_pending_dir = key_motor_dir;
            KeyDebug_PrintState("DIR_TOGGLE");
        }
    }

    /* 加档：最高保持在 3 档，不循环。 */
    if (KeyPressedEdge(&key_speed_up))
    {
        if (key_speed_level < KEY_LEVEL_MAX)
        {
            key_speed_level++;
        }
        KeyDebug_PrintState("SPEED_UP");
    }

    /* 减档：最低保持在 0 档，不循环。 */
    if (KeyPressedEdge(&key_speed_down))
    {
        if (key_speed_level > KEY_LEVEL_MIN)
        {
            key_speed_level--;
        }
        KeyDebug_PrintState("SPEED_DOWN");
    }

    /* 未使能或 0 档时，输出停止命令并清零斜坡。 */
    if (!key_motor_enable || key_speed_level == KEY_LEVEL_MIN)
    {
        key_dir_change_active = 0;
        key_current_rpm = KeyRampRPM(key_current_rpm, 0.0f);

        if (KeyRpmNearZero(key_current_rpm))
        {
            key_current_rpm = 0.0f;
            comm[2] = 0;
            comm[1] = 0.0f;
        }
        else
        {
            comm[2] = 1;
            comm[1] = key_current_rpm;
        }
    }
    else
    {
        /* 换向第一阶段：目标先降到 0。 */
        if (key_dir_change_active)
        {
            target_rpm = 0.0f;
            key_current_rpm = KeyRampRPM(key_current_rpm, target_rpm);

            if (KeyRpmNearZero(key_current_rpm))
            {
                /* 接近 0 后真正切换方向，下一周期开始向反方向加速。 */
                key_current_rpm = 0.0f;
                key_motor_dir = key_pending_dir;
                key_dir_change_active = 0;
                KeyDebug_PrintState("DIR_REVERSED");
            }
        }
        else
        {
            /* 正常运行：向当前档位/方向的目标 RPM 斜坡逼近。 */
            target_rpm = KeyTargetRPM();
            key_current_rpm = KeyRampRPM(key_current_rpm, target_rpm);
        }

        /* 统一将控制结果交给状态机入口。 */
        comm[2] = 1;
        comm[1] = key_current_rpm;
    }

    /* 保留原工程计时变量语义：按键控制周期内清零方向计时。 */
    forwardTimeCnt = 0;
    backTimeCnt = 0;
}

/** @brief 判断命令字符串是否完全相同。 */
static uint8_t CmdEquals(const char *cmd, const char *target)
{
    while (*cmd != '\0' && *target != '\0')
    {
        if (*cmd++ != *target++)
        {
            return 0;
        }
    }

    return *cmd == '\0' && *target == '\0';
}

/** @brief 判断命令字符串是否以指定前缀开头。 */
static uint8_t CmdStartsWith(const char *cmd, const char *target)
{
    while (*target != '\0')
    {
        if (*cmd++ != *target++)
        {
            return 0;
        }
    }

    return 1;
}

/** @brief 串口 RUN/START：启动电机；故障状态下先清故障。 */
static void CmdRun(void)
{
    if (mc_info.mc_state == MC_ERR || mc_info.mc_err != NONE_ERR)
    {
        mc_info.mc_state = MC_STOP;
        mc_info.mc_err = NONE_ERR;
        StatusLed_AllOff();
        printf("OK FAULT_RESET\r\n");
    }

    key_motor_enable = 1;
    if (key_speed_level == KEY_LEVEL_MIN)
    {
        key_speed_level = 1;
    }
    KeyDebug_PrintState("UART_RUN");
}

/** @brief 串口 STOP：停止电机，并清除当前斜坡转速。 */
static void CmdStop(void)
{
    key_motor_enable = 0;
    key_dir_change_active = 0;
    KeyDebug_PrintState("UART_STOP");
}

/** @brief 串口 DIR：切换方向，运行中执行缓停换向。 */
static void CmdDir(void)
{
    if (key_motor_enable && key_speed_level > KEY_LEVEL_MIN)
    {
        key_pending_dir = -key_motor_dir;
        key_dir_change_active = 1;
        KeyDebug_PrintState("UART_DIR_RAMP_START");
    }
    else
    {
        key_motor_dir = -key_motor_dir;
        key_pending_dir = key_motor_dir;
        KeyDebug_PrintState("UART_DIR_TOGGLE");
    }
}

/** @brief 串口 UP：速度档位加 1。 */
static void CmdSpeedUp(void)
{
    if (key_speed_level < KEY_LEVEL_MAX)
    {
        key_speed_level++;
    }
    KeyDebug_PrintState("UART_SPEED_UP");
}

/** @brief 串口 DOWN：速度档位减 1。 */
static void CmdSpeedDown(void)
{
    if (key_speed_level > KEY_LEVEL_MIN)
    {
        key_speed_level--;
    }
    KeyDebug_PrintState("UART_SPEED_DOWN");
}

/**
 * @brief 串口 LVL/LEVEL：直接设置速度档位。
 * @param level 目标档位，允许 0~3。
 */
static void CmdSetLevel(uint8_t level)
{
    if (level > KEY_LEVEL_MAX)
    {
        printf("ERR LVL 0..3\r\n");
        return;
    }

    key_speed_level = level;
    KeyDebug_PrintState("UART_LVL");
}

/**
 * @brief 串口命令解析入口。
 * @param data 串口接收缓冲区。
 * @param size 本次接收到的有效字节数。
 *
 * 命令会先去掉首尾空白，并把小写字母转成大写。支持：
 *   RUN/START、STOP、DIR、UP、DOWN、LVL 0..3、LEVEL 0..3、STATUS、HELP。
 */
void MotorControl_HandleUartCommand(uint8_t *data, uint16_t size)
{
    char cmd[32];
    uint16_t src = 0;
    uint16_t dst = 0;
    uint8_t level;

    /* 跳过前导空格、TAB 和换行。 */
    while (src < size && (data[src] == ' ' || data[src] == '\t' || data[src] == '\r' || data[src] == '\n'))
    {
        src++;
    }

    /* 拷贝命令主体，并转换为大写，直到 CR/LF 或缓冲区满。 */
    while (src < size && dst < sizeof(cmd) - 1)
    {
        char c = (char)data[src++];

        if (c == '\r' || c == '\n')
        {
            break;
        }
        if (c >= 'a' && c <= 'z')
        {
            c = c - 'a' + 'A';
        }
        cmd[dst++] = c;
    }

    /* 去掉尾部空白，兼容串口工具发送的空格或换行。 */
    while (dst > 0 && (cmd[dst - 1] == ' ' || cmd[dst - 1] == '\t'))
    {
        dst--;
    }
    cmd[dst] = '\0';

    if (dst == 0)
    {
        return;
    }

    /* 命令分发。 */
    if (CmdEquals(cmd, "RUN") || CmdEquals(cmd, "START"))
    {
        CmdRun();
    }
    else if (CmdEquals(cmd, "STOP"))
    {
        CmdStop();
    }
    else if (CmdEquals(cmd, "DIR"))
    {
        CmdDir();
    }
    else if (CmdEquals(cmd, "UP"))
    {
        CmdSpeedUp();
    }
    else if (CmdEquals(cmd, "DOWN"))
    {
        CmdSpeedDown();
    }
    else if (CmdStartsWith(cmd, "LVL ") || CmdStartsWith(cmd, "LEVEL "))
    {
        /* 根据前缀长度定位参数起始位置。 */
        char *p = CmdStartsWith(cmd, "LVL ") ? &cmd[4] : &cmd[6];

        /* 允许参数前有多个空格。 */
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }

        /* 只接受单个数字 0~3，避免 LVL 12 被误认为 1 档。 */
        if (*p >= '0' && *p <= '3' && (p[1] == '\0' || p[1] == ' ' || p[1] == '\t'))
        {
            level = (uint8_t)(*p - '0');
            CmdSetLevel(level);
        }
        else
        {
            printf("ERR LVL 0..3\r\n");
        }
    }
    else if (CmdEquals(cmd, "STATUS"))
    {
        KeyDebug_PrintState("UART_STATUS");
    }
    else if (CmdEquals(cmd, "HELP"))
    {
        printf("CMD: RUN STOP DIR UP DOWN LVL 0..3 STATUS\r\n");
    }
    else
    {
        printf("ERR UNKNOWN CMD: %s\r\n", cmd);
    }
}

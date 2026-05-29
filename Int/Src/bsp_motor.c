#include "bsp.h"
#include "FOC_CURRENT.h"
#include "tim.h"
#include <stdio.h>

/* 第三阶段状态机和 FOC 调度模块：
 * 状态机负责 RUN/STOP/ERR，快速控制函数负责把 ADC 采样送进 FOC_CURRENT。
 */

float comm[10];
stMcInfo mc_info;

/* 上一次已经打印过的状态。状态变化时才打印，避免串口被刷屏。 */
static enMcState last_report_state = MC_ERR;

/* 将 FOC 输出的浮点比较值限制在 TIM1 的有效 CCR 范围内。
 * 这样即使模型输出异常，也不会把比较值写到 ARR 之外。
 */
static uint32_t MotorControl_ClampCompare(float value)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1);

    if (value < 0.0f)
    {
        return 0U;
    }
    if (value > (float)period)
    {
        return period;
    }
    return (uint32_t)value;
}

static void MotorControl_PrintState(const char *tag)
{
    /* 状态机调试打印：重点看 cmd/state/err/rpm。 */
    printf("[FOC3] %s cmd=%d state=%d err=%d rpm=%d vbus=%d temp=%d\r\n",
           tag,
           mc_info.cmd,
           mc_info.mc_state,
           mc_info.mc_err,
           (int)mc_info.refRPM,
           (int)mc_info.vbus,
           (int)mc_info.temperature);
}

void ParaInit(void)
{
    /* 上电或重新初始化时，统一进入安全停止状态。 */
    mc_info.cmd = STOP_CMD;
    mc_info.mc_state = MC_STOP;
    mc_info.mc_err = NONE_ERR;
    mc_info.refRPM = 0.0f;
    mc_info.refIq = 0.0f;
    mc_info.refId = 0.0f;
    mc_info.over_vol_count = 0U;
    mc_info.over_cur_count = 0U;
    mc_info.over_temp_count = 0U;
    comm[0] = 0.0f;
    comm[1] = 0.0f;
    comm[2] = 0.0f;
    StatusLed_AllOff();
    /* 初始化结束时先关 PWM，避免上电瞬间误输出。 */
    StopPWM();
}

void MotorControl_UpdateCommand(void)
{
    /* 状态机只认识 START_CMD/STOP_CMD，这里把 comm[] 转成枚举命令。 */
    mc_info.cmd = comm[2] != 0.0f ? START_CMD : STOP_CMD;
    mc_info.refIq = comm[0];
    mc_info.refRPM = comm[1];
}

void MotorControl_StateMachineStep(void)
{
    /* 状态变化时打印一次，方便观察 MC_STOP -> MC_START -> MC_RUN。 */
    if (last_report_state != mc_info.mc_state)
    {
        MotorControl_PrintState("STATE");
        last_report_state = mc_info.mc_state;
    }

    switch (mc_info.mc_state)
    {
    case MC_RDY:
    case MC_STOP:
        /* 停止/就绪状态保持 PWM 关闭；收到 START 才进入启动过渡。 */
        StopPWM();
        if (mc_info.mc_err == NONE_ERR && mc_info.cmd == START_CMD)
        {
            mc_info.mc_state = MC_START;
        }
        break;

    case MC_START:
        /* 每次启动前重新初始化 FOC 模型，清掉上一次运行残留的积分和观测器状态。 */
        FOC_CURRENT_initialize();
        FOC_CURRENT_U.Ref_Id = 0.0f;
        ObserverParam.RefRPM = mc_info.refRPM;
        /* 先给三相 50% 中点，等待下一次 ADC 回调计算出真正的 FOC 比较值。 */
        BspPwm_SetComparePercent(50U, 50U, 50U);
        StartPWM();
        mc_info.mc_state = MC_RUN;
        break;

    case MC_RUN:
        /* MC_RUN 的快速 FOC 和过流/过温检查在 ADC 回调中执行，这里只处理慢速命令。 */
        /* 收到 STOP 命令后立即停 PWM，回到停止状态。 */
        if (mc_info.cmd == STOP_CMD)
        {
            StopPWM();
            FOC_CURRENT_initialize();
            mc_info.mc_state = MC_STOP;
        }
        /* 任意模块置故障后，统一进入 MC_ERR。 */
        if (mc_info.mc_err != NONE_ERR)
        {
            StopPWM();
            mc_info.mc_state = MC_ERR;
        }
        break;

    case MC_ERR:
        /* 故障状态强制关闭 PWM，并清启动命令，等待按键/串口清故障。 */
        StopPWM();
        comm[2] = 0.0f;
        break;

    default:
        mc_info.mc_state = MC_ERR;
        break;
    }
}

void MotorControl_FocControlStep(void)
{
    /* FOC 必须在电机处于 MC_RUN 且没有故障时才运行。 */
    if (mc_info.mc_state != MC_RUN || mc_info.mc_err != NONE_ERR)
    {
        return;
    }

    /* 把 BSP 采样值送入 Simulink 生成的 FOC 模型输入。 */
    FOC_CURRENT_U.ISensA = mc_info.isens_a;
    FOC_CURRENT_U.ISensB = mc_info.isens_b;
    FOC_CURRENT_U.ISensC = mc_info.isens_c;
    FOC_CURRENT_U.VBus = mc_info.vbus;
    FOC_CURRENT_U.Ref_Id = mc_info.refId;
    /* 速度给定通过 ObserverParam.RefRPM 进入模型内部速度外环。 */
    ObserverParam.RefRPM = mc_info.refRPM;

    /* 执行一个 FOC 控制周期。输出为 TIM1 CH1/CH2/CH3 比较值。 */
    FOC_CURRENT_step();

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, MotorControl_ClampCompare(FOC_CURRENT_Y.tAout));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, MotorControl_ClampCompare(FOC_CURRENT_Y.tBout));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, MotorControl_ClampCompare(FOC_CURRENT_Y.tCout));

    /* FOC 输出后检查运行期故障；一旦触发，立即停 PWM 并进入故障状态。 */
    MotorProtection_CheckRunFaults();
    if (mc_info.mc_err != NONE_ERR)
    {
        StopPWM();
        mc_info.mc_state = MC_ERR;
    }
}

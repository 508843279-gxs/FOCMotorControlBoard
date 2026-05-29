#include "bsp.h"
#include <stdio.h>

/* 第二阶段状态机模块：
 * 目前只恢复状态流转和 PWM 启停，不接入 FOC_CURRENT 模型。
 */

float comm[10];
stMcInfo mc_info;

/* 上一次已经打印过的状态。状态变化时才打印，避免串口被刷屏。 */
static enMcState last_report_state = MC_ERR;

static void MotorControl_PrintState(const char *tag)
{
    /* 状态机调试打印：重点看 cmd/state/err/rpm。 */
    printf("[BSP2] %s cmd=%d state=%d err=%d rpm=%d vbus=%d temp=%d\r\n",
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
        /* 第二阶段还没有 FOC，先给三相固定占空比，方便示波器确认 PWM。 */
        BspPwm_SetComparePercent(25U, 50U, 75U);
        StartPWM();
        mc_info.mc_state = MC_RUN;
        break;

    case MC_RUN:
        /* 运行状态先检查过流/过温；母线电压保护在 ADC 回调里已经检查。 */
        MotorProtection_CheckRunFaults();
        /* 收到 STOP 命令后立即停 PWM，回到停止状态。 */
        if (mc_info.cmd == STOP_CMD)
        {
            StopPWM();
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

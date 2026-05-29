#include "bsp.h"
#include <stdio.h>

/* 第二阶段状态机模块：
 * 目前只恢复状态流转和 PWM 启停，不接入 FOC_CURRENT 模型。
 */

float comm[10];
stMcInfo mc_info;

static enMcState last_report_state = MC_ERR;

static void MotorControl_PrintState(const char *tag)
{
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
    StopPWM();
}

void MotorControl_UpdateCommand(void)
{
    mc_info.cmd = comm[2] != 0.0f ? START_CMD : STOP_CMD;
    mc_info.refIq = comm[0];
    mc_info.refRPM = comm[1];
}

void MotorControl_StateMachineStep(void)
{
    if (last_report_state != mc_info.mc_state)
    {
        MotorControl_PrintState("STATE");
        last_report_state = mc_info.mc_state;
    }

    switch (mc_info.mc_state)
    {
    case MC_RDY:
    case MC_STOP:
        StopPWM();
        if (mc_info.mc_err == NONE_ERR && mc_info.cmd == START_CMD)
        {
            mc_info.mc_state = MC_START;
        }
        break;

    case MC_START:
        BspPwm_SetComparePercent(25U, 50U, 75U);
        StartPWM();
        mc_info.mc_state = MC_RUN;
        break;

    case MC_RUN:
        MotorProtection_CheckRunFaults();
        if (mc_info.cmd == STOP_CMD)
        {
            StopPWM();
            mc_info.mc_state = MC_STOP;
        }
        if (mc_info.mc_err != NONE_ERR)
        {
            StopPWM();
            mc_info.mc_state = MC_ERR;
        }
        break;

    case MC_ERR:
        StopPWM();
        comm[2] = 0.0f;
        break;

    default:
        mc_info.mc_state = MC_ERR;
        break;
    }
}

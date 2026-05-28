#include "bsp.h"
#include "FOC_CURRENT.h"
#include "stdio.h"
#include "tim.h"

/**
 * @file bsp_motor.c
 * @brief 电机控制状态机和 FOC 调度。
 *
 * 本模块负责把上层命令真正落到电机运行流程中：
 *   1. MotorControl_UpdateCommand() 从 comm[] 取启停和转速命令；
 *   2. MotorControl_StateMachineStep() 根据 mc_info.mc_state 执行状态机；
 *   3. MC_RUN 状态下调用 FOC_CURRENT_step() 并更新 TIM1 占空比。
 */

/**
 * @brief 打印状态机关键跳转信息。
 * @param message 跳转标签，便于串口日志定位状态变化。
 */
static void McDebug_PrintTransition(const char *message)
{
    printf("%s: cmd=%d rpm=%d state=%d err=%d vbus=%d\r\n",
           message,
           mc_info.cmd,
           (int)mc_info.refRPM,
           mc_info.mc_state,
           mc_info.mc_err,
           (int)mc_info.vbus);
}

/**
 * @brief 将 comm[] 共享命令转换为 mc_info 中的状态机输入。
 *
 * comm[] 是按键和串口共同写入的控制缓冲区：
 *   comm[0]：参考 q 轴电流，目前速度控制模式下固定清零；
 *   comm[1]：参考转速 RPM；
 *   comm[2]：启停命令，非 0 为 START，0 为 STOP。
 */
void MotorControl_UpdateCommand(void)
{
    comm[0] = 0;
    mc_info.cmd = comm[2] ? START_CMD : STOP_CMD;
    mc_info.refIq = comm[0];
    mc_info.refRPM = comm[1];
}

/**
 * @brief 执行一次电机状态机。
 *
 * 该函数每个 ADC 控制周期调用一次。状态流转：
 *   MC_RDY/MC_STOP 收到 START -> 初始化 FOC -> MC_START
 *   MC_START       打开门极/PWM -> MC_RUN
 *   MC_RUN         执行 FOC，响应 STOP 和故障
 *   MC_ERR         保持 PWM/门极关闭，等待外部复位
 */
void MotorControl_StateMachineStep(void)
{
    switch (mc_info.mc_state)
    {
    case MC_RDY:
    case MC_STOP:
        /* 就绪/停止状态只响应启动命令，没有启动命令时持续关断 PWM。 */
        if (mc_info.cmd == START_CMD)
        {
            McDebug_PrintTransition("CMD_START");
            /* 重新初始化自动生成的 FOC 模型，清除上一轮运行状态。 */
            FOC_CURRENT_initialize();
            ParaInit();
            mc_info.mc_state = MC_START;
        }
        else
        {
            StopPWM();
            EN_GATE_RESET;
        }
        break;

    case MC_START:
        /* 先使能门极驱动，再给一个短延时，最后启动三相 PWM。 */
        EN_GATE_SET;
        delay_nop(500);
        mc_info.mc_state = MC_RUN;
        StartPWM();
        McDebug_PrintTransition("MC_RUN");
        break;

    case MC_RUN:
        /*
         * 将 BSP 采样值写入 Simulink 生成的 FOC_CURRENT 输入结构体。
         * Ref_Id 当前固定为 0，转速参考写入 ObserverParam.RefRPM。
         */
        FOC_CURRENT_U.ISensA = mc_info.isens_a;
        FOC_CURRENT_U.ISensB = mc_info.isens_b;
        FOC_CURRENT_U.ISensC = mc_info.isens_c;
        FOC_CURRENT_U.VBus = mc_info.vbus;
        ObserverParam.RefRPM = mc_info.refRPM;
        FOC_CURRENT_U.Ref_Id = 0;

        /* 执行一次 FOC 控制，输出 tAout/tBout/tCout 三相比较值。 */
        FOC_CURRENT_step();
        /* 更新 TIM1 三相 PWM 占空比。 */
        __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_1, FOC_CURRENT_Y.tAout);
        __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_2, FOC_CURRENT_Y.tBout);
        __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_3, FOC_CURRENT_Y.tCout);

        /* 运行期保护放在 FOC 后检查，故障会把状态切到 MC_ERR。 */
        MotorProtection_CheckRunFaults();

        /* 收到停止命令时关闭输出，并重新初始化 FOC，为下次启动做准备。 */
        if (mc_info.cmd == STOP_CMD)
        {
            mc_info.mc_state = MC_STOP;
            StopPWM();
            EN_GATE_RESET;
            delay_nop(500);
            FOC_CURRENT_initialize();
        }

        /* 任何模块设置 mc_err 后，都统一进入故障状态。 */
        if (mc_info.mc_err != NONE_ERR)
        {
            mc_info.mc_state = MC_ERR;
        }
        break;

    case MC_ERR:
        /* 故障状态只做安全关断，故障清除由按键/串口命令模块处理。 */
        StopPWM();
        EN_GATE_RESET;
        break;

    default:
        break;
    }
}

/**
 * @brief 初始化电机控制参数。
 *
 * 当前主要清除参考电流、参考转速和故障状态，并熄灭故障灯。
 * 注意：该函数会把状态置为 MC_RDY，调用后状态机需要再走一次 MC_START。
 */
void ParaInit(void)
{
    mc_info.refIq = 0.0f;
    mc_info.refRPM = 0.0f;
    mc_info.mc_state = MC_RDY;
    mc_info.mc_err = NONE_ERR;
    StatusLed_AllOff();
    printf("Motor Init OK!\r\n");
}

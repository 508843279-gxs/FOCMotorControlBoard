#include "bsp.h"

/**
 * @file bsp_protection.c
 * @brief 电机运行保护逻辑。
 *
 * 保护分为两类：
 *   - 运行期保护：过流、过温，只在 MC_RUN 中由状态机调用；
 *   - 母线保护：欠压、过压，每个 ADC 控制周期都会检查。
 *
 * 故障触发后会写 mc_info.mc_err/mc_state，并通过 StatusLed_Set()
 * 点亮对应故障灯。故障复位由按键或串口 RUN/START 逻辑完成。
 */

/**
 * @brief 检查运行状态下的过流和过温故障。
 *
 * 过流采用计数器去抖，避免单个 ADC 毛刺直接触发故障。
 * 过温使用 SafeTemp，也就是温度模块平均滤波后的值。
 */
void MotorProtection_CheckRunFaults(void)
{
    /* 三相任一相超过 +/-20A 就累加过流计数。 */
    if (mc_info.isens_a > 20 || mc_info.isens_a < -20)
    {
        mc_info.over_cur_count++;
    }
    if (mc_info.isens_b > 20 || mc_info.isens_b < -20)
    {
        mc_info.over_cur_count++;
    }
    if (mc_info.isens_c > 20 || mc_info.isens_c < -20)
    {
        mc_info.over_cur_count++;
    }

    /* 连续多次超限后锁定过流故障。 */
    if (mc_info.over_cur_count > 4)
    {
        mc_info.over_cur_count = 60;
        mc_info.mc_state = MC_ERR;
        mc_info.mc_err = OC_ERR;
        StatusLed_Set(OC_ERR, 1);
    }

    /* SafeTemp 处于有效高温区间时触发过温保护。 */
    if (SafeTemp >= 80 && SafeTemp <= 200)
    {
        mc_info.mc_state = MC_ERR;
        mc_info.mc_err = OT_ERR;
        StatusLed_Set(OT_ERR, 1);
    }
}

/**
 * @brief 检查母线欠压/过压。
 *
 * 欠压立即进入故障；过压使用 over_vol_count 做简单去抖。
 * 注意：当前代码保持原工程判断顺序，vbus > 30V 的分支位于 vbus > 20V
 * 之后，因此正常情况下不会进入。若后续要区分严重过压，应把 30V 判断
 * 放到 20V 判断之前。
 */
void MotorProtection_CheckVbus(void)
{
    /* 欠压保护：母线低于 4V 直接锁定 LV_ERR。 */
    if (mc_info.vbus < 4.0f)
    {
        mc_info.mc_err = LV_ERR;
        mc_info.mc_state = MC_ERR;
        StatusLed_Set(LV_ERR, 1);
    }
    /* 过压保护：连续多次超过 20V 后锁定 OV_ERR。 */
    else if (mc_info.vbus > 20.0f)
    {
        if (mc_info.over_vol_count++ > 5)
        {
            mc_info.mc_err = OV_ERR;
            mc_info.err_state_vbus = mc_info.vbus;
            StatusLed_Set(OV_ERR, 1);
        }
    }
    /* 保留原工程严重过压分支。当前由于上一分支存在，该分支不可达。 */
    else if (mc_info.vbus > 30.0f)
    {
        mc_info.mc_state = MC_ERR;
    }
    else
    {
        /* 电压恢复到正常范围后，清除过压计数。 */
        mc_info.over_vol_count = 0;
    }
}

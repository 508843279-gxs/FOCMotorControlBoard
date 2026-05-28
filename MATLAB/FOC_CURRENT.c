/**
 * @file FOC_CURRENT.c
 * @brief FOC 电流环、速度外环、SVPWM 输出和无感观测器更新。
 *
 * 该文件由 MATLAB/Simulink 模型生成后接入当前 STM32 工程。上层代码在
 * ADC 注入转换回调中写入 FOC_CURRENT_U 的三相电流、母线电压、d 轴给定，
 * 并通过 ObserverParam.RefRPM 写入目标转速；随后调用 FOC_CURRENT_step()。
 * step() 会完成 Clarke/Park 变换、速度环生成 RefIq、电流 PI、反 Park、SVPWM，
 * 最后把 TIM1 三相比较值输出到 FOC_CURRENT_Y.tAout/tBout/tCout。
 */
#include "FOC_CURRENT.h"
#include "FOC_CURRENT_private.h"
/**
 * @brief FOC 模型使用的电机和观测器控制参数。
 *
 * 字段顺序见 FOC_CURRENT.h 中的 MotorParam_type：
 *   LADRC_Omega_c : 速度 LADRC/ESO 相关带宽参数。
 *   LADRC_b0      : 速度环控制对象增益，用于把速度误差换算为 q 轴电流给定。
 *   RsObserver    : 无感观测器里的等效电阻参数。
 *   pll_omega     : PLL 角速度环节参数。
 *   pll_xi        : PLL 阻尼/比例参数。
 */
MotorParam_type MotorParam = {
    5.0F,
    8000.0F,
    0.05F,
    8000.0F,
    140.0F};

    
/** @brief 无感观测器和速度环对外可见的运行参数，RefRPM 由上层控制代码写入。 */
ObserverParam_type ObserverParam;
/** @brief FOC 模型内部状态，包括积分器、单位延迟和电流环 PI 积分状态。 */
DW_FOC_CURRENT_T FOC_CURRENT_DW;
/** @brief FOC 模型输入：三相采样电流、母线电压和 d 轴电流给定。 */
ExtU_FOC_CURRENT_T FOC_CURRENT_U;
/** @brief FOC 模型输出：写入 TIM1 CH1/CH2/CH3 的三相 PWM 比较值。 */
ExtY_FOC_CURRENT_T FOC_CURRENT_Y;
/** @brief Simulink 生成代码保留的实时模型对象，当前主要用于保存 errorStatus。 */
static RT_MODEL_FOC_CURRENT_T FOC_CURRENT_M_;
/** @brief 实时模型对象指针，供 rtmSetErrorStatus/rtmGetErrorStatus 宏访问。 */
RT_MODEL_FOC_CURRENT_T *const FOC_CURRENT_M = &FOC_CURRENT_M_;

/**
 * @brief dq 轴电流 PI 控制器。
 *
 * 输入为 d/q 轴电流给定和实际电流，输出为 d/q 轴电压给定。
 * 函数内部带输出限幅和抗积分饱和逻辑：当电压已经达到限幅且积分项继续
 * 推向饱和方向时，会暂停该方向的积分更新。
 *
 * @param rtu_RefId d 轴电流给定，通常为 0，用于弱磁或磁链控制时可调整。
 * @param rtu_ISensD Park 变换得到的 d 轴实际电流。
 * @param rtu_RefIq 速度环输出的 q 轴电流给定，决定主要电磁转矩。
 * @param rtu_ISensQ Park 变换得到的 q 轴实际电流。
 * @param rty_PIOutputVd 输出 d 轴电压给定。
 * @param rty_PIOutputVq 输出 q 轴电压给定。
 * @param localDW 电流环积分器状态。
 */
void FOC_CURRENT_CTRL_CUR(real32_T rtu_RefId, real32_T rtu_ISensD, real32_T rtu_RefIq, real32_T rtu_ISensQ, real32_T *rty_PIOutputVd, real32_T *rty_PIOutputVq, DW_CTRL_CUR_FOC_CURRENT_T *localDW)
{
    /* 以下 rtb_* 是 Simulink 自动生成的临时变量，名称不代表固定物理量。 */
    real32_T rtb_IProdOut_l;
    real32_T rtb_IProdOut_p;
    real32_T rtb_Integrator_f2;
    real32_T rtb_Switch;
    /* d 轴 PI：误差 = 给定 Id - 实际 Id。 */
    *rty_PIOutputVd = rtu_RefId - rtu_ISensD;
    *rty_PIOutputVq = *rty_PIOutputVd * 75.3982239F;
    *rty_PIOutputVd = *rty_PIOutputVd * 0.0628318563F +
                      localDW->Integrator_DSTATE;
    /* d 轴电压输出限幅为 [-5, 5]，同时计算超出限幅的量用于抗积分饱和。 */
    if (*rty_PIOutputVd > 5.0F)
    {
        rtb_IProdOut_l = *rty_PIOutputVd - 5.0F;
    }
    else if (*rty_PIOutputVd >= -5.0F)
    {
        rtb_IProdOut_l = 0.0F;
    }
    else
    {
        rtb_IProdOut_l = *rty_PIOutputVd - -5.0F;
    }
    if (rtb_IProdOut_l < 0.0F)
    {
        rtb_IProdOut_p = -1.0F;
    }
    else if (rtb_IProdOut_l > 0.0F)
    {
        rtb_IProdOut_p = 1.0F;
    }
    else
    {
        rtb_IProdOut_p = rtb_IProdOut_l;
    }
    rtb_IProdOut_p = fmodf(rtb_IProdOut_p, 256.0F);
    if (*rty_PIOutputVq < 0.0F)
    {
        rtb_Integrator_f2 = -1.0F;
    }
    else if (*rty_PIOutputVq > 0.0F)
    {
        rtb_Integrator_f2 = 1.0F;
    }
    else
    {
        rtb_Integrator_f2 = *rty_PIOutputVq;
    }
    rtb_Switch = fmodf(rtb_Integrator_f2, 256.0F);
    /* 如果 d 轴输出已饱和，且积分方向会让饱和更严重，则本周期不继续积分。 */
    if ((0.0F != rtb_IProdOut_l) && ((rtb_IProdOut_p < 0.0F ? (int32_T)(int8_T) - (int8_T)(uint8_T)-rtb_IProdOut_p : (int32_T)(int8_T)(uint8_T)rtb_IProdOut_p) == (rtb_Switch < 0.0F ? (int32_T)(int8_T) - (int8_T)(uint8_T)-rtb_Switch : (int32_T)(int8_T)(uint8_T)rtb_Switch)))
    {
        rtb_Switch = 0.0F;
    }
    else
    {
        rtb_Switch = *rty_PIOutputVq;
    }
    /* 将 d 轴 PI 输出真正限幅后返回给后续反 Park 变换。 */
    if (*rty_PIOutputVd > 5.0F)
    {
        *rty_PIOutputVd = 5.0F;
    }
    else if (*rty_PIOutputVd < -5.0F)
    {
        *rty_PIOutputVd = -5.0F;
    }
    /* q 轴 PI：误差 = 给定 Iq - 实际 Iq。Iq 主要对应转矩输出。 */
    rtb_Integrator_f2 = rtu_RefIq - rtu_ISensQ;
    rtb_IProdOut_l = rtb_Integrator_f2 * 75.3982239F;
    if (rtb_IProdOut_l < 0.0F)
    {
        *rty_PIOutputVq = -1.0F;
    }
    else if (rtb_IProdOut_l > 0.0F)
    {
        *rty_PIOutputVq = 1.0F;
    }
    else
    {
        *rty_PIOutputVq = rtb_IProdOut_l;
    }
    rtb_IProdOut_p = fmodf(floorf(*rty_PIOutputVq), 256.0F);
    *rty_PIOutputVq = rtb_Integrator_f2 * 0.0628318563F +
                      localDW->Integrator_DSTATE_h;
    /* 计算 q 轴输出超过 [-5, 5] 的量，用于后面的抗积分饱和判断。 */
    if (*rty_PIOutputVq > 5.0F)
    {
        rtb_Integrator_f2 = *rty_PIOutputVq - 5.0F;
    }
    else if (*rty_PIOutputVq >= -5.0F)
    {
        rtb_Integrator_f2 = 0.0F;
    }
    else
    {
        rtb_Integrator_f2 = *rty_PIOutputVq - -5.0F;
    }
    /* 将 q 轴 PI 输出真正限幅后返回给后续反 Park 变换。 */
    if (*rty_PIOutputVq > 5.0F)
    {
        *rty_PIOutputVq = 5.0F;
    }
    else if (*rty_PIOutputVq < -5.0F)
    {
        *rty_PIOutputVq = -5.0F;
    }
    /* 采样周期为 0.0001s，更新 d 轴积分器。 */
    localDW->Integrator_DSTATE += 0.0001F * rtb_Switch;
    if (rtb_Integrator_f2 < 0.0F)
    {
        rtb_Switch = -1.0F;
    }
    else if (rtb_Integrator_f2 > 0.0F)
    {
        rtb_Switch = 1.0F;
    }
    else
    {
        rtb_Switch = rtb_Integrator_f2;
    }
    rtb_Switch = fmodf(rtb_Switch, 256.0F);
    /* q 轴抗积分饱和判断，逻辑与 d 轴一致。 */
    if ((0.0F != rtb_Integrator_f2) && ((rtb_Switch < 0.0F ? (int32_T)(int8_T) - (int8_T)(uint8_T)-rtb_Switch : (int32_T)(int8_T)(uint8_T)rtb_Switch) == (int8_T)(rtb_IProdOut_p < 0.0F ? (int32_T)(int8_T) - (int8_T)(uint8_T)-rtb_IProdOut_p : (int32_T)(int8_T)(uint8_T)rtb_IProdOut_p)))
    {
        rtb_IProdOut_l = 0.0F;
    }
    /* 更新 q 轴积分器。 */
    localDW->Integrator_DSTATE_h += 0.0001F * rtb_IProdOut_l;
}

/**
 * @brief 反 Park 变换：把旋转 dq 坐标系电压变换到静止 alpha/beta 坐标系。
 *
 * @param rtu_d d 轴电压给定。
 * @param rtu_q q 轴电压给定。
 * @param rtu_SinCos 当前观测电角度的 sin/cos，索引 0 为 sin，索引 1 为 cos。
 * @param rty_alpha 输出 alpha 轴电压。
 * @param rty_beta 输出 beta 轴电压。
 */
void FOC_CURRENT_InvPark(real32_T rtu_d, real32_T rtu_q, const real32_T rtu_SinCos[2], real32_T *rty_alpha, real32_T *rty_beta)
{
    *rty_alpha = rtu_d * rtu_SinCos[1];
    *rty_alpha -= rtu_q * rtu_SinCos[0];
    *rty_beta = rtu_q * rtu_SinCos[1];
    *rty_beta += rtu_d * rtu_SinCos[0];
}

/**
 * @brief Park 变换：把静止 alpha/beta 坐标系电流变换到随转子旋转的 dq 坐标系。
 *
 * @param rtu_Alpha alpha 轴电流。
 * @param rtu_Beta beta 轴电流。
 * @param rtu_SinCos 当前观测电角度的 sin/cos，索引 0 为 sin，索引 1 为 cos。
 * @param rty_D 输出 d 轴电流。
 * @param rty_Q 输出 q 轴电流。
 */
void FOC_CURRENT_Park(real32_T rtu_Alpha, real32_T rtu_Beta, const real32_T rtu_SinCos[2], real32_T *rty_D, real32_T *rty_Q)
{
    *rty_D = rtu_Alpha * rtu_SinCos[1];
    *rty_Q = rtu_Beta * rtu_SinCos[0];
    *rty_D += *rty_Q;
    *rty_Q = rtu_Alpha * rtu_SinCos[0];
    *rty_Q = rtu_Beta * rtu_SinCos[1] - *rty_Q;
}

/**
 * @brief SVPWM 计算：把 alpha/beta 电压指令换算成 TIM1 三相比较值。
 *
 * 该函数先根据空间矢量零序注入得到三相相电压指令，再根据母线电压归一化，
 * 最后乘以 4199。4199 对应当前 PWM 定时器周期/计数范围，因此输出不是百分比，
 * 而是后续直接写入 __HAL_TIM_SetCompare() 的比较值。
 *
 * @param rtu_Valpha alpha 轴电压指令。
 * @param rtu_Vbeta beta 轴电压指令。
 * @param rtu_v_bus 当前母线电压，来自 ADC 采样换算。
 * @param rty_tABC 输出 A/B/C 三相 PWM 比较值。
 */
void FOC_CURRENT_SVPWM2(real32_T rtu_Valpha, real32_T rtu_Vbeta, real32_T rtu_v_bus, real32_T rty_tABC[3])
{
    real32_T rtb_Min;
    real32_T rtb_Sum1_p;
    real32_T rtb_Sum_b;
    /* 由 alpha/beta 电压反算三相静止坐标下的相电压分量。 */
    rtb_Min = -0.5F * rtu_Valpha;
    rtb_Sum1_p = 0.866025388F * rtu_Vbeta;
    rtb_Sum_b = rtb_Min + rtb_Sum1_p;
    rtb_Sum1_p = rtb_Min - rtb_Sum1_p;
    /* 零序注入：取三相最大值和最小值的中点作为偏置，提高直流母线利用率。 */
    rtb_Min = (fminf(fminf(rtu_Valpha, rtb_Sum_b), rtb_Sum1_p) + fmaxf(fmaxf(rtu_Valpha, rtb_Sum_b), rtb_Sum1_p)) * -0.5F;
    rty_tABC[0] = rtb_Min + rtu_Valpha;
    rty_tABC[1] = rtb_Min + rtb_Sum_b;
    rty_tABC[2] = rtb_Min + rtb_Sum1_p;
    /* 归一化到中心对齐 PWM 的比较值范围：0.5 为中点，正负电压围绕中点调制。 */
    rty_tABC[0] = (-rty_tABC[0] / rtu_v_bus + 0.5F) * 4199.0F;
    rty_tABC[1] = (-rty_tABC[1] / rtu_v_bus + 0.5F) * 4199.0F;
    rty_tABC[2] = (-rty_tABC[2] / rtu_v_bus + 0.5F) * 4199.0F;
}

/**
 * @brief 执行一个 FOC 控制周期。
 *
 * 调用场景：Int_bsp.c 的 ADC 注入转换完成回调中，采样电流/电压后调用本函数。
 * 输入来自 FOC_CURRENT_U 和 ObserverParam.RefRPM，输出为 FOC_CURRENT_Y 三相 PWM 比较值。
 * 主要流程：
 * 1. 使用上一周期观测电角度计算 sin/cos。
 * 2. 三相电流经 Clarke/Park 变换得到 Id/Iq。
 * 3. 速度外环根据 RefRPM 和观测转速生成 RefIq，并限幅到 +/-70A。
 * 4. d/q 电流 PI 输出 Vd/Vq。
 * 5. 反 Park + SVPWM 得到三相 PWM 比较值。
 * 6. 更新无感观测器、PLL 和速度 ESO/LADRC 内部状态。
 */
void FOC_CURRENT_step(void)
{
    /* Simulink 生成的局部变量会在不同阶段复用，注释按当前代码段解释其用途。 */
    real32_T rtb_PWM_HalfPeriod[3];
    real32_T rtb_TmpSignalConversionAtInvP_0[2];
    real32_T rtb_Gain2_l;
    real32_T rtb_Gain_l;
    real32_T rtb_Gain_p;
    real32_T rtb_Integrator_m;
    real32_T rtb_Product2;
    real32_T rtb_Product3;
    real32_T rtb_Sum1_i;
    real32_T rtb_Sum_g;
    real32_T rtb_Sum_i4;
    real32_T rtb_Sum_m;
    real32_T rtb_UnitDelay;
    /* 取上一周期估算出的电角度，作为本周期坐标变换角度。 */
    ObserverParam.ObserverTheta = FOC_CURRENT_DW.UnitDelay_DSTATE;
    /* 计算 sin(theta)、cos(theta)，供 Park 和反 Park 共同使用。 */
    rtb_Sum_m = sinf(ObserverParam.ObserverTheta);
    rtb_Integrator_m = cosf(ObserverParam.ObserverTheta);
    rtb_TmpSignalConversionAtInvP_0[0] = rtb_Sum_m;
    rtb_TmpSignalConversionAtInvP_0[1] = rtb_Integrator_m;
    /* Clarke 变换：三相电流 Ia/Ib/Ic -> 静止 alpha/beta 电流。 */
    rtb_Gain_l = (FOC_CURRENT_U.ISensA - (FOC_CURRENT_U.ISensB +
                                          FOC_CURRENT_U.ISensC) *
                                             0.5F) *
                 0.666666687F;
    rtb_Gain2_l = (FOC_CURRENT_U.ISensB - FOC_CURRENT_U.ISensC) * 0.577350259F;
    /* Park 变换：alpha/beta 电流 -> d/q 电流，d 轴用于磁链，q 轴用于转矩。 */
    FOC_CURRENT_Park(rtb_Gain_l, rtb_Gain2_l, rtb_TmpSignalConversionAtInvP_0,
                     &rtb_Sum_i4, &rtb_Gain_p);
    /* 速度外环：目标转速 RefRPM 与估算转速状态比较，生成 q 轴电流给定 RefIq。 */
    ObserverParam.RefIq = ((ObserverParam.RefRPM -
                            FOC_CURRENT_DW.DiscreteTimeIntegrator1_DSTATE) *
                               MotorParam.LADRC_Omega_c -
                           FOC_CURRENT_DW.DiscreteTimeIntegrator_DSTATE) /
                          MotorParam.LADRC_b0;
    /* q 轴电流限幅，限制最大转矩电流。 */
    if (ObserverParam.RefIq > 70.0F)
    {
        ObserverParam.RefIq = 70.0F;
    }
    else if (ObserverParam.RefIq < -70.0F)
    {
        ObserverParam.RefIq = -70.0F;
    }
    /* 电流内环：Id/Iq 误差经过 PI，得到 Vd/Vq 电压指令。 */
    FOC_CURRENT_CTRL_CUR(FOC_CURRENT_U.Ref_Id, rtb_Sum_i4, ObserverParam.RefIq,
                         rtb_Gain_p, &rtb_Sum1_i, &rtb_Sum_i4,
                         &FOC_CURRENT_DW.CTRL_CUR);
    /* 反 Park：Vd/Vq -> Valpha/Vbeta，准备进入 SVPWM。 */
    FOC_CURRENT_InvPark(rtb_Sum1_i, rtb_Sum_i4, rtb_TmpSignalConversionAtInvP_0,
                        &rtb_Sum_g, &rtb_Sum1_i);
    /* SVPWM：根据母线电压把 Valpha/Vbeta 转成三相定时器比较值。 */
    FOC_CURRENT_SVPWM2(rtb_Sum_g, rtb_Sum1_i, FOC_CURRENT_U.VBus,
                       rtb_PWM_HalfPeriod);
    /* 无感观测器状态更新：结合电压模型积分状态和采样电流估算内部 eta 状态。 */
    ObserverParam.eta_x1 = FOC_CURRENT_DW.Integrator_DSTATE - 5.0E-5F *
                                                                  rtb_Gain_l;
    ObserverParam.eta_x2 = FOC_CURRENT_DW.Integrator1_DSTATE - 5.0E-5F *
                                                                   rtb_Gain2_l;
    /* PLL 鉴相与滤波：由 eta 状态和当前角度计算角度误差，再得到估算角速度。 */
    rtb_Integrator_m = ObserverParam.eta_x2 * rtb_Integrator_m - rtb_Sum_m *
                                                                     ObserverParam.eta_x1;
    rtb_Sum_m = MotorParam.pll_xi * MotorParam.pll_omega * rtb_Integrator_m +
                FOC_CURRENT_DW.Integrator_DSTATE_l;
    /* 对估算角速度幅值做一阶低通滤波，后续用于自适应观测器参数。 */
    FOC_CURRENT_DW.UnitDelay_DSTATE_h = fabsf(rtb_Sum_m) * 0.004F + 0.996F *
                                                                        FOC_CURRENT_DW.UnitDelay_DSTATE_h;
    rtb_Sum_i4 = 0.005F * FOC_CURRENT_DW.UnitDelay_DSTATE_h;
    if (rtb_Sum_i4 > 1.0F)
    {
        rtb_Sum_i4 = 1.0F;
    }
    else if (rtb_Sum_i4 < 0.8F)
    {
        rtb_Sum_i4 = 0.8F;
    }
    /* 根据滤波后的速度幅值调整等效 RsObserver，改善不同转速下的观测器表现。 */
    rtb_UnitDelay = MotorParam.RsObserver * rtb_Sum_i4;
    rtb_Gain_p = (1.00000011E-6F - ObserverParam.eta_x1 * ObserverParam.eta_x1) - ObserverParam.eta_x2 * ObserverParam.eta_x2;
    rtb_Sum_i4 = 500000.0F * FOC_CURRENT_DW.UnitDelay_DSTATE_h;
    if (rtb_Sum_i4 > 1.4E+6F)
    {
        rtb_Sum_i4 = 1.4E+6F;
    }
    else if (rtb_Sum_i4 < 700000.0F)
    {
        rtb_Sum_i4 = 700000.0F;
    }
    rtb_Product2 = rtb_Gain_p * ObserverParam.eta_x1 * rtb_Sum_i4;
    rtb_Product3 = rtb_Gain_p * ObserverParam.eta_x2 * rtb_Sum_i4;
    /* 积分估算角速度得到新的电角度，0.0001s 为模型采样周期。 */
    FOC_CURRENT_DW.UnitDelay_DSTATE = 0.0001F * rtb_Sum_m +
                                      ObserverParam.ObserverTheta;
    /* 将电角度限制在 0~2π，避免角度无限累加。 */
    if (FOC_CURRENT_DW.UnitDelay_DSTATE > 6.28318548F)
    {
        FOC_CURRENT_DW.UnitDelay_DSTATE -= 6.28318548F;
    }
    else if (FOC_CURRENT_DW.UnitDelay_DSTATE < 0.0F)
    {
        FOC_CURRENT_DW.UnitDelay_DSTATE += 6.28318548F;
    }
    /* 将观测角速度按模型系数换算成 ObserverRPM，供速度外环和调试查看。 */
    ObserverParam.ObserverRPM = 1.36418521F * rtb_Sum_m;
    /* 速度 ESO/LADRC 状态更新：估算转速与内部转速状态的误差。 */
    rtb_Sum_i4 = ObserverParam.ObserverRPM -
                 FOC_CURRENT_DW.DiscreteTimeIntegrator1_DSTATE;
    rtb_Gain_p = 3.0F * MotorParam.LADRC_Omega_c;
    /* 更新速度观测状态，并限制在 +/-12000RPM 范围内。 */
    FOC_CURRENT_DW.DiscreteTimeIntegrator1_DSTATE += ((2.0F * rtb_Sum_i4 *
                                                           rtb_Gain_p +
                                                       FOC_CURRENT_DW.UnitDelay_DSTATE_e * MotorParam.LADRC_b0) +
                                                      FOC_CURRENT_DW.DiscreteTimeIntegrator_DSTATE) *
                                                     0.0001F;
    if (FOC_CURRENT_DW.DiscreteTimeIntegrator1_DSTATE >= 12000.0F)
    {
        FOC_CURRENT_DW.DiscreteTimeIntegrator1_DSTATE = 12000.0F;
    }
    else if (FOC_CURRENT_DW.DiscreteTimeIntegrator1_DSTATE <= -12000.0F)
    {
        FOC_CURRENT_DW.DiscreteTimeIntegrator1_DSTATE = -12000.0F;
    }
    /* 更新速度扰动状态、电压模型积分状态和 PLL 积分状态，供下一周期继续估算。 */
    FOC_CURRENT_DW.DiscreteTimeIntegrator_DSTATE += rtb_Gain_p * rtb_Gain_p *
                                                    rtb_Sum_i4 * 0.0001F;
    FOC_CURRENT_DW.Integrator_DSTATE += ((rtb_Sum_g - rtb_Gain_l * rtb_UnitDelay) + rtb_Product2) * 0.0001F;
    FOC_CURRENT_DW.Integrator1_DSTATE += ((rtb_Sum1_i - rtb_Gain2_l *
                                                            rtb_UnitDelay) +
                                          rtb_Product3) *
                                         0.0001F;
    FOC_CURRENT_DW.Integrator_DSTATE_l += MotorParam.pll_omega *
                                          MotorParam.pll_omega * rtb_Integrator_m * 0.0001F;
    /* 保存本周期 RefIq，作为下一周期速度 ESO 更新的输入延迟。 */
    FOC_CURRENT_DW.UnitDelay_DSTATE_e = ObserverParam.RefIq;
    /* 输出给上层：Int_bsp.c 会把这三个值写入 TIM1 的 CH1/CH2/CH3 比较寄存器。 */
    FOC_CURRENT_Y.tAout = rtb_PWM_HalfPeriod[0];
    FOC_CURRENT_Y.tBout = rtb_PWM_HalfPeriod[1];
    FOC_CURRENT_Y.tCout = rtb_PWM_HalfPeriod[2];
}

/**
 * @brief 初始化 FOC 模型状态。
 *
 * 上层在电机启动前会调用该函数，清空积分器和观测器状态，避免上一次运行的
 * 积分残留影响本次启动。注意：当前函数没有清零 ObserverParam.RefRPM，目标转速
 * 由上层控制逻辑在运行周期中持续写入。
 */
void FOC_CURRENT_initialize(void)
{
    /* 清除 Simulink 实时模型错误状态。 */
    rtmSetErrorStatus(FOC_CURRENT_M, (NULL));
    /* 清空观测器输出和内部状态。 */
    ObserverParam.ObserverTheta = 0.0F;
    ObserverParam.RefIq = 0.0F;
    ObserverParam.eta_x1 = 0.0F;
    ObserverParam.eta_x2 = 0.0F;
    ObserverParam.ObserverRPM = 0.0F;
    /* 清空全部离散状态、积分器状态和电流环 PI 积分状态。 */
    (void)memset((void *)&FOC_CURRENT_DW, 0,
                 sizeof(DW_FOC_CURRENT_T));
}

/**
 * @brief FOC 模型终止函数。
 *
 * 当前工程中没有动态资源需要释放，因此函数保持为空；保留该接口是为了兼容
 * Simulink 生成代码的标准结构。
 */
void FOC_CURRENT_terminate(void)
{
}

#ifndef RTW_HEADER_FOC_CURRENT_private_h_
#define RTW_HEADER_FOC_CURRENT_private_h_
#include "rtwtypes.h"
#include "FOC_CURRENT.h"
/**
 * @brief 声明外部函数 FOC_CURRENT_CTRL_CUR
 *
 * 此函数用于执行 FOC 电流控制中的电流环控制。
 * 根据给定的参考电流和实际检测电流，计算并输出 PI 控制器的输出电压，
 * 同时处理积分饱和和输出限幅等逻辑。
 *
 * @param rtu_RefId 参考 d 轴电流
 * @param rtu_ISensD 检测到的 d 轴电流
 * @param rtu_RefIq 参考 q 轴电流
 * @param rtu_ISensQ 检测到的 q 轴电流
 * @param rty_PIOutputVd 指向存储 PI 控制器 d 轴输出电压的指针
 * @param rty_PIOutputVq 指向存储 PI 控制器 q 轴输出电压的指针
 * @param localDW 指向局部工作状态结构体的指针
 */
extern void FOC_CURRENT_CTRL_CUR(real32_T rtu_RefId, real32_T rtu_ISensD,
                                 real32_T rtu_RefIq, real32_T rtu_ISensQ, real32_T *rty_PIOutputVd, real32_T *rty_PIOutputVq, DW_CTRL_CUR_FOC_CURRENT_T *localDW);

/**
 * @brief 声明外部函数 FOC_CURRENT_InvPark
 *
 * 此函数用于执行反派克变换，将 dq 坐标系下的电压转换为 alpha-beta 坐标系下的电压。
 *
 * @param rtu_d d 轴电压
 * @param rtu_q q 轴电压
 * @param rtu_SinCos 包含正弦和余弦值的数组，rtu_SinCos[0] 为正弦值，rtu_SinCos[1] 为余弦值
 * @param rty_alpha 指向存储 alpha 轴电压的指针
 * @param rty_beta 指向存储 beta 轴电压的指针
 */
extern void FOC_CURRENT_InvPark(real32_T rtu_d, real32_T rtu_q, const real32_T rtu_SinCos[2], real32_T *rty_alpha, real32_T *rty_beta);

/**
 * @brief 声明外部函数 FOC_CURRENT_Park
 *
 * 此函数用于执行派克变换，将 alpha-beta 坐标系下的电压转换为 dq 坐标系下的电压。
 *
 * @param rtu_Alpha alpha 轴电压
 * @param rtu_Beta beta 轴电压
 * @param rtu_SinCos 包含正弦和余弦值的数组，rtu_SinCos[0] 为正弦值，rtu_SinCos[1] 为余弦值
 * @param rty_D 指向存储 d 轴电压的指针
 * @param rty_Q 指向存储 q 轴电压的指针
 */
extern void FOC_CURRENT_Park(real32_T rtu_Alpha, real32_T rtu_Beta, const real32_T rtu_SinCos[2], real32_T *rty_D, real32_T *rty_Q);

/**
 * @brief 声明外部函数 FOC_CURRENT_SVPWM2
 *
 * 此函数用于执行 SVPWM（空间矢量脉宽调制）计算，
 * 根据 alpha-beta 坐标系下的电压和母线电压，计算三相 PWM 占空比。
 *
 * @param rtu_Valpha alpha 轴电压
 * @param rtu_Vbeta beta 轴电压
 * @param rtu_v_bus 母线电压
 * @param rty_tABC 指向存储三相 PWM 占空比的数组的指针
 */
extern void FOC_CURRENT_SVPWM2(real32_T rtu_Valpha, real32_T rtu_Vbeta, real32_T rtu_v_bus, real32_T rty_tABC[3]);

#endif

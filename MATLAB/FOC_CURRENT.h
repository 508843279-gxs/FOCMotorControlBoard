#ifndef RTW_HEADER_FOC_CURRENT_h_
#define RTW_HEADER_FOC_CURRENT_h_
#include <math.h>
#include <stddef.h>
#include <string.h>
#ifndef FOC_CURRENT_COMMON_INCLUDES_
#define FOC_CURRENT_COMMON_INCLUDES_
#include "rtwtypes.h"
#endif

#include "FOC_CURRENT_types.h"

#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm) ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val) ((rtm)->errorStatus = (val))
#endif

/**
 * @brief 定义 FOC 电流控制中电流环控制的工作状态结构体
 *
 * 该结构体存储电流环控制中积分器的状态变量。
 */
typedef struct
{
    real32_T Integrator_DSTATE;   // d 轴积分器的状态变量
    real32_T Integrator_DSTATE_h; // q 轴积分器的状态变量
} DW_CTRL_CUR_FOC_CURRENT_T;

/**
 * @brief 定义 FOC 电流控制模块的工作状态结构体
 *
 * 该结构体存储 FOC 电流控制模块运行过程中的各种状态变量，
 * 包括延迟状态、积分状态以及电流环控制的工作状态。
 */
typedef struct
{
    real32_T UnitDelay_DSTATE;               // 单位延迟模块的状态变量
    real32_T DiscreteTimeIntegrator1_DSTATE; // 第一个离散时间积分器的状态变量
    real32_T DiscreteTimeIntegrator_DSTATE;  // 离散时间积分器的状态变量
    real32_T Integrator_DSTATE;              // 积分器的状态变量
    real32_T Integrator1_DSTATE;             // 另一个积分器的状态变量
    real32_T Integrator_DSTATE_l;            // 积分器的状态变量（可能用于特定计算）
    real32_T UnitDelay_DSTATE_h;             // 单位延迟模块的另一个状态变量
    real32_T UnitDelay_DSTATE_e;             // 单位延迟模块的错误相关状态变量
    DW_CTRL_CUR_FOC_CURRENT_T CTRL_CUR;      // 电流环控制的工作状态结构体
} DW_FOC_CURRENT_T;

/**
 * @brief 定义 FOC 电流控制模块的外部输入结构体
 *
 * 该结构体存储 FOC 电流控制模块的外部输入信号，
 * 包括三相电流、母线电压、参考 d 轴电流和角度信息。
 */
typedef struct
{
    real32_T ISensA; // A 相电流检测值
    real32_T ISensB; // B 相电流检测值
    real32_T ISensC; // C 相电流检测值
    real32_T VBus;   // 母线电压
    real32_T Ref_Id; // 参考 d 轴电流
    real32_T theta;  // 角度信息
} ExtU_FOC_CURRENT_T;

/**
 * @brief 定义 FOC 电流控制模块的外部输出结构体
 *
 * 该结构体存储 FOC 电流控制模块的外部输出信号，
 * 即三相 PWM 占空比输出。
 */
typedef struct
{
    real32_T tAout; // A 相 PWM 占空比输出
    real32_T tBout; // B 相 PWM 占空比输出
    real32_T tCout; // C 相 PWM 占空比输出
} ExtY_FOC_CURRENT_T;

/**
 * @brief 定义电机参数结构体
 *
 * 该结构体存储电机的相关参数，用于 FOC 电流控制计算。
 */
typedef struct MotorParam_tag
{
    real32_T LADRC_Omega_c; // LADRC 控制器的带宽参数
    real32_T LADRC_b0;      // LADRC 控制器的增益参数
    real32_T RsObserver;    // 电阻观测器的参数
    real32_T pll_omega;     // PLL（锁相环）的角频率参数
    real32_T pll_xi;        // PLL 的阻尼比参数
} MotorParam_type;

/**
 * @brief 定义观测器参数结构体
 *
 * 该结构体存储观测器的相关参数和状态变量，
 * 用于估计电机的角度、转速等信息。
 */
typedef struct ObserverParam_tag
{
    real32_T ObserverTheta; // 观测器估计的角度
    real32_T RefIq;         // 参考 q 轴电流
    real32_T eta_x1;        // 观测器状态变量 1
    real32_T eta_x2;        // 观测器状态变量 2
    real32_T ObserverRPM;   // 观测器估计的转速
    real32_T RefRPM;        // 参考转速
} ObserverParam_type;

/**
 * @brief 定义 FOC 电流控制模块的实时模型结构体
 *
 * 该结构体存储 FOC 电流控制模块实时模型的错误状态信息。
 */
struct tag_RTM_FOC_CURRENT_T
{
    const char_T *volatile errorStatus; // 实时模型的错误状态指针
};

// 声明外部变量，这些变量在其他文件中定义
extern DW_FOC_CURRENT_T FOC_CURRENT_DW;  // FOC 电流控制模块的工作状态变量
extern ExtU_FOC_CURRENT_T FOC_CURRENT_U; // FOC 电流控制模块的外部输入变量
extern ExtY_FOC_CURRENT_T FOC_CURRENT_Y; // FOC 电流控制模块的外部输出变量

// 声明外部函数，这些函数在其他文件中实现
extern void FOC_CURRENT_initialize(void); // 初始化 FOC 电流控制模块的函数
extern void FOC_CURRENT_step(void);       // 执行 FOC 电流控制模块一个控制周期的函数
extern void FOC_CURRENT_terminate(void);  // 终止 FOC 电流控制模块的函数

extern MotorParam_type MotorParam;                  // 电机参数变量
extern ObserverParam_type ObserverParam;            // 观测器参数变量
extern RT_MODEL_FOC_CURRENT_T *const FOC_CURRENT_M; // 指向 FOC 电流控制模块实时模型的常量指针

#endif

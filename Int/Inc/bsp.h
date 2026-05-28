#ifndef __BSP_H__
#define __BSP_H__

#include "main.h"

/*
 * BSP 公共头文件
 *
 * 这里仅放跨模块共享的类型、宏、全局变量声明和函数原型。
 * 具体实现已经拆到 Int/Src/bsp_*.c 中：
 *   - bsp_adc.c         ADC 注入采样和物理量换算
 *   - bsp_temperature.c PT100 温度查表和平均滤波
 *   - bsp_command.c     按键/串口命令到 comm[] 的转换
 *   - bsp_motor.c       电机状态机和 FOC 调度
 *   - bsp_protection.c  过流、过温、欠压、过压保护
 *   - bsp_pwm.c         TIM1 三相 PWM 启停
 *   - bsp_status_led.c  故障指示灯控制
 *   - bsp_shared.c      全局运行状态变量定义
 */

/* 2π，保留原工程命名，供角度/电角度相关计算使用。 */
#define PI_2 6.2832f

/*
 * 门极驱动使能宏。
 * 目前原工程中这两个宏为空定义，保留接口是为了不影响状态机调用点。
 * 如果后续硬件需要控制 WAKE/EN_GATE 引脚，只需要在这里补 HAL_GPIO_WritePin。
 */
#define EN_GATE_SET
#define EN_GATE_RESET

/* 板载驱动 LED 控制宏。 */
#define LED_DRV_SET HAL_GPIO_WritePin(LED_DRV_GPIO_Port, LED_DRV_Pin, GPIO_PIN_SET)
#define LED_DRV_RESET HAL_GPIO_WritePin(LED_DRV_GPIO_Port, LED_DRV_Pin, GPIO_PIN_RESET)

/* 放电 MOS 控制宏，用于母线放电相关逻辑。 */
#define DISCHARGE_MOS_SET HAL_GPIO_WritePin(DISCHARGE_MOS_GPIO_Port, DISCHARGE_MOS_Pin, GPIO_PIN_SET)
#define DISCHARGE_MOS_RESET HAL_GPIO_WritePin(DISCHARGE_MOS_GPIO_Port, DISCHARGE_MOS_Pin, GPIO_PIN_RESET)

/* 电机故障类型。故障灯和保护逻辑都以该枚举作为统一入口。 */
typedef enum
{
    NONE_ERR, /* 无故障 */
    LV_ERR,   /* 欠压 */
    OV_ERR,   /* 过压 */
    OC_ERR,   /* 过流 */
    OT_ERR    /* 过温 */
} enMcErr;

/* 电机运行状态机。状态流转主要在 MotorControl_StateMachineStep() 中完成。 */
typedef enum
{
    MC_RDY,   /* 参数已准备，等待启动命令 */
    MC_START, /* 启动过渡状态：打开驱动并启动 PWM */
    MC_RUN,   /* 正常运行：执行 FOC_CURRENT_step() */
    MC_STOP,  /* 停止状态：关闭 PWM，等待再次启动 */
    MC_ERR    /* 故障锁定状态：保持安全关断 */
} enMcState;

/* 上层控制命令。comm[2] 会被转换为该枚举。 */
typedef enum
{
    START_CMD = 1,
    STOP_CMD = 0
} enMcCMD;

/*
 * 电机运行信息结构体。
 * ADC 采样、命令解析、状态机、保护模块都会读写这个结构体。
 * 将运行状态集中在 mc_info 中，可以避免各模块之间传一堆零散变量。
 */
typedef struct
{
    enMcCMD cmd;       /* 当前启停命令 */
    enMcState mc_state;/* 当前状态机状态 */
    enMcErr mc_err;    /* 当前故障类型 */
    float refFreq;     /* 预留：参考频率 */
    float refIq;       /* q 轴参考电流 */
    float refId;       /* d 轴参考电流 */
    float refRPM;      /* 参考转速，最终写入 ObserverParam.RefRPM */
    float refPos;      /* 预留：参考位置 */

    float isens_a;         /* A 相电流，单位 A */
    float isens_b;         /* B 相电流，单位 A */
    float isens_c;         /* C 相电流，由 -(A+B) 计算得到 */
    float vbus;            /* 母线电压，单位 V */
    float encoder_theta;   /* 预留：编码器角度 */
    uint16_t encoder_dir;  /* 预留：编码器方向 */
    int16_t encoder_count; /* 预留：编码器计数 */

    float run_realtime;    /* 预留：运行时间 */
    uint8_t discharge_on;  /* 预留：放电开关状态 */
    uint8_t err_code;      /* 预留：扩展错误码 */
    uint8_t over_vol_count;/* 过压连续计数，用于去抖 */
    uint8_t over_cur_count;/* 过流连续计数，用于去抖 */

    float err_state_vbus;  /* 记录进入过压故障时的母线电压 */
} stMcInfo;

/* 命令共享缓冲区：comm[0]=Iq，comm[1]=RPM，comm[2]=启停。 */
extern float comm[10];
/* 全局电机运行信息。 */
extern stMcInfo mc_info;
/* 温度平均滤波后的安全温度值。 */
extern float SafeTemp;

/* 以下计数变量保留原工程接口，部分逻辑当前未使用。 */
extern uint32_t forwardTimeCnt;
extern uint32_t backTimeCnt;
extern uint32_t positionTimeCnt;
extern uint32_t speedSelectTimeCnt;

/* 以下 I2C/测试变量保留原工程接口，方便后续恢复驱动芯片寄存器逻辑。 */
extern uint32_t Theta_obser;
extern uint8_t POWMNG_i2cReg1;
extern uint8_t LOGIC_i2cReg2;
extern uint8_t READY_i2cReg3;
extern uint8_t NFAULT_i2cReg4;
extern uint8_t STBY_i2cReg5;
extern uint8_t STATUS_i2cReg6;
extern uint8_t Rtest;
extern uint8_t Ntest;

/* 保留的 BSP 初始化接口，当前工程内暂未实现。 */
void BspCordicInit(void);
void BspInit(void);

/* ADC 和温度模块接口。 */
void BspAdcSample_Update(void);
void BspTemperature_Accumulate(uint16_t raw_adc);
void BspTemperature_FilterTick(void);
float CalculateTemperature(float fR);

/* 按键/串口命令与电机状态机接口。 */
void KeyControl_Update(void);
void MotorControl_HandleUartCommand(uint8_t *data, uint16_t size);
void MotorControl_UpdateCommand(void);
void MotorControl_StateMachineStep(void);

/* 保护模块接口。 */
void MotorProtection_CheckRunFaults(void);
void MotorProtection_CheckVbus(void);

/* PWM、参数初始化、状态灯和短延时接口。 */
void StartPWM(void);
void StopPWM(void);
void ParaInit(void);
void StatusLed_AllOff(void);
void StatusLed_Set(enMcErr err, uint8_t on);
void delay_nop(uint16_t cont);

#endif

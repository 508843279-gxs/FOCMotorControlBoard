#ifndef __BSP_H__
#define __BSP_H__

#include "main.h"

/*
 * 第四阶段 BSP 公共头文件。
 *
 * 本阶段在第三阶段 FOC 数据链路基础上，恢复原始工程里与安全试转相关的
 * delay、shared、status_led、temperature 模块。main.c 仍然只调用
 * BspInit()/BspTask()，具体硬件和控制细节放在各个 bsp_*.c 文件中。
 */

/* 2π，保留原工程命名，后续角度和电角度相关计算可以继续使用。 */
#define PI_2 6.2832f

/*
 * 门极驱动使能宏。
 * 原始工程中这两个宏为空定义，这里继续保留调用点。若硬件需要真实 EN_GATE
 * 或 WAKE 控制，只需要把下面宏改成 HAL_GPIO_WritePin() 即可。
 */
#define EN_GATE_SET
#define EN_GATE_RESET

/* 下面两个宏来自原始工程，当前阶段保留接口，等硬件引脚确认后再启用。 */
#define LED_DRV_SET
#define LED_DRV_RESET
#define DISCHARGE_MOS_SET
#define DISCHARGE_MOS_RESET

typedef enum
{
    NONE_ERR = 0, /* 无故障 */
    LV_ERR,       /* 欠压故障，母线电压过低 */
    OV_ERR,       /* 过压故障，母线电压过高 */
    OC_ERR,       /* 过流故障，三相电流超过阈值 */
    OT_ERR        /* 过温故障，温度超过阈值 */
} enMcErr;

typedef enum
{
    MC_RDY = 0,   /* 预留就绪状态 */
    MC_START,     /* 启动过渡状态：准备打开 PWM */
    MC_RUN,       /* 运行状态：PWM 已经输出 */
    MC_STOP,      /* 停止状态：PWM 关闭，等待启动命令 */
    MC_ERR        /* 故障状态：强制关闭 PWM */
} enMcState;

typedef enum
{
    STOP_CMD = 0, /* 停止命令 */
    START_CMD = 1 /* 启动命令 */
} enMcCMD;

typedef struct
{
    enMcCMD cmd;        /* 当前状态机输入命令 */
    enMcState mc_state; /* 当前电机状态 */
    enMcErr mc_err;     /* 当前故障类型 */

    float refFreq; /* 预留：参考频率 */
    float refRPM; /* 参考转速，来自按键或串口命令 */
    float refIq;  /* 预留：q 轴参考电流 */
    float refId;  /* 预留：d 轴参考电流 */
    float refPos; /* 预留：参考位置 */

    float isens_a;     /* A 相电流，单位 A */
    float isens_b;     /* B 相电流，单位 A */
    float isens_c;     /* C 相电流，由 -(A+B) 推算 */
    float vbus;        /* 母线电压，单位 V */
    float temperature; /* 电机或功率板温度，单位摄氏度 */
    float encoder_theta;   /* 预留：编码器角度 */
    uint16_t encoder_dir;  /* 预留：编码器方向 */
    int16_t encoder_count; /* 预留：编码器计数 */

    uint16_t adc_vbus_raw; /* 母线电压 ADC 原始值 */
    uint16_t adc_temp_raw; /* 温度 ADC 原始值 */
    uint16_t adc_ia_raw;   /* A 相电流 ADC 原始值 */
    uint16_t adc_ib_raw;   /* B 相电流 ADC 原始值 */

    uint8_t over_vol_count;  /* 过压连续计数，用于简单去抖 */
    uint8_t over_cur_count;  /* 过流连续计数，用于简单去抖 */
    uint8_t over_temp_count; /* 过温连续计数，用于简单去抖 */

    float err_state_vbus; /* 记录进入过压故障时的母线电压 */
    float run_realtime;   /* 预留：运行时间 */
    uint8_t discharge_on; /* 预留：放电开关状态 */
    uint8_t err_code;     /* 预留：扩展错误码 */
} stMcInfo;

/* 中断计数和串口接收缓冲区，主要用于调试确认外设链路是否正常。 */
extern volatile uint32_t adc_injected_count;
extern volatile uint32_t uart_rx_event_count;
extern uint8_t rxBuff[1000];

/* comm[] 是命令层和状态机之间的简单共享缓冲：
 * comm[0]：预留 q 轴电流；
 * comm[1]：目标转速 RPM；
 * comm[2]：启停命令，非 0 表示启动。
 */
extern float comm[10];
extern stMcInfo mc_info;
extern float SafeTemp;

/* 原始工程保留变量：第四阶段先恢复声明，后续接驱动芯片寄存器时继续使用。 */
extern uint32_t forwardTimeCnt;
extern uint32_t backTimeCnt;
extern uint32_t positionTimeCnt;
extern uint32_t speedSelectTimeCnt;
extern uint32_t Theta_obser;
extern uint8_t POWMNG_i2cReg1;
extern uint8_t LOGIC_i2cReg2;
extern uint8_t READY_i2cReg3;
extern uint8_t NFAULT_i2cReg4;
extern uint8_t STBY_i2cReg5;
extern uint8_t STATUS_i2cReg6;
extern uint8_t Rtest;
extern uint8_t Ntest;

/* 三个速度档位，命令模块根据档位选择目标转速。 */
extern uint16_t RPM1;
extern uint16_t RPM2;
extern uint16_t RPM3;

/* BSP 总入口：main.c 只需要调用这两个函数。 */
void BspInit(void);
void BspTask(void);

/* ADC 采样换算接口。 */
void BspAdcSample_Update(void);
void BspTemperature_Accumulate(uint16_t raw_adc);
void BspTemperature_FilterTick(void);
float CalculateTemperature(float fR);

/* 按键/串口命令接口。 */
void KeyControl_Update(void);
void MotorControl_HandleUartCommand(uint8_t *data, uint16_t size);

/* PWM 控制接口。 */
void StartPWM(void);
void StopPWM(void);
void BspPwm_StartAdcTrigger(void);
void BspPwm_SetComparePercent(uint8_t a_percent, uint8_t b_percent, uint8_t c_percent);

/* 保护和状态灯接口。 */
void MotorProtection_CheckRunFaults(void);
void MotorProtection_CheckVbus(void);
void StatusLed_AllOff(void);
void StatusLed_Set(enMcErr err, uint8_t on);

/* 电机状态机接口。 */
void MotorControl_UpdateCommand(void);
void MotorControl_StateMachineStep(void);
void MotorControl_FocControlStep(void);
void ParaInit(void);
void delay_nop(uint16_t cont);

#endif

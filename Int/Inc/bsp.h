#ifndef __BSP_H__
#define __BSP_H__

#include "main.h"

/* 第二阶段 BSP 分层公共头文件。
 * main.c 只调用 BspInit()/BspTask()，具体硬件细节拆到各个 bsp_*.c。
 */

typedef enum
{
    NONE_ERR = 0,
    LV_ERR,
    OV_ERR,
    OC_ERR,
    OT_ERR
} enMcErr;

typedef enum
{
    MC_RDY = 0,
    MC_START,
    MC_RUN,
    MC_STOP,
    MC_ERR
} enMcState;

typedef enum
{
    STOP_CMD = 0,
    START_CMD = 1
} enMcCMD;

typedef struct
{
    enMcCMD cmd;
    enMcState mc_state;
    enMcErr mc_err;

    float refRPM;
    float refIq;
    float refId;

    float isens_a;
    float isens_b;
    float isens_c;
    float vbus;
    float temperature;

    uint16_t adc_vbus_raw;
    uint16_t adc_temp_raw;
    uint16_t adc_ia_raw;
    uint16_t adc_ib_raw;

    uint8_t over_vol_count;
    uint8_t over_cur_count;
    uint8_t over_temp_count;

    float err_state_vbus;
} stMcInfo;

extern volatile uint32_t adc_injected_count;
extern volatile uint32_t uart_rx_event_count;
extern uint8_t rxBuff[1000];

extern float comm[10];
extern stMcInfo mc_info;

extern uint16_t RPM1;
extern uint16_t RPM2;
extern uint16_t RPM3;

void BspInit(void);
void BspTask(void);

void BspAdcSample_Update(void);

void KeyControl_Update(void);
void MotorControl_HandleUartCommand(uint8_t *data, uint16_t size);

void StartPWM(void);
void StopPWM(void);
void BspPwm_StartAdcTrigger(void);
void BspPwm_SetComparePercent(uint8_t a_percent, uint8_t b_percent, uint8_t c_percent);

void MotorProtection_CheckRunFaults(void);
void MotorProtection_CheckVbus(void);
void StatusLed_AllOff(void);
void StatusLed_Set(enMcErr err, uint8_t on);

void MotorControl_UpdateCommand(void);
void MotorControl_StateMachineStep(void);
void ParaInit(void);

#endif

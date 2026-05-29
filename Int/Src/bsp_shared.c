#include "bsp.h"

/**
 * @file bsp_shared.c
 * @brief BSP 层共享全局变量定义。
 *
 * 这里集中放置原 Int_bsp.c 中跨模块使用的全局变量。
 * 其他模块只通过 bsp.h 中的 extern 声明访问，避免每个模块各自定义
 * 导致重复符号。
 */

/*
 * 控制命令缓冲区：
 *   comm[0]：参考 q 轴电流，目前速度控制下固定为 0；
 *   comm[1]：参考转速 RPM，由按键/串口模块写入；
 *   comm[2]：启停命令，非 0 表示 START，0 表示 STOP。
 */
float comm[10];

/* 电机运行状态、采样值、故障计数等核心信息。 */
stMcInfo mc_info;

/* 原工程保留的按键/方向计时变量，目前主要由命令模块清零。 */
uint32_t forwardTimeCnt;
uint32_t backTimeCnt;
uint32_t positionTimeCnt;
uint32_t speedSelectTimeCnt;

/* 以下变量保留原工程的外设/调试接口，后续接回驱动芯片寄存器逻辑时可继续使用。 */
uint32_t Theta_obser;
uint8_t POWMNG_i2cReg1 = 0;
uint8_t LOGIC_i2cReg2 = 0;
uint8_t READY_i2cReg3 = 0;
uint8_t NFAULT_i2cReg4 = 0;
uint8_t STBY_i2cReg5 = 0;
uint8_t STATUS_i2cReg6 = 0;
uint8_t Rtest;
uint8_t Ntest;

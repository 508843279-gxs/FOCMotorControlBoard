#include "bsp.h"

/**
 * @file bsp_delay.c
 * @brief 极短软件延时。
 *
 * 该延时通过循环执行 CMSIS 的 __NOP() 指令实现，只适合门极使能后
 * 等待几个控制周期以内的短延时。需要毫秒级延时时请优先使用 HAL_Delay()
 * 或定时器，避免阻塞 ADC 控制回调。
 */

/**
 * @brief 执行指定次数的空操作指令。
 * @param cont __NOP() 执行次数，值越大延时越长。
 *
 * @note 该函数不是精确定时函数，实际时间会受 CPU 主频、编译优化等级影响。
 */
void delay_nop(uint16_t cont)
{
    uint16_t i;

    for (i = 0; i < cont; i++)
    {
        __NOP();
    }
}

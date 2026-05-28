#include "bsp.h"
#include "adc.h"

/**
 * @file bsp_adc.c
 * @brief ADC 注入通道采样与物理量换算。
 *
 * ADC1 注入序列当前包含 4 个通道：
 *   Rank1：母线电压 VBUS
 *   Rank2：PT100 温度采样
 *   Rank3：A 相电流
 *   Rank4：B 相电流
 *
 * 本模块只负责把 ADC 原始码值转换成 mc_info 中的工程量，保护判断和
 * 电机状态机不放在这里，方便后续单独标定采样系数。
 */

/**
 * @brief 读取一次 ADC1 注入转换结果，并更新电流、电压和温度累计值。
 *
 * @note 该函数在 ADC 注入转换完成回调中调用，不能做耗时操作。
 * @note 电流零点补偿值和比例系数来自原工程，硬件改版后需要重新标定。
 */
void BspAdcSample_Update(void)
{
    /* ADC 注入通道原始采样值，范围通常为 0~4095。 */
    uint16_t adc1_in1;
    uint16_t adc1_in2;
    uint16_t adc1_in3;
    uint16_t adc1_in4;

    /* 读取注入序列的 4 个 Rank。Rank 映射关系由 CubeMX 的 ADC 配置决定。 */
    adc1_in1 = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
    adc1_in2 = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
    adc1_in3 = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_3);
    adc1_in4 = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_4);

    /*
     * 电流换算：
     *   2048 是 12bit ADC 的理论中点，对应零电流；
     *   0.0403F 是 ADC 码值到电流的比例系数；
     *   +1.04F/+0.24F 是 A/B 两相静态零漂补偿。
     * C 相没有直接采样，由三相电流和为 0 推算。
     */
    mc_info.isens_a = (adc1_in3 - 2048) * 0.0403F + 1.04F;
    mc_info.isens_b = (adc1_in4 - 2048) * 0.0403F + 0.24F;
    mc_info.isens_c = -(mc_info.isens_a + mc_info.isens_b);

    /*
     * 母线电压换算：
     *   0.01853F 为原工程分压电路和 ADC 参考电压合成后的比例系数。
     *   输出单位为 V，后续欠压/过压保护直接读取 mc_info.vbus。
     */
    mc_info.vbus = adc1_in1 * 0.01853F;

    /* 温度采样交给温度模块累计，平均滤波在 BspTemperature_FilterTick() 中完成。 */
    BspTemperature_Accumulate(adc1_in2);
}

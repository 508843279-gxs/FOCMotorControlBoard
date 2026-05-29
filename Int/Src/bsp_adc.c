#include "bsp.h"
#include "adc.h"

/* 第四阶段 ADC 模块：
 * 负责读取注入通道原始值，并换算母线电压/三相电流。
 * 温度换算重新交给 bsp_temperature.c 的 PT100 查表和平均滤波模块。
 */

#define ADC_MID_CODE              2048.0f
#define ADC_TO_CURRENT_A          0.0403f
#define CURRENT_A_OFFSET          1.04f
#define CURRENT_B_OFFSET          0.24f
#define ADC_TO_VBUS_V             0.01853f

void BspAdcSample_Update(void)
{
    /* 读取 ADC 注入序列 4 个 Rank。
     * Rank 对应关系来自 CubeMX 的 ADC 配置，后续改 ADC 通道时这里也要同步确认。
     */
    mc_info.adc_vbus_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
    mc_info.adc_temp_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
    mc_info.adc_ia_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_3);
    mc_info.adc_ib_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_4);

    /* 电流换算沿用原工程标定值；低压试转前需要重新确认零偏。 */
    mc_info.isens_a = ((float)mc_info.adc_ia_raw - ADC_MID_CODE) * ADC_TO_CURRENT_A + CURRENT_A_OFFSET;
    mc_info.isens_b = ((float)mc_info.adc_ib_raw - ADC_MID_CODE) * ADC_TO_CURRENT_A + CURRENT_B_OFFSET;
    mc_info.isens_c = -(mc_info.isens_a + mc_info.isens_b);

    /* 母线电压比例来自原工程分压系数。 */
    mc_info.vbus = (float)mc_info.adc_vbus_raw * ADC_TO_VBUS_V;

    /* 温度恢复为原始 PT100 查表 + 10000 点平均滤波。 */
    BspTemperature_Accumulate(mc_info.adc_temp_raw);
    mc_info.temperature = SafeTemp;
}

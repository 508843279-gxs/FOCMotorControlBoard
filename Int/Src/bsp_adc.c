#include "bsp.h"
#include "adc.h"

/* 第二阶段 ADC 模块：
 * 只负责读取注入通道原始值，并换算成母线电压、电流、温度等工程量。
 */

#define ADC_MID_CODE              2048.0f
#define ADC_TO_CURRENT_A          0.0403f
#define CURRENT_A_OFFSET          1.04f
#define CURRENT_B_OFFSET          0.24f
#define ADC_TO_VBUS_V             0.01853f
#define ADC_REF_MV                3300.0f
#define ADC_FULL_SCALE            4096.0f
#define PT100_PULLUP_OHM          150.0f
#define PT100_R0_OHM              100.0f
#define PT100_ALPHA_OHM_PER_DEG   0.385f

static float BspAdc_ConvertPt100(uint16_t raw_adc)
{
    float temp_mv = ((float)raw_adc * ADC_REF_MV) / ADC_FULL_SCALE;
    float denominator = ADC_REF_MV - temp_mv;
    float pt100_ohm;

    if (denominator <= 1.0f)
    {
        return 200.0f;
    }

    pt100_ohm = (PT100_PULLUP_OHM * temp_mv) / denominator;
    return (pt100_ohm - PT100_R0_OHM) / PT100_ALPHA_OHM_PER_DEG;
}

void BspAdcSample_Update(void)
{
    mc_info.adc_vbus_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
    mc_info.adc_temp_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
    mc_info.adc_ia_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_3);
    mc_info.adc_ib_raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_4);

    /* 电流换算沿用原工程标定值；后续第三阶段再专门做零偏校准。 */
    mc_info.isens_a = ((float)mc_info.adc_ia_raw - ADC_MID_CODE) * ADC_TO_CURRENT_A + CURRENT_A_OFFSET;
    mc_info.isens_b = ((float)mc_info.adc_ib_raw - ADC_MID_CODE) * ADC_TO_CURRENT_A + CURRENT_B_OFFSET;
    mc_info.isens_c = -(mc_info.isens_a + mc_info.isens_b);

    /* 母线电压比例来自原工程分压系数。 */
    mc_info.vbus = (float)mc_info.adc_vbus_raw * ADC_TO_VBUS_V;

    /* 温度先用 PT100 线性近似，后续可以替换为查表或校准曲线。 */
    mc_info.temperature = BspAdc_ConvertPt100(mc_info.adc_temp_raw);
}

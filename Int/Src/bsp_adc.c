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

/* 将温度 ADC 原始值换算成 PT100 温度。
 * 当前使用线性近似：PT100 在 0 摄氏度约 100 欧，每升高 1 摄氏度约增加 0.385 欧。
 * 后续如果要提高精度，可以把这里替换成查表或标定曲线。
 */
static float BspAdc_ConvertPt100(uint16_t raw_adc)
{
    /* ADC 原始值先换算成分压点电压，单位 mV。 */
    float temp_mv = ((float)raw_adc * ADC_REF_MV) / ADC_FULL_SCALE;
    /* denominator 是分压公式里的上半部分电压，过小代表采样接近满量程。 */
    float denominator = ADC_REF_MV - temp_mv;
    float pt100_ohm;

    if (denominator <= 1.0f)
    {
        return 200.0f;
    }

    /* 根据分压公式反推 PT100 电阻值。 */
    pt100_ohm = (PT100_PULLUP_OHM * temp_mv) / denominator;
    /* 根据 PT100 线性近似公式换算温度。 */
    return (pt100_ohm - PT100_R0_OHM) / PT100_ALPHA_OHM_PER_DEG;
}

void BspAdcSample_Update(void)
{
    /* 读取 ADC 注入序列 4 个 Rank。
     * Rank 对应关系来自 CubeMX 的 ADC 配置，后续改 ADC 通道时这里也要同步确认。
     */
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

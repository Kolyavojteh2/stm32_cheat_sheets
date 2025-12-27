#include "ph_adc_hal.h"

static uint32_t ph_adc_hal_rank_first(void)
{
    /* STM32F0 usually uses ADC_RANK_CHANNEL_NUMBER */
#if defined(ADC_RANK_CHANNEL_NUMBER)
    return ADC_RANK_CHANNEL_NUMBER;
#endif

    /* STM32F4 uses ADC_REGULAR_RANK_1 */
#if defined(ADC_REGULAR_RANK_1)
    return ADC_REGULAR_RANK_1;
#endif

    /* Fallback for older/other HAL variants */
    return 1U;
}

HAL_StatusTypeDef ph_adc_hal_read(ADC_HandleTypeDef *hadc,
                                  uint32_t channel,
                                  uint32_t sampling_time,
                                  uint16_t *raw_out)
{
    ADC_ChannelConfTypeDef sConfig;

    if (hadc == NULL || raw_out == NULL) {
        return HAL_ERROR;
    }

    /* Configure mandatory fields (common for F0/F4) */
    sConfig.Channel = channel;
    sConfig.Rank = ph_adc_hal_rank_first();
    sConfig.SamplingTime = sampling_time;

    /* Configure optional fields (exist on some HAL versions / families) */
#if defined(ADC_OFFSET_NONE)
    sConfig.Offset = ADC_OFFSET_NONE;
#elif defined(ADC_CHANNELS_BANK_A)
    (void)0;
#else
    /* If your HAL adds other fields, set them here if compilation requires. */
#endif

    if (HAL_ADC_ConfigChannel(hadc, &sConfig) != HAL_OK) {
        return HAL_ERROR;
    }

    if (HAL_ADC_Start(hadc) != HAL_OK) {
        return HAL_ERROR;
    }

    if (HAL_ADC_PollForConversion(hadc, 10) != HAL_OK) {
        (void)HAL_ADC_Stop(hadc);
        return HAL_ERROR;
    }

    *raw_out = (uint16_t)HAL_ADC_GetValue(hadc);

    if (HAL_ADC_Stop(hadc) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

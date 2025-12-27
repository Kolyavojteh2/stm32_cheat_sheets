#include "tds_meter.h"
#include <math.h>

static float tds_meter_raw_to_voltage(const TDS_Meter_t *m, uint16_t raw)
{
    if (m == 0 || m->adc_range == 0U) {
        return 0.0f;
    }

    return ((float)raw * m->vref_v) / (float)m->adc_range;
}

static float tds_meter_calc_tds_from_voltage(const TDS_Meter_t *m, float voltage_v, float temperature_c, float calibration_factor)
{
    float coeff;
    float compensated_v;
    float tds;

    /* Temperature compensation coefficient:
       f(25C) = f(current) / (1 + 0.02*(T-25)) */
    coeff = 1.0f + (m->temp_comp_coeff * (temperature_c - 25.0f));
    if (coeff <= 0.0001f) {
        coeff = 1.0f;
    }

    compensated_v = voltage_v / coeff;

    /* Polynomial conversion used by common TDS V1.0 / SEN0244 examples */
    tds = (133.42f * compensated_v * compensated_v * compensated_v
         - 255.86f * compensated_v * compensated_v
         + 857.39f * compensated_v) * m->tds_factor;

    tds *= calibration_factor;

    if (tds < 0.0f) {
        tds = 0.0f;
    }

    return tds;
}

void tds_meter_init(TDS_Meter_t *m,
                    ADC_HandleTypeDef *hadc,
                    float vref_v,
                    uint32_t adc_range,
                    uint16_t *sample_buf,
                    uint16_t *work_buf,
                    uint16_t sample_count)
{
    uint16_t i;

    if (m == 0) {
        return;
    }

    m->hadc = hadc;
    m->vref_v = vref_v;
    m->adc_range = adc_range;

    m->sample_buf = sample_buf;
    m->work_buf = work_buf;
    m->sample_count = sample_count;
    m->sample_index = 0;

    m->temperature_c = 25.0f;
    m->temp_comp_coeff = 0.02f;
    m->tds_factor = 0.5f;
    m->calibration_factor = 1.0f;

    m->last_voltage_v = 0.0f;
    m->last_tds_ppm = 0.0f;
    m->last_valid = false;

    m->power_pin_used = false;
    m->power_pin_active_high = true;
    m->power_pin.port = 0;
    m->power_pin.pin = 0;

    /* Clear sample buffer for deterministic startup */
    if (m->sample_buf != 0) {
        for (i = 0; i < m->sample_count; i++) {
            m->sample_buf[i] = 0;
        }
    }
}

void tds_meter_set_power_pin(TDS_Meter_t *m, GPIO_t pin, bool active_high)
{
    if (m == 0) {
        return;
    }

    m->power_pin = pin;
    m->power_pin_used = (pin.port != 0);
    m->power_pin_active_high = active_high;
}

void tds_meter_power_on(TDS_Meter_t *m)
{
    if (m == 0 || !m->power_pin_used) {
        return;
    }

    /* WritePin expects GPIO_PIN_SET/RESET */
    HAL_GPIO_WritePin(m->power_pin.port,
                      m->power_pin.pin,
                      m->power_pin_active_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void tds_meter_power_off(TDS_Meter_t *m)
{
    if (m == 0 || !m->power_pin_used) {
        return;
    }

    HAL_GPIO_WritePin(m->power_pin.port,
                      m->power_pin.pin,
                      m->power_pin_active_high ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void tds_meter_set_temperature_c(TDS_Meter_t *m, float temperature_c)
{
    if (m == 0) {
        return;
    }

    m->temperature_c = temperature_c;
}

void tds_meter_set_temp_comp_coeff(TDS_Meter_t *m, float coeff_per_c)
{
    if (m == 0) {
        return;
    }

    m->temp_comp_coeff = coeff_per_c;
}

void tds_meter_set_tds_factor(TDS_Meter_t *m, float tds_factor)
{
    if (m == 0) {
        return;
    }

    m->tds_factor = tds_factor;
}

void tds_meter_set_calibration_factor(TDS_Meter_t *m, float factor)
{
    if (m == 0) {
        return;
    }

    if (factor <= 0.0f) {
        factor = 1.0f;
    }

    m->calibration_factor = factor;
}

HAL_StatusTypeDef tds_meter_sample(TDS_Meter_t *m, uint32_t timeout_ms)
{
    HAL_StatusTypeDef st;
    uint32_t raw;

    if (m == 0 || m->hadc == 0) {
        return HAL_ERROR;
    }

    st = HAL_ADC_Start(m->hadc);
    if (st != HAL_OK) {
        return st;
    }

    st = HAL_ADC_PollForConversion(m->hadc, timeout_ms);
    if (st != HAL_OK) {
        (void)HAL_ADC_Stop(m->hadc);
        return st;
    }

    raw = HAL_ADC_GetValue(m->hadc);

    st = HAL_ADC_Stop(m->hadc);
    if (st != HAL_OK) {
        return st;
    }

    tds_meter_push_raw(m, (uint16_t)raw);
    return HAL_OK;
}

void tds_meter_push_raw(TDS_Meter_t *m, uint16_t raw)
{
    if (m == 0 || m->sample_buf == 0 || m->sample_count == 0U) {
        return;
    }

    m->sample_buf[m->sample_index] = raw;

    m->sample_index++;
    if (m->sample_index >= m->sample_count) {
        m->sample_index = 0;
    }

    m->last_valid = false;
}

float tds_meter_get_voltage_v(TDS_Meter_t *m)
{
    uint16_t median_raw;
    float voltage;

    if (m == 0 || m->sample_buf == 0 || m->work_buf == 0 || m->sample_count == 0U) {
        return 0.0f;
    }

    median_raw = tds_filter_median_u16(m->sample_buf, m->sample_count, m->work_buf);
    voltage = tds_meter_raw_to_voltage(m, median_raw);

    m->last_voltage_v = voltage;
    return voltage;
}

float tds_meter_get_tds_ppm(TDS_Meter_t *m)
{
    float voltage;
    float tds;

    if (m == 0) {
        return 0.0f;
    }

    voltage = tds_meter_get_voltage_v(m);
    tds = tds_meter_calc_tds_from_voltage(m, voltage, m->temperature_c, m->calibration_factor);

    m->last_tds_ppm = tds;
    m->last_valid = true;
    return tds;
}

bool tds_meter_calibrate_at_25c(TDS_Meter_t *m, float known_tds_ppm)
{
    float voltage;
    float measured;
    float factor;

    if (m == 0 || known_tds_ppm <= 0.0f) {
        return false;
    }

    /* Use current filtered voltage and compute TDS with calibration = 1.0 */
    voltage = tds_meter_get_voltage_v(m);
    measured = tds_meter_calc_tds_from_voltage(m, voltage, 25.0f, 1.0f);

    if (measured <= 0.0001f) {
        return false;
    }

    factor = known_tds_ppm / measured;
    if (factor <= 0.0f) {
        return false;
    }

    m->calibration_factor = factor;
    m->last_valid = false;
    return true;
}

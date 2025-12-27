#include "ph_sensor.h"

static uint32_t ph_sensor_get_adc_rank(void)
{
#if defined(ADC_RANK_CHANNEL_NUMBER)
    return ADC_RANK_CHANNEL_NUMBER;
#else
    return 1;
#endif
}

static PH_Sensor_Status_t ph_sensor_adc_read_channel(PH_Sensor_t *self,
                                                     uint32_t channel,
                                                     uint32_t sampling_time,
                                                     uint16_t *raw_out)
{
    ADC_ChannelConfTypeDef sConfig;

    if (self == NULL || self->hadc == NULL || raw_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    sConfig.Channel = channel;
    sConfig.Rank = ph_sensor_get_adc_rank();

#if defined(ADC_SAMPLETIME_1CYCLE_5) || defined(ADC_SAMPLETIME_7CYCLES_5)
    sConfig.SamplingTime = sampling_time;
#else
    /* If HAL variant differs, you must adjust this field for your MCU family. */
    sConfig.SamplingTime = sampling_time;
#endif

    if (HAL_ADC_ConfigChannel(self->hadc, &sConfig) != HAL_OK) {
        return PH_SENSOR_STATUS_HAL_ERROR;
    }

    if (HAL_ADC_Start(self->hadc) != HAL_OK) {
        return PH_SENSOR_STATUS_HAL_ERROR;
    }

    if (HAL_ADC_PollForConversion(self->hadc, 10) != HAL_OK) {
        (void)HAL_ADC_Stop(self->hadc);
        return PH_SENSOR_STATUS_HAL_ERROR;
    }

    *raw_out = (uint16_t)HAL_ADC_GetValue(self->hadc);

    if (HAL_ADC_Stop(self->hadc) != HAL_OK) {
        return PH_SENSOR_STATUS_HAL_ERROR;
    }

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_init(PH_Sensor_t *self,
                                  ADC_HandleTypeDef *hadc,
                                  uint32_t ph_adc_channel,
                                  uint32_t ph_adc_sampling_time,
                                  float vref,
                                  uint32_t adc_max)
{
    if (self == NULL || hadc == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->hadc = hadc;
    self->ph_adc_channel = ph_adc_channel;
    self->ph_adc_sampling_time = ph_adc_sampling_time;

    self->has_temp_channel = false;
    self->temp_adc_channel = 0;
    self->temp_adc_sampling_time = 0;

    self->has_do_pin = false;

    if (adc_max == 0U) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }
    self->adc_max = adc_max;

    if (vref <= 0.0f) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }
    self->vref = vref;

    ph_sensor_calib_reset(&self->calib);

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_set_do_pin(PH_Sensor_t *self, GPIO_t do_pin)
{
    if (self == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->do_pin = do_pin;
    self->has_do_pin = true;

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_disable_do_pin(PH_Sensor_t *self)
{
    if (self == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->has_do_pin = false;
    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_set_temp_channel(PH_Sensor_t *self,
                                              uint32_t temp_adc_channel,
                                              uint32_t temp_adc_sampling_time)
{
    if (self == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->has_temp_channel = true;
    self->temp_adc_channel = temp_adc_channel;
    self->temp_adc_sampling_time = temp_adc_sampling_time;

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_disable_temp_channel(PH_Sensor_t *self)
{
    if (self == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->has_temp_channel = false;
    return PH_SENSOR_STATUS_OK;
}

void ph_sensor_calibration_reset(PH_Sensor_t *self)
{
    if (self == NULL) {
        return;
    }

    ph_sensor_calib_reset(&self->calib);
}

bool ph_sensor_calibration_set_two_point(PH_Sensor_t *self,
                                         float v1, float ph1,
                                         float v2, float ph2,
                                         float calibration_temp_c)
{
    if (self == NULL) {
        return false;
    }

    return ph_sensor_calib_set_two_point(&self->calib,
                                         v1, ph1,
                                         v2, ph2,
                                         calibration_temp_c);
}

PH_Sensor_Status_t ph_sensor_read_raw(PH_Sensor_t *self, uint16_t *raw_out)
{
    if (self == NULL || raw_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    return ph_sensor_adc_read_channel(self,
                                      self->ph_adc_channel,
                                      self->ph_adc_sampling_time,
                                      raw_out);
}

PH_Sensor_Status_t ph_sensor_read_voltage(PH_Sensor_t *self, float *voltage_out)
{
    uint16_t raw;
    PH_Sensor_Status_t st;

    if (self == NULL || voltage_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    st = ph_sensor_read_raw(self, &raw);
    if (st != PH_SENSOR_STATUS_OK) {
        return st;
    }

    *voltage_out = ((float)raw * self->vref) / (float)self->adc_max;
    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_read_ph(PH_Sensor_t *self, float *ph_out)
{
    float voltage;
    PH_Sensor_Status_t st;

    if (self == NULL || ph_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (!self->calib.valid) {
        return PH_SENSOR_STATUS_NOT_CALIBRATED;
    }

    st = ph_sensor_read_voltage(self, &voltage);
    if (st != PH_SENSOR_STATUS_OK) {
        return st;
    }

    if (!ph_sensor_calib_voltage_to_ph(&self->calib, voltage, ph_out)) {
        return PH_SENSOR_STATUS_NOT_CALIBRATED;
    }

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_read_ph_tc(PH_Sensor_t *self,
                                        float temperature_c,
                                        float *ph_out)
{
    float voltage;
    PH_Sensor_Status_t st;

    if (self == NULL || ph_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (!self->calib.valid) {
        return PH_SENSOR_STATUS_NOT_CALIBRATED;
    }

    st = ph_sensor_read_voltage(self, &voltage);
    if (st != PH_SENSOR_STATUS_OK) {
        return st;
    }

    if (!ph_sensor_calib_voltage_to_ph_tc(&self->calib, voltage, temperature_c, ph_out)) {
        return PH_SENSOR_STATUS_NOT_CALIBRATED;
    }

    return PH_SENSOR_STATUS_OK;
}

bool ph_sensor_read_do(PH_Sensor_t *self, bool *state_out)
{
    GPIO_PinState s;

    if (self == NULL || state_out == NULL) {
        return false;
    }

    if (!self->has_do_pin) {
        return false;
    }

    s = HAL_GPIO_ReadPin(self->do_pin.port, self->do_pin.pin);
    *state_out = (s == GPIO_PIN_SET);

    return true;
}

PH_Sensor_Status_t ph_sensor_read_temp_raw(PH_Sensor_t *self, uint16_t *raw_out)
{
    if (self == NULL || raw_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (!self->has_temp_channel) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    return ph_sensor_adc_read_channel(self,
                                      self->temp_adc_channel,
                                      self->temp_adc_sampling_time,
                                      raw_out);
}

PH_Sensor_Status_t ph_sensor_read_temp_voltage(PH_Sensor_t *self, float *voltage_out)
{
    uint16_t raw;
    PH_Sensor_Status_t st;

    if (self == NULL || voltage_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    st = ph_sensor_read_temp_raw(self, &raw);
    if (st != PH_SENSOR_STATUS_OK) {
        return st;
    }

    *voltage_out = ((float)raw * self->vref) / (float)self->adc_max;
    return PH_SENSOR_STATUS_OK;
}
